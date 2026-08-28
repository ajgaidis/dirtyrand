#define _GNU_SOURCE
#include "libprimitive.h"
#include "modkrng.h"
#include "logging.h"
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <sys/random.h>

/* Device file descriptor */
static int krng_fd = -1;

//#############################################################################
// Utility functions and some helpers
//#############################################################################

static void
_modkrng_close(void)
{
  /* Close the device */
  close(krng_fd);

  /* Prevent further use of the old FD number */
  krng_fd = -1;
}

static int
modkrng_open(void)
{
  int rv; /* Return value */

  /* Check if descriptor is already open */
  if (0 <= krng_fd)
    return 0;

  /* Open the device */
  if ((rv = open(DEVICE_FILE_NAME, 0)) < 0) {
    fprintf(stderr,
            "[!] Error opening device file %s: %d.\n",
            DEVICE_FILE_NAME, rv);
    return rv;
  }
  krng_fd = rv;

  /* Register a destructor to close the device */
  atexit(_modkrng_close);

  /* Success! */
  return 0;
}

static int
_ioctl(ul request, struct krng_data *data)
{
  int rv;           /* Return value */

  /* Ensure device is open */
  if (0 > krng_fd)
    if ((rv = modkrng_open()))
      return rv;

  /* Send a command to the device */
  if ((rv = ioctl(krng_fd, request, data)) == -1) {
    fprintf(stderr, "[!] KRNG ioctl failed: %d.\n", rv);
    return rv;
  }

  /* Success! */
  return 0;
}

//#############################################################################
// Other exported functions that can be used for testing
//#############################################################################


int
krng_get_addr(struct krng_data *data)
{
  int rv = _ioctl(IOCTL_KRNG_GET_ADDR, data);
  log("&%s = %p\n", data->buf, data->addr);
  return rv;
}

int
krng_get_percpu_addr(struct krng_data *data)
{
  unsigned int cpu;
  int rv = _ioctl(IOCTL_KRNG_GET_PERCPU_ADDR, data);
  getcpu(&cpu, NULL);
  log("&(CPU#%d->%s) = %p\n", cpu, data->buf, data->addr);
  return rv;
}

int
krng_getrandom(struct krng_data *data)
{
  ssize_t rv = getrandom(data->buf, data->sz, GRND_NONBLOCK);
  if (rv == -1)
    handle_error("getrandom()");

  log("getrandom (wanted %luB, got %luB):\n", data->sz, rv);
  hexdump(data->buf, rv);

  /* Success! */
  return 0;
}

int
krng_read_urandom(struct krng_data *data)
{
  int fd;      /* File descriptor */
  ssize_t rv;  /* Return value */

  /* Open /dev/urandom file */
  if ((fd = open("/dev/urandom", O_RDONLY)) == -1)
    handle_error("open(/dev/urandom)");

  /* Read from /dev/urandom file */
  if ((rv = read(fd, data->buf, data->sz)) == -1)
    handle_error("read(/dev/urandom)");

  log("Read /dev/urandom (wanted %luB, got %luB):\n", data->sz, rv);
  hexdump(data->buf, rv);

  /* Cleanup */
  close(fd);

  /* Success! */
  return 0;
}

int
krng_read_random(struct krng_data *data)
{
  int fd;      /* File descriptor */
  ssize_t rv;  /* Return value */

  /* Open /dev/random file */
  if ((fd = open("/dev/random", O_RDONLY)) == -1)
    handle_error("open(/dev/random)");

  /* Read from /dev/random file */
  if ((rv = read(fd, data->buf, data->sz)) == -1)
    handle_error("read(/dev/random)");

  log("Read /dev/random (wanted %luB, got %luB):\n", data->sz, rv);
  hexdump(data->buf, rv);

  /* Cleanup */
  close(fd);

  /* Success! */
  return 0;
}

int
krng_get_random_data(struct krng_data *data)
{
  int rv; /* Return value */

  /* Ensure size field is correct and log some stuff */
  switch (data->rand_if)
  {
    case GET_RANDOM_BYTES:
    case GET_RANDOM_BYTES_WAIT:
      log("get_random_bytes* (sz = %lu):\n", data->sz);
      break;

    case GET_RANDOM_U8:
    case GET_RANDOM_U8_WAIT:
      log("get_random_u8*:\n");
      data->sz = sizeof(u8);
      break;

    case GET_RANDOM_U16:
    case GET_RANDOM_U16_WAIT:
      log("get_random_u16*:\n");
      data->sz = sizeof(u16);
      break;

    case GET_RANDOM_U32:
    case GET_RANDOM_U32_WAIT:
    case GET_RANDOM_U32_BELOW:
    case GET_RANDOM_U32_ABOVE:
    case GET_RANDOM_U32_INCLUSIVE:
      log("get_random_u32*:\n");
      data->sz = sizeof(u32);
      break;

    case GET_RANDOM_U64:
    case GET_RANDOM_U64_WAIT:
      log("get_random_u64*:\n");
      data->sz = sizeof(u64);
      break;

    case GET_RANDOM_LONG:
    case GET_RANDOM_LONG_WAIT:
      log("get_random_long*:\n");
      data->sz = sizeof(ul);
      break;

    default:
      handle_error_no_en("Unknown interface: %d\n", data->rand_if);
  }

  rv = _ioctl(IOCTL_KRNG_GET_RANDOM_DATA, data);
  hexdump(data->buf, data->sz);
  return rv;
}

int
krng_read_addr(struct krng_data *data)
{
  int rv = _ioctl(IOCTL_KRNG_READ_ADDR, data);
  log("Contents of %p:\n", data->addr);
  hexdump(data->buf, data->sz);
  return rv;
}

int
krng_write_addr(struct krng_data *data)
{
  log("Writing %lu bytes to %p\n", data->sz, data->addr);
  return _ioctl(IOCTL_KRNG_WRITE_ADDR, data);
}

int
krng_zap_l1_key(struct krng_data *data)
{
  log("Zapping L1 key\n");
  return _ioctl(IOCTL_KRNG_ZAP_L1_KEY, data);
}

int
krng_zap_batch_entropy(struct krng_data *data)
{
  log("Zapping batch entropy\n");
  return _ioctl(IOCTL_KRNG_ZAP_BATCH_ENTROPY, data);
}

int
krng_zap_l2_key(struct krng_data *data)
{
  log("Zapping L2 key\n");
  return _ioctl(IOCTL_KRNG_ZAP_L2_KEY, data);
}

int
krng_set_l1_gen(struct krng_data *data)
{
  log("Setting L1->generation = %lu\n", data->gen);
  return _ioctl(IOCTL_KRNG_SET_L1_GEN, data);
}

int
krng_set_l2_gen(struct krng_data *data)
{
  log("Setting L2->generation = %lu\n", data->gen);
  return _ioctl(IOCTL_KRNG_SET_L2_GEN, data);
}

int
krng_set_batch_gen(struct krng_data *data)
{
  log("Setting batch->generation = %lu\n", data->gen);
  return _ioctl(IOCTL_KRNG_SET_BATCH_GEN, data);
}

int
krng_set_batch_pos(struct krng_data *data)
{
  log("Setting batch->position = %d\n", data->pos);
  return _ioctl(IOCTL_KRNG_SET_BATCH_POS, data);
}

int
krng_zap_next_reseed(struct krng_data *data)
{
  log("Overwriting next_reseed with sys_ni_syscall\n");
  return _ioctl(IOCTL_KRNG_ZAP_NEXT_RESEED, data);
}

int
krng_zap_net_secret(struct krng_data *data)
{
  log("Zapping net_secret\n");
  return _ioctl(IOCTL_KRNG_ZAP_NET_SECRET, data);
}

int
krng_zap_table_perturb(struct krng_data *data)
{
  log("Zapping table_perturb\n");
  return _ioctl(IOCTL_KRNG_ZAP_TABLE_PERTURB, data);
}

//#############################################################################
// Main interface functions
//#############################################################################

void
trigger_overwrite_of_l1_key(void)
{
  INIT_DATA(data);
  struct timespec ts;

  log("Triggering overwrite of L1 key...\n");
  if ((_ioctl(IOCTL_KRNG_ZAP_L1_KEY, NULL)) != 0)
      handle_error_no_en("Overwriting L1 key failed!\n");
  log("Overwrote L1 key!\n");

  clock_gettime(CLOCK_REALTIME, &ts);
  data.gen = ts.tv_nsec;
  log("Triggering overwrite of L1 generation (gen = %lu)...\n", data.gen);
  if ((_ioctl(IOCTL_KRNG_SET_L1_GEN, &data)) != 0)
      handle_error_no_en("Overwriting L1 generation failed!\n");
  log("Overwrote L1 generation!\n");
}

void
trigger_overwrite_of_net_secret(void)
{
  if ((_ioctl(IOCTL_KRNG_ZAP_NET_SECRET, NULL)) != 0)
      handle_error_no_en("Overwriting net_secret failed!\n");
  log("Overwrote net_secret!\n");
}

void
trigger_overwrite_of_table_perturb(void)
{
  if ((_ioctl(IOCTL_KRNG_ZAP_TABLE_PERTURB, NULL)) != 0)
      handle_error_no_en("Overwriting table_perturb failed!\n");
  log("Overwrote table_perturb!\n");
}

void 
trigger_overwrite_of_pending_bit(void)
{
  if ((_ioctl(IOCTL_KRNG_ZAP_PENDING_BIT, NULL)) != 0)
    handle_error_no_en("Overwriting pending bit failed!\n");
}
