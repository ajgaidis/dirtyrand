#ifndef __MODKRNG_H__
#define __MODKRNG_H__

#include <linux/ioctl.h>
#include <linux/types.h>


/* Some type definitions to match the kernel */
typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;
typedef unsigned long ul;
typedef unsigned long long ull;
typedef ul size_t;

/* Device major number */
#define MAJOR_NUM 144

/* Device file name */
#define DEVICE_FILE_NAME  "/dev/modkrng"

/* IOCTL command numbers for device */
#define IOCTL_KRNG_GET_ADDR           _IOWR(MAJOR_NUM, 0,  struct krng_data *)
#define IOCTL_KRNG_GET_PERCPU_ADDR    _IOWR(MAJOR_NUM, 1,  struct krng_data *)
#define IOCTL_KRNG_GET_RANDOM_DATA    _IOWR(MAJOR_NUM, 2,  struct krng_data *)
#define IOCTL_KRNG_READ_ADDR          _IOWR(MAJOR_NUM, 3,  struct krng_data *)
#define IOCTL_KRNG_WRITE_ADDR         _IOWR(MAJOR_NUM, 4,  struct krng_data *)
#define IOCTL_KRNG_ZAP_L2_KEY         _IOWR(MAJOR_NUM, 5,  struct krng_data *)
#define IOCTL_KRNG_ZAP_L1_KEY         _IOWR(MAJOR_NUM, 6,  struct krng_data *)
#define IOCTL_KRNG_ZAP_BATCH_ENTROPY  _IOWR(MAJOR_NUM, 7,  struct krng_data *)
#define IOCTL_KRNG_SET_L1_GEN         _IOWR(MAJOR_NUM, 8,  struct krng_data *)
#define IOCTL_KRNG_SET_L2_GEN         _IOWR(MAJOR_NUM, 9,  struct krng_data *)
#define IOCTL_KRNG_SET_BATCH_GEN      _IOWR(MAJOR_NUM, 10, struct krng_data *)
#define IOCTL_KRNG_SET_BATCH_POS      _IOWR(MAJOR_NUM, 11, struct krng_data *)
#define IOCTL_KRNG_ZAP_NEXT_RESEED    _IOWR(MAJOR_NUM, 12, struct krng_data *)
#define IOCTL_KRNG_ZAP_NET_SECRET     _IOWR(MAJOR_NUM, 13, struct krng_data *)
#define IOCTL_KRNG_ZAP_TABLE_PERTURB  _IOWR(MAJOR_NUM, 14, struct krng_data *)
#define IOCTL_KRNG_ZAP_PENDING_BIT    _IOWR(MAJOR_NUM, 15, struct krng_data *)

/* Kernel random number interface functions */
typedef enum {
  RAND_IF_NOT_SPECIFIED = 0,

  /* Basic random interfaces */
  GET_RANDOM_BYTES,
  GET_RANDOM_U8,
  GET_RANDOM_U16,
  GET_RANDOM_U32,
  GET_RANDOM_U64,
  GET_RANDOM_LONG,

  /* Random interfaces that wait for input pool to be seeded */
  GET_RANDOM_BYTES_WAIT,
  GET_RANDOM_U8_WAIT,
  GET_RANDOM_U16_WAIT,
  GET_RANDOM_U32_WAIT,
  GET_RANDOM_U64_WAIT,
  GET_RANDOM_LONG_WAIT,

  /* Random interfaces that generate random numbers in some interval */
  GET_RANDOM_U32_BELOW,
  GET_RANDOM_U32_ABOVE,
  GET_RANDOM_U32_INCLUSIVE
} rand_if;

/* Kernel's random batch sizes */
typedef enum {
  BATCH_NOT_SPECIFIED = 0,

  U8,
  U16,
  U32,
  U64
} batch_type;

/* Input/output buffer size */
#define BUFFER_SIZE       256 /* Bytes */
#define HALF_BUFFER_SIZE  (BUFFER_SIZE / 2) /* Bytes */

/* Input/output data structure for use communicating with device */
struct krng_data {
  void *addr;            /* Address given to or returned from device */
  u8 buf[BUFFER_SIZE];   /* Buffer given to or returned from device */
  size_t sz;             /* Size of data in buffer */
  ul gen;                /* Generation counter of ChaCha CRNG */
  unsigned int pos;      /* Position marker of batch pool */
  int cpu;               /* CPU for per-cpu variables (-1 is current CPU) */
  rand_if rand_if;       /* Kernel random number interface to extract from */
  batch_type batch;      /* Random batch type for kernel */
  struct {               /* Random number interval specification */
    u32 floor;             /* Floor for random number interval */
    u32 ceil;              /* Ceiling for random number interval */
  } interval;
};
#define KRNG_DATA_SIZE  sizeof(struct krng_data)

/* Initializer macro for struct krng_data */
#define INIT_DATA(name)                \
  struct krng_data name = {            \
    .addr     = NULL,                  \
    .buf      = {0},                   \
    .sz       = 0,                     \
    .gen      = 0,                     \
    .pos      = 0,                     \
    .cpu      = -1,                    \
    .rand_if  = RAND_IF_NOT_SPECIFIED, \
    .batch    = BATCH_NOT_SPECIFIED,   \
    .interval = {0, 0},                \
  }

#define REINIT_DATA(data) ({          \
  memset(&(data), 0, KRNG_DATA_SIZE); \
  (data).cpu = -1;                    \
})

/* table_perturb size */
#define CONFIG_INET_TABLE_PERTURB_ORDER 16
#define INET_TABLE_PERTURB_SIZE (1 << CONFIG_INET_TABLE_PERTURB_ORDER)

/* ChaCha sizes */
#define CHACHA_KEY_SIZE         32 /* bytes */
#define CHACHA_BLOCK_SIZE       64 /* bytes */

/* Batch entropy size */
#define BATCH_ENTROPY_SIZE      (CHACHA_BLOCK_SIZE * 3 / 2)  /* bytes */

/* Offsets */
#define CRNG_GENERATION_OFFSET  CHACHA_KEY_SIZE
#define BATCH_GENERATION_OFFSET (BATCH_ENTROPY_SIZE + sizeof(local_lock_t))
#define BATCH_POSITION_OFFSET   (BATCH_GENERATION_OFFSET + sizeof(ul))

#endif /* __MODKRNG_H__ */
