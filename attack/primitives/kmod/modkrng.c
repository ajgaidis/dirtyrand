#define pr_fmt(fmt) "[modkrng]: " fmt

#include "modkrng.h"
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/kallsyms.h>
#include <linux/percpu-defs.h>
#include <linux/cpumask.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/workqueue_types.h>
#include <linux/types.h>

#define DEVICE_NAME "modkrng"

#define SUCCESS 0
#define FAILURE -1

static int
device_open(struct inode *inode, struct file *file)
{
  return SUCCESS;
}

static int
device_release(struct inode *inode, struct file *file)
{
  return SUCCESS;
}

static int
get_kernel_random_data(rand_if rand_if, void *buf, size_t sz,
                        u32 floor, u32 ceil)
{
  int rv = SUCCESS;         /* Return value */
  u8 rand_u8;               /* Random u8 value */
  u16 rand_u16;             /* Random u16 value */
  u32 rand_u32;             /* Random u32 value */
  u64 rand_u64;             /* Random u64 value */
  ul rand_long;             /* Random long value */

  /* Select the appropriate interface to generate randomness */
  switch (rand_if)
  {
    /*
     * Basic random interfaces
     */

    case GET_RANDOM_BYTES:
      get_random_bytes(buf, sz);
      break;

    case GET_RANDOM_U8:
      if (sizeof(u8) > sz)
        goto sz_err;

      rand_u8 = get_random_u8();
      memcpy(buf, &rand_u8, sizeof(u8));
      break;

    case GET_RANDOM_U16:
      if (sizeof(u16) > sz)
        goto sz_err;

      rand_u16 = get_random_u16();
      memcpy(buf, &rand_u16, sizeof(u16));
      break;

    case GET_RANDOM_U32:
      if (sizeof(u32) > sz)
        goto sz_err;

      rand_u32 = get_random_u32();
      memcpy(buf, &rand_u32, sizeof(u32));
      break;

    case GET_RANDOM_U64:
      if (sizeof(u64) > sz)
        goto sz_err;

      rand_u64 = get_random_u64();
      memcpy(buf, &rand_u64, sizeof(u64));
      break;

    case GET_RANDOM_LONG:
      if (sizeof(unsigned long) > sz)
        goto sz_err;

      rand_long = get_random_long();
      memcpy(buf, &rand_long, sizeof(unsigned long));
      break;

    /*
     * Random interfaces that wait for input pool to be seeded
     */

    case GET_RANDOM_BYTES_WAIT:
      if (!get_random_bytes_wait(buf, sz))
        goto wait_err;
      break;

    case GET_RANDOM_U8_WAIT:
      if (!get_random_u8_wait(buf))
        goto wait_err;
      break;

    case GET_RANDOM_U16_WAIT:
      if (!get_random_u16_wait(buf))
        goto wait_err;
      break;

    case GET_RANDOM_U32_WAIT:
      if (!get_random_u32_wait(buf))
        goto wait_err;
      break;

    case GET_RANDOM_U64_WAIT:
      if (!get_random_u64_wait(buf))
        goto wait_err;
      break;

    case GET_RANDOM_LONG_WAIT:
      if (!get_random_long_wait(buf))
        goto wait_err;
      break;

    /*
     * Random interfaces that generate random numbers in some interval
     */

    case GET_RANDOM_U32_BELOW:
      rand_u32 = get_random_u32_below(ceil);
      memcpy(buf, &rand_u32, sizeof(u32));
      break;

    case GET_RANDOM_U32_ABOVE:
      rand_u32 = get_random_u32_above(floor);
      memcpy(buf, &rand_u32, sizeof(u32));
      break;

    case GET_RANDOM_U32_INCLUSIVE:
      rand_u32 = get_random_u32_inclusive(floor, ceil);
      memcpy(buf, &rand_u32, sizeof(u32));
      break;


    default:
      pr_err("get_kernel_random_data: no such interface: %d.\n", rand_if);
      rv = -EINVAL;
  }

  return rv;

sz_err:
  pr_err("get_kernel_random_data: buffer too small for random data.\n");
  return -EINVAL;

wait_err:
  pr_err("get_kernel_random_data: function interrupted by signal.\n");
  return -ERESTARTSYS;
}

static void *
get_pcpu_addr(const char *buf, int cpu)
{
  void *off;  /* Offset of pcpu symbol */
  void *addr; /* Address of pcpu symbol for a given cpu */

  /* Get the per-cpu offset of the input symbol */
  off = (void *)kallsyms_lookup_name(buf);

  /* Get the address of the per-CPU variable */
  if (cpu < 0) {
    addr = get_cpu_ptr(off);
    put_cpu_ptr(off);
  }
  else if (cpu < NR_CPUS) {
    addr = per_cpu_ptr(off, cpu);
  }
  else {
    pr_err("Invalid CPU number: %d.\n", cpu);
    return NULL;
  }

  /* Return per-cpu address */
  return addr;
}

static const char *
get_batch_name(batch_type batch)
{
  switch (batch)
  {
    case U8:  return "batched_entropy_u8";
    case U16: return "batched_entropy_u16";
    case U32: return "batched_entropy_u32";
    case U64: return "batched_entropy_u64";
    default:  return NULL;
  }
}

static long
device_ioctl(struct file *file, unsigned int ioctl_num,
              unsigned long ioctl_param)
{
  INIT_DATA(data);    /* Initialize data to send and receive */
  int rv;             /* Return value */
  size_t sz;          /* Size of buffer data */
  void *addr;         /* Address of various kernel data */
  const char *batch;  /* Pointer to name of batch */
  ul *gen;            /* Pointer to generation field in crng structure */
  unsigned int *pos;  /* Pointer to batch position */
  struct delayed_work *next_reseed; /* Ptr to delayed work of next_reseed */
  u64 *net_secret;    /* net_secret random seed */
  u32 **table_perturb; /* table_perturb table of randomness */

  /* Get the struct used for communicating with user space */
  if (ioctl_param != 0) {
      if (copy_from_user((void *)&data, (void *)ioctl_param, KRNG_DATA_SIZE))
        return -ENOMEM;

      /* Get the intended size of the buf contents */
      sz = MIN(BUFFER_SIZE-1, data.sz);
  }


  /* Find the operation to perform. */
  switch (ioctl_num)
  {
    case IOCTL_KRNG_GET_ADDR:
    {
      /* Ensure the string in the buf is null terminated */
      data.buf[sz] = '\0';

      /* Set output address to kernel symbol address */
      data.addr = (void *)kallsyms_lookup_name(data.buf);

      break;
    }

    case IOCTL_KRNG_GET_PERCPU_ADDR:
    {
      /* Ensure the string in the buf is null terminated */
      data.buf[sz] = '\0';

      /* Get the per-cpu address */
      if (!(addr = get_pcpu_addr(data.buf, data.cpu)))
        return -EINVAL;

      break;
    }

    case IOCTL_KRNG_GET_RANDOM_DATA:
    {
      /* Get random data from a kernel interface */
      if ((rv = get_kernel_random_data(data.rand_if, data.buf, data.sz,
                                  data.interval.floor, data.interval.ceil))) {
        pr_err("Error handling IOCTL_KRNG_GET_RANDOM_DATA!\n");
        return rv;
      }

      break;
    }

    case IOCTL_KRNG_READ_ADDR:
    {
      /* Read sz bytes from specified address into buf */
      memcpy(data.buf, data.addr, sz);

      break;
    }

    case IOCTL_KRNG_WRITE_ADDR:
    {
      /* Write sz bytes from buf into specified address */
      memcpy(data.addr, data.buf, sz);

      break;
    }

    case IOCTL_KRNG_ZAP_L1_KEY:
    {
      /* Get addr of (L1) base crng structure */
      addr = (void *)kallsyms_lookup_name("base_crng");

      /* Zap the L1 crng chacha key */
      memset(addr, 0, CHACHA_KEY_SIZE);

      break;
    }

    case IOCTL_KRNG_ZAP_L2_KEY:
    {
      /* Get the per-cpu address of crngs */
      if (!(addr = get_pcpu_addr("crngs", data.cpu)))
        return -EINVAL;

      /* Zap the L2 crng ChaCha key */
      memset(addr, 0, CHACHA_KEY_SIZE);

      break;
    }

    case IOCTL_KRNG_ZAP_BATCH_ENTROPY:
    {
      /* Get the name of the batch we are targeting */
      if (!(batch = get_batch_name(data.batch))) {
        pr_err("Invalid batch type: %d.\n", data.batch);
        return -EINVAL;
      }

      /* Get the per-cpu address of the batch we are targeting */
      if (!(addr = get_pcpu_addr(batch, data.cpu)))
        return -EINVAL;

      /* Zap the batch entropy pool */
      memset(addr, 0, BATCH_ENTROPY_SIZE);

      break;
    }

    case IOCTL_KRNG_SET_L1_GEN:
    {
      /* Get addr of (L1) base crng structure */
      addr = (void *)kallsyms_lookup_name("base_crng");

      /* Set L1's generation count so the L2 cache gets synced */
      gen = (unsigned long *)((u8 *)addr + CRNG_GENERATION_OFFSET);
      *gen = data.gen;

      break;
    }

    case IOCTL_KRNG_SET_L2_GEN:
    {
      /* Get the per-cpu address of crngs */
      if (!(addr = get_pcpu_addr("crngs", data.cpu)))
        return -EINVAL;

      /* Set L1's generation count so the L2 cache gets synced */
      gen = (unsigned long *)((u8 *)addr + CRNG_GENERATION_OFFSET);
      *gen = data.gen;

      break;
    }

    case IOCTL_KRNG_SET_BATCH_GEN:
    {
      /* Get the name of the batch we are targeting */
      if (!(batch = get_batch_name(data.batch))) {
        pr_err("Invalid batch type: %d.\n", data.batch);
        return -EINVAL;
      }

      /* Get the per-cpu address of the batch we are targeting */
      if (!(addr = get_pcpu_addr(batch, data.cpu)))
        return -EINVAL;

      /* Set the batch's generation count */
      gen = (unsigned long *)((u8 *)addr + BATCH_GENERATION_OFFSET);
      *gen = data.gen;

      break;
    }

    case IOCTL_KRNG_SET_BATCH_POS:
    {
      /* Get the name of the batch we are targeting */
      if (!(batch = get_batch_name(data.batch))) {
        pr_err("Invalid batch type: %d.\n", data.batch);
        return -EINVAL;
      }

      /* Get the per-cpu address of the batch we are targeting */
      if (!(addr = get_pcpu_addr(batch, data.cpu)))
        return -EINVAL;

      /* Set the batch's position marker */
      pos = (unsigned int *)((u8 *)addr + BATCH_POSITION_OFFSET);
      *pos = data.pos;

      break;
    }

    case IOCTL_KRNG_ZAP_NEXT_RESEED:
    {
      /* Get the delayed_work struct for next_reseed */
      /* XXX: where does the .11 come from? Compiler name mangling? */
      next_reseed = (struct delayed_work *)kallsyms_lookup_name("next_reseed.11");

      /* Get the address of a function to replace the work function with */
      addr = (void *)kallsyms_lookup_name("sys_ni_syscall");

      /* Overwrite the work_func_t function pointer */
      next_reseed->work.func = (work_func_t)addr;

      break;
    }

    case IOCTL_KRNG_ZAP_PENDING_BIT:
    {
      /* Get the delayed_work struct for next_reseed */
      next_reseed = (struct delayed_work *)kallsyms_lookup_name("next_reseed.11");

      /* Overwrite counter (data field) to set pending bit to true */
      //unsigned long *counter = (unsigned long *)next_reseed;
      unsigned long *pprev = (unsigned long *)next_reseed->timer.entry.pprev;
      //*counter = 0xffff888100154205;
      *pprev = 0x1;

      break;
    }

    case IOCTL_KRNG_ZAP_NET_SECRET:
    {
      /* Get the address of the net_secret data structure */
      net_secret = (u64 *)kallsyms_lookup_name("net_secret");

      /* Zap it to 0 */
      net_secret[0] = 0;
      net_secret[1] = 0;

      break;
    }

    case IOCTL_KRNG_ZAP_TABLE_PERTURB:
    {
      /* Get the address of the table_perturb data structure */
      table_perturb = (u32 **)kallsyms_lookup_name("table_perturb");
      pr_info("((u32 **)table_perturb)    = 0x%p\n", table_perturb);
      pr_info("((u32 **)table_perturb)[0] = 0x%p\n", table_perturb[0]);

      /* Zap it to 0 */
      pr_info("((u32 **)table_perturb)[0][0] before zap: 0x%x\n",
              table_perturb[0][0]);
      pr_info("((u32 **)table_perturb)[0][1] before zap: 0x%x\n",
              table_perturb[0][1]);
      pr_info("((u32 **)table_perturb)[0][2] before zap: 0x%x\n",
              table_perturb[0][2]);
      memset(table_perturb[0], 0, INET_TABLE_PERTURB_SIZE * sizeof(u32));
      pr_info("((u32 **)table_perturb)[0][0] before zap: 0x%x\n",
              table_perturb[0][0]);
      pr_info("((u32 **)table_perturb)[0][1] before zap: 0x%x\n",
              table_perturb[0][1]);
      pr_info("((u32 **)table_perturb)[0][2] before zap: 0x%x\n",
              table_perturb[0][2]);

      break;
    }

    default:
    {
      pr_err("krng ioctl: no such command: %d.\n", ioctl_num);
      return -EINVAL;
    }
  }

  if (ioctl_param != 0) {
      /* Copy the data back to the user */
      if (copy_to_user((void *)ioctl_param, (void *)&data, KRNG_DATA_SIZE))
        return -ENOMEM;
  }

  return SUCCESS;
}

/* File operations for device */
struct file_operations fops = {
  .unlocked_ioctl = device_ioctl,
  .open           = device_open,
  .release        = device_release,
};

static int __init
modkrng_init(void)
{
  int rv; /* Return value */

  /* Verbose */
  pr_info("Initializing module...\n");

  /* Register the character device */
  if ((rv = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops)) < 0) {
    pr_err("Registering character device failed with %d.\n", rv);
    return FAILURE;
  }

  /* Verbose */
  pr_info("Character device registration success! Major device number = %d.\n",
            MAJOR_NUM);
  pr_info("Create a device file with:\n\tmknod %s c %d 0\n",
            DEVICE_FILE_NAME, MAJOR_NUM);
  pr_info("Module initialization complete!\n");

  return SUCCESS;
}

static void __exit
modkrng_exit(void)
{
  /* Verbose */
  pr_info("Cleaning up before exiting module...\n");

  /* Unregister character device */
  unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

  /* Verbose */
  pr_info("Unregistered character device: %s.\n", DEVICE_FILE_NAME);
  pr_info("Exited module.\n");
}

module_init(modkrng_init);
module_exit(modkrng_exit);

/* Module meta data */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anonymous Submission #60");
MODULE_DESCRIPTION("KRNG helper module");
MODULE_VERSION("1.0");
