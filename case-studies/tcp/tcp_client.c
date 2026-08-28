#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_IP     "10.0.0.2"
#define PORT          80
#define REQUEST_PATH  "/index.nginx-debian.html"
#define CONNECTION    "keep-alive"  /* or: "close" */
#define BUFF_SIZE     2048

#define SLEEP_TIME    30


#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)


static void do_http_request(int sockfd)
{
  ssize_t bytes_rcv;
  char request_buffer[BUFF_SIZE];
  char response_buffer[BUFF_SIZE];

  /* Prepare the request string */
  snprintf(request_buffer, sizeof(request_buffer),
   "GET %s HTTP/1.0\r\n"
   "Host: %s\r\n"
   "Connection: %s\r\n"
   "\r\n",
   REQUEST_PATH, SERVER_IP, CONNECTION);

  /* Send request to server */
  printf("[o] Sending message to server:\n");
  printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
  printf("%s", request_buffer);
  printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
  if (send(sockfd, request_buffer, strlen(request_buffer), 0) < 0)
    handle_error("send()");
  printf("[+] Sent message to %s:%d\n", SERVER_IP, PORT);

  /* Read response from server */
  printf("[o] Waiting for response from %s:%d...\n", SERVER_IP, PORT);
  bytes_rcv = recv(sockfd, response_buffer, BUFF_SIZE - 1, 0);
  if (bytes_rcv > 0) {
    response_buffer[bytes_rcv] = '\0';
    printf("[+] Server response:\n");
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    printf("%s\n", response_buffer);
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
  } else if (bytes_rcv == 0) {
    printf("[!] Server closed the connection.\n");
  } else {
    handle_error("recv()");
  }
}

int main(void)
{
  int sockfd;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t socklen;
  char client_ip[16];
  unsigned int client_port;

  /* Create socket file descriptor */
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    handle_error("socket()");

  /* Setup server address info */
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    handle_error("inet_pton()");

  /* Connect to the server */
  printf("[o] Connecting to %s:%d...\n", SERVER_IP, PORT);
  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    handle_error("connect()");
  printf("[+] Connected to %s:%d\n", SERVER_IP, PORT);

  /* Get client info */
  memset(&client_addr, 0, sizeof(struct sockaddr_in));
  socklen = sizeof(struct sockaddr_in);
  if (getsockname(sockfd, (struct sockaddr *)&client_addr, &socklen) < 0)
    handle_error("getsockname()");
  memset(client_ip, 0, sizeof(client_ip));
  if (inet_ntop(AF_INET, &client_addr.sin_addr,
        client_ip, sizeof(client_ip)) == NULL)
    handle_error("inet_ntop()");
  client_port = ntohs(client_addr.sin_port);

  /* Print out IP/port combos */
  printf("[+] Connection info:\n");
  printf("  ... Server IP:   %s\n", SERVER_IP);
  printf("  ... Server port: %d\n", PORT);
  printf("  ... Client IP:   %s\n", client_ip);
  printf("  ... Client port: %d\n", client_port);

  /* Do the http request and get a response */
  do_http_request(sockfd);

  /* Sleep to allow the attacker time to do their thing */
  printf("[o] Sleeping for %d seconds...\n", SLEEP_TIME);
  sleep(SLEEP_TIME);
  printf("[+] Woke up after %d seconds.\n", SLEEP_TIME);

  /* Do another http request and get a response */
  do_http_request(sockfd);

  /* Close the socket */
  if (close(sockfd) < 0)
    handle_error("close()");
  printf("[+] Socket closed successfully!\n");

  /* Success! */
  return EXIT_SUCCESS;
}
