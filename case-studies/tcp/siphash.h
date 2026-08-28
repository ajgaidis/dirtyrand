#ifndef __SIPHASH_H__
#define __SIPHASH_H__

typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

typedef struct {
	u64 key[2];
} siphash_key_t;

u64 siphash_3u32(const u32 first, const u32 second, const u32 third,
									const siphash_key_t *key);
u64 siphash_2u64(const u64 first, const u64 second, const siphash_key_t *key);
u64 siphash_4u32(const u32 a, const u32 b, const u32 c, const u32 d,
									const siphash_key_t *key);

#endif /* __SIPHASH_H__ */
