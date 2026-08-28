#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include "libprimitive.h"
#define DEBUG
#include "logging.h"

/* Error checking macro */
#define CHECK_RET(ret, msg) \
    if ((ret) < 0) { \
        handle_error_no_en("%s: %s\n", msg, gnutls_strerror((ret))); \
    }

/* Configuration */
const char *HOST = "10.116.70.102";
unsigned short PORT = 4433;
const char *HTTP_REQUEST = "GET / HTTP/1.1\r\n" \
                           "Host: 10.116.70.102\r\n" \
                           "Connection: close\r\n\r\n";
const char *PRIORITY = "NONE:+VERS-TLS1.2:+RSA:+AES-128-GCM:+AES-128-CBC:" \
                        "+SHA1:+SHA256:+SIGN-ALL:+COMP-NULL:+CTYPE-X509" \
                        ":%NO_SESSION_HASH";

/* Lil' hack to get at the exported *set_gettime* function in GnuTLS */
typedef void (*gnutls_gettime_func)(struct timespec *t);
extern void _gnutls_global_set_gettime_function(gnutls_gettime_func gettime_func);

void gnutls_fixed_gettime(struct timespec *t)
{
    t->tv_nsec = 0;
    t->tv_sec  = 0;
}

/*
 * This function prints GnuTLS logs with dbg
 */
void gnutls_debug_func(int level, const char *msg)
{
    dbg("GnuTLS [%d]: %s", level, msg);
}

/*
 * Creates a standard TCP socket and connects to the server.
 */
int tcp_connect(const char *ip, unsigned short port)
{
    int sock;
    struct sockaddr_in server_addr;

    /* Create socket file descriptor */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        handle_error("socket creation failed");

    /* Setup server address info */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
        handle_error("IP conversion failed");

    /* Connect to the server */
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        handle_error("TCP connect failed");

    /* Success! */
    log("Successfully connected TCP socket to %s:%hu\n", ip, port);
    return sock;
}

int main(void)
{
    int ret, sock;
    gnutls_session_t session;
    gnutls_certificate_credentials_t x509_creds;
    char buffer[16384]; /* 16K buffer for response */

    /* Initializes the library, PRNG, etc. */
    CHECK_RET(gnutls_global_init(), "GnuTLS global init");

    /* Initialize debug logging */
    gnutls_global_set_log_function(gnutls_debug_func);
    gnutls_global_set_log_level(9); /* 10 is max */

    /* Allocate certificate credentials structure */
    CHECK_RET(gnutls_certificate_allocate_credentials(&x509_creds),
            "Alloc cert credentials");

    /* Load System Trusted CAs */
    CHECK_RET(gnutls_certificate_set_x509_system_trust(x509_creds),
            "Set system trust");

    /* Initialize TLS Session */
    CHECK_RET(gnutls_init(&session, GNUTLS_CLIENT), "GnuTLS init client");

    /* Change the gettime function to eliminate the time variable for now */
    _gnutls_global_set_gettime_function(gnutls_fixed_gettime);
    log("Changed gnutls' gettime function.\n");

    /* Set the security priorities */ 
    CHECK_RET(gnutls_priority_set_direct(session, PRIORITY, NULL),
            "Set priority");

    /* Set session credentials */
    CHECK_RET(gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE,
                x509_creds), "Set credentials");
    
    /* Set Server Name Indication (SNI) for virtual hosting stuff */
    CHECK_RET(gnutls_server_name_set(session, GNUTLS_NAME_DNS, HOST,
                strlen(HOST)), "Set SNI");
    
    /* Create TCP Connection */
    sock = tcp_connect(HOST, PORT);

    /* Bind GnuTLS to the socket */
    gnutls_transport_set_int(session, sock);

    /* !!!!!!!!!!!!!!!!!!!!!!!! */
    /* !!!!!!! Zap RNG !!!!!!!! */
    /* !!!!!!!!!!!!!!!!!!!!!!!! */
    trigger_overwrite_of_l1_key();
    
    /* Perform TLS Handshake */
    log("Performing TLS handshake...\n");
    do {
        ret = gnutls_handshake(session);
    } while (ret < 0 && gnutls_error_is_fatal(ret) == 0);
    
    /* Check for a fatal handshake error */
    CHECK_RET(ret, "TLS Handshake failed");
    log("TLS Handshake successful.\n");

    /* Send encrypted HTTP request */
    log("Sending HTTP request...\n");
    CHECK_RET(gnutls_record_send(session, HTTP_REQUEST,
                strlen(HTTP_REQUEST)), "Record send");

    /* Receive Encrypted HTTP Response */
    log("Receiving HTTP response:\n");
    log("----------------------------------------\n");
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ret = gnutls_record_recv(session, buffer, sizeof(buffer) - 1);

        if (ret == 0) {
            /* Server closed the connection */
            log("----------------------------------------\n");
            log("Peer closed connection.\n");
            break;
        } else if (ret < 0 && gnutls_error_is_fatal(ret) == 0) {
            /* Non-fatal error (e.g., GNUTLS_E_AGAIN), try again */
            continue;
        } else if (ret < 0) {
            /* Fatal error */
            handle_error_no_en("Record recv failed: %s\n",
                    gnutls_strerror(ret));
            break;
        }

        /* Print the data we received */
        log("%s", buffer);
    }

    /* Cleanup */
    gnutls_bye(session, GNUTLS_SHUT_RDWR);           /* Send close_notify */
    gnutls_deinit(session);                          /* Free session */
    close(sock);                                     /* Close socket */
    gnutls_certificate_free_credentials(x509_creds); /* Free credentials */
    gnutls_global_deinit();                          /* Free global state */

    /* All done! */
    return 0;
}
