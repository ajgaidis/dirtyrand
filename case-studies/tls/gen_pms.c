#include <bits/time.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include "libprimitive.h"
#include "logging.h"

/* A standard pre-master secret is 48 bytes */
#define PREMASTER_SECRET_SIZE 48
/* The random portion is the total size minus the 2 version bytes */
#define RANDOM_DATA_SIZE (PREMASTER_SECRET_SIZE - 2)

int main(void)
{
    unsigned char premaster_secret[PREMASTER_SECRET_SIZE];
    int ret;
    const char *errstr;
    struct timespec t;

    /* Initialize the GnuTLS global state. */
    ret = gnutls_global_init();
    if (ret < 0) {
        errstr = gnutls_strerror(ret);
        handle_error_no_en("Failed to initialize GnuTLS: %s\n", errstr);
    }
    log("GnuTLS initialized.\n");

    /* Verbose */
    log("Generating 48-byte pre-master secret...\n");

    /* Set first two bytes to client's highest supported TLS version */
    premaster_secret[0] = 0x03; /* Major version (TLS 1.2) */
    premaster_secret[1] = 0x03; /* Minor version (TLS 1.2) */

    /* Zap RNG  */
    trigger_overwrite_of_l1_key();

    /* Sample time; wait to print until after we call gnutls_rnd */
    clock_gettime(CLOCK_REALTIME, &t);

    /*
     * Use GnuTLS's CSPRNG to generate the remaining 46 random bytes.
     * We use GNUTLS_RND_RANDOM, which is intended for generating session
     * keys. This is the first call, so it will also trigger the internal
     * RNG to be seeded by the OS.
     */
    ret = gnutls_rnd(GNUTLS_RND_RANDOM, &premaster_secret[2],
                    RANDOM_DATA_SIZE);
    if (ret < 0) {
        errstr = gnutls_strerror(ret);
        gnutls_global_deinit();
        handle_error_no_en("Failed to generate random data: %s\n", errstr);
    }

    /* Log estimated time stamp */
    log("Estimated timestamp (struct timespec t):\n"
            "\tt.tv_sec  = %ld\n"
            "\tt.tv_nsec = %ld\n", t.tv_sec, t.tv_nsec);

    /* Verbose */
    log("Generated Pre-Master Secret (48 bytes):\n");
    hexdump(premaster_secret, PREMASTER_SECRET_SIZE);

    /* Cleanup */
    gnutls_global_deinit();

    /* Success! */
    return 0;
}
