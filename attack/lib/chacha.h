#ifndef __COMMON_H__
#define __COMMON_H__

#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


typedef uint8_t __u8;
typedef __u8 u8;
typedef uint16_t __u16;
typedef __u16 u16;
typedef uint32_t __u32;
typedef __u32 u32;
typedef unsigned long ul;
typedef unsigned long long ull;

#define CHACHA_STATE_WORDS		16  /* 4-byte words */
#define CHACHA_KEY_SIZE				32  /* bytes */
#define CHACHA_BLOCK_SIZE			64  /* bytes */
#define CHACHA_NROUNDS				20

enum chacha_constants { /* expand 32-byte k */
	CHACHA_CONSTANT_EXPA = 0x61707865U,
	CHACHA_CONSTANT_ND_3 = 0x3320646eU,
	CHACHA_CONSTANT_2_BY = 0x79622d32U,
	CHACHA_CONSTANT_TE_K = 0x6b206574U
};

static inline void
chacha_init_consts(u32 *state)
{
	state[0] = CHACHA_CONSTANT_EXPA;
	state[1] = CHACHA_CONSTANT_ND_3;
	state[2] = CHACHA_CONSTANT_2_BY;
	state[3] = CHACHA_CONSTANT_TE_K;
}

struct crng {
	u8 key[CHACHA_KEY_SIZE];
};

#endif /* __COMMON_H__ */
