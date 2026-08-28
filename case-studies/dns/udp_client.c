#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#define SERVER_IP "10.0.0.2"
#define SERVER_PORT 12345
#define MESSAGE "Hello from UDP client!"

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t len;
    char client_ip_str[INET_ADDRSTRLEN];
    int client_port;

    /* Create UDP socket */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        handle_error("socket()");

    /* Get server address information */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    /* Convert server IP string to network address structure */
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        handle_error("inet_pton()");
    }

    /* Verbose */
    printf("[+] Sending UDP packet to server %s:%d\n", SERVER_IP, SERVER_PORT);

    /* Send message to server */
    if (sendto(sockfd, (const char *)MESSAGE, strlen(MESSAGE), 0,
               (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        handle_error("sendto()");
    }

    /* Get client's actual bound address (IP and Port) */
    len = sizeof(client_addr);
    if (getsockname(sockfd, (struct sockaddr *)&client_addr, &len) < 0) {
        close(sockfd);
        handle_error("getsockname()");
    }

    /* Convert client IP to string */
    if (inet_ntop(AF_INET, &client_addr.sin_addr,
                    client_ip_str, INET_ADDRSTRLEN) == NULL) {
        close(sockfd);
        handle_error("inet_ntop()");
    }
    client_port = ntohs(client_addr.sin_port);

    /* Print the 4-tuple */
    printf("[+] Connection info:\n");
    printf("  ... Server IP:   %s\n", SERVER_IP);
    printf("  ... Server port: %d\n", SERVER_PORT);
    printf("  ... Client IP:   %s\n", client_ip_str);
    printf("  ... Client port: %d\n", client_port);

    /* Cleanup */
    close(sockfd);

    /* Success! */
    return EXIT_SUCCESS;
}
