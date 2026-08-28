#ifndef __LIBPRIMITIVE_H__
#define __LIBPRIMITIVE_H__

#include <stddef.h>
#include "modkrng.h"

//#############################################################################
// Main interface functions
//#############################################################################

void trigger_overwrite_of_l1_key(void);
void trigger_overwrite_of_net_secret(void);
void trigger_overwrite_of_table_perturb(void);
void trigger_overwrite_of_pending_bit(void);

//#############################################################################
// Other exported functions that can be used for testing
//#############################################################################

/* Generic function type for krng functions */
typedef int (*krng_func_t)(struct krng_data *);

/* Get address of kernel symbol */
int krng_get_addr(struct krng_data *data);

/* Get address of per-CPU kernel symbol */
int krng_get_percpu_addr(struct krng_data *data);

/* Get random data from getrandom syscall */
int krng_getrandom(struct krng_data *data);

/* Get random data from /dev/urandom */
int krng_read_urandom(struct krng_data *data);

/* Get random data from /dev/random */
int krng_read_random(struct krng_data *data);

/* Get random data from kernel interface rand_if */
int krng_get_random_data(struct krng_data *data);

#define KRNG_GET_RANDOM_XXX(type_lower, type_upper)                           \
int krng_get_random_ ##type_lower(struct krng_data *data)                     \
{                                                                             \
  data->rand_if = GET_RANDOM_ ##type_upper;                                   \
  return krng_get_random_data(data);                                          \
}

#define KRNG_GET_RANDOM_U32_INTERVAL(type_lower, type_upper)                  \
int krng_get_random_u32_ ##type_lower(struct krng_data *data)                 \
{                                                                             \
  data->rand_if = GET_RANDOM_U32_ ##type_upper;                               \
  return krng_get_random_data(data);                                          \
}

KRNG_GET_RANDOM_XXX(bytes, BYTES)
KRNG_GET_RANDOM_XXX(u8, U8)
KRNG_GET_RANDOM_XXX(u16, U16)
KRNG_GET_RANDOM_XXX(u32, U32)
KRNG_GET_RANDOM_XXX(u64, U64)
KRNG_GET_RANDOM_XXX(long, LONG)

KRNG_GET_RANDOM_XXX(bytes_wait, BYTES_WAIT)
KRNG_GET_RANDOM_XXX(u8_wait, U8_WAIT)
KRNG_GET_RANDOM_XXX(u16_wait, U16_WAIT)
KRNG_GET_RANDOM_XXX(u32_wait, U32_WAIT)
KRNG_GET_RANDOM_XXX(u64_wait, U64_WAIT)
KRNG_GET_RANDOM_XXX(long_wait, LONG_WAIT)

KRNG_GET_RANDOM_U32_INTERVAL(below, BELOW)
KRNG_GET_RANDOM_U32_INTERVAL(above, ABOVE)
KRNG_GET_RANDOM_U32_INTERVAL(inclusive, INCLUSIVE)

/* Read from kernel address */
int krng_read_addr(struct krng_data *data);

/* Write to kernel address */
int krng_write_addr(struct krng_data *data);

/* Zap L1 (base_crng) key */
int krng_zap_l1_key(struct krng_data *data);

/* Zap L2 (crngs) key */
int krng_zap_l2_key(struct krng_data *data);

/* Zap batch entropy */
int krng_zap_batch_entropy(struct krng_data *data);

/* Set L1 (base_crng) generation field */
int krng_set_l1_gen(struct krng_data *data);

/* Set L2 (crngs) generation field */
int krng_set_l2_gen(struct krng_data *data);

/* Set batch generation field */
int krng_set_batch_gen(struct krng_data *data);

/* Set batch position field */
int krng_set_batch_pos(struct krng_data *data);

/* Overwrite next_reseed with sys_ni_syscall */
int krng_zap_next_reseed(struct krng_data *data);

/* Overwrite net_secret with zeroes */
int krng_zap_net_secret(struct krng_data *data);

/* Overwrite table_perturb with zeroes */
int krng_zap_table_perturb(struct krng_data *data);

#endif /* __LIBPRIMITIVE_H__ */
