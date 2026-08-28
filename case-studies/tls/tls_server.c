#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#define DEBUG
#include "logging.h"

/* Error checking macro */
#define CHECK_RET(ret, msg) \
    if ((ret) < 0) { \
        handle_error_no_en("%s: %s\n", msg, gnutls_strerror((ret))); \
    }

/* Configuration stuff */
const char *LISTEN_PORT = "4433";
const char *CERT_FILE = "server-cert.pem";
const char *KEY_FILE = "server-key.pem";
const char *HTTP_RESPONSE = "HTTP/1.1 200 OK\r\n" \
                             "Content-Type: text/plain\r\n" \
                             "Connection: close\r\n\r\n" \
                             "Hello from GnuTLS server!\r\n";
const char *PRIORITY = "NONE:+VERS-TLS1.2:+RSA:+AES-128-GCM:+AES-128-CBC:" \
                        "+SHA1:+SHA256:+SIGN-ALL:+COMP-NULL:+CTYPE-X509" \
                        ":%NO_SESSION_HASH";

/*
 * Creates a TCP listening socket.
 */
int tcp_listen(const char *port)
{
    int listen_sock;
    struct sockaddr_in serv_addr;
    int opt;
    int saved_errno;

    /* Create socket */
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        handle_error("socket creation failed");

    /* Set socket options (allow reuse) */
    opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind to address and port */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(atoi(port));

    if (bind(listen_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        saved_errno = errno;
        close(listen_sock);
        handle_error_en(saved_errno, "bind failed");
    }

    /* Listen for connections */
    if (listen(listen_sock, 10) < 0) {
        saved_errno = errno;
        close(listen_sock);
        handle_error_en(saved_errno, "listen_failed");
    }

    /* Success! */
    log("Server listening on port %s\n", port);
    return listen_sock;
}

int main(void)
{
    int ret, listen_sock, conn_sock;
    gnutls_session_t session;
    gnutls_certificate_credentials_t x509_creds;
    unsigned char fixed_server_random[32];
    gnutls_datum_t server_random_datum;
    char buffer[1024];

    /* GnuTLS Global Init */
    CHECK_RET(gnutls_global_init(), "GnuTLS global init");

    /* Allocate credentials */
    CHECK_RET(gnutls_certificate_allocate_credentials(&x509_creds),
            "Alloc cert credentials");

    /* Load credentials */
    CHECK_RET(gnutls_certificate_set_x509_key_file(x509_creds, CERT_FILE,
                KEY_FILE, GNUTLS_X509_FMT_PEM), "Set key/cert file");

    /* Create TCP listener */
    listen_sock = tcp_listen(LISTEN_PORT);

    /*
     * ======================== 
     * === Main server loop ===
     * ========================
     */
    while (1) {
        log("Waiting for new connection...\n");
        conn_sock = accept(listen_sock, NULL, NULL);
        if (conn_sock < 0) {
            color_perror("accept failed");
            continue;
        }

        /* Verbose */
        log("Connection accepted.\n");
        log("Starting TLS handshake...\n");

        /* Initialize TLS session */
        CHECK_RET(gnutls_init(&session, GNUTLS_SERVER), "GnuTLS init server");

        /* Set security priorities; MUST match the client's allowed set */
        CHECK_RET(gnutls_priority_set_direct(session, PRIORITY, NULL),
                "Set priority");

        /* Set server credentials */
        CHECK_RET(gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE,
                    x509_creds), "Set credentials");
        
        /*
         * Manipulate the server_random value so that we know it and it isn't
         * some random thing further influencing our randomness. Fill with 0x0.
         */
        memset(fixed_server_random, 0x0, sizeof(fixed_server_random));

        /* Setup the server_random to used */
        server_random_datum.data = fixed_server_random;
        server_random_datum.size = sizeof(fixed_server_random);

        /* Override the internal CSPRNG */
        CHECK_RET(gnutls_handshake_set_random(session, &server_random_datum),
                "Failed to set custom server_random");
        
        /* Verbose */
        log("Set custom server_random to 32 bytes of 0x0\n");

        /* Bind GnuTLS to the socket */
        gnutls_transport_set_int(session, conn_sock);

        /* Perform the handshake */
        do {
            ret = gnutls_handshake(session);
        } while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

        if (ret < 0) {
            color_error("Handshake failed: %s\n", gnutls_strerror(ret));
            close(conn_sock);
            gnutls_deinit(session);
            continue;
        }
        log("Handshake successful.\n");

        /* Read the client's request */
        memset(buffer, 0, sizeof(buffer));
        ret = gnutls_record_recv(session, buffer, sizeof(buffer) - 1);
        if (ret > 0) {
            log("Client sent:\n%s\n", buffer);
        } else if (ret == 0) {
            log("Client sent empty request\n");
        } else {
            color_error("Recv client request failed: %s\n",
                    gnutls_strerror(ret));
        }

        /* Send a response */
        log("Sending HTTP response...\n");
        gnutls_record_send(session, HTTP_RESPONSE, strlen(HTTP_RESPONSE));

        /* Local cleanup */
        gnutls_bye(session, GNUTLS_SHUT_RDWR);           /* Send close_notify */
        gnutls_deinit(session);                          /* Free session */
        close(conn_sock);                                /* Close socket */
    }

    /* Global cleanup */
    close(listen_sock);                               /* Close socket */
    gnutls_certificate_free_credentials(x509_creds); /* Free credentials */
    gnutls_global_deinit();                          /* Free global state */

    /* All done! */
    return 0;
}
