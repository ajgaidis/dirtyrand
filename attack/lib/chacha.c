#include <asm-generic/errno-base.h>
#include <string.h>
#include <getopt.h>
#include "chacha.h"
#include "logging.h"


#define MIN(x,y)						((x)<(y)?(x):(y))
#define ARRAY_SIZE(x)				(sizeof(x) / sizeof((x)[0]))

/* Globals */
struct crng l1_crng = {0};
struct crng l2_crng = {0};
static int start_from_l1 = 0;
static int l1_key_calculated = 0;
static int emulate_user = 0;


static inline __u32
rol32(__u32 word, unsigned int shift)
{
	return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

static inline void
__put_unaligned_le16(u16 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
}

static inline void
__put_unaligned_le32(u32 val, u8 *p)
{
	__put_unaligned_le16(val >> 16, p + 2);
	__put_unaligned_le16(val, p);
}

static inline void
put_unaligned_le32(u32 val, void *p)
{
	__put_unaligned_le32(val, p);
}

static void
chacha_permute(u32 *x, int nrounds)
{
	int i;

	/* Whitelist the allowed round counts */
	if (nrounds != 20 && nrounds != 12) {
		handle_error_en(EINVAL, "invalid round count");
	}

	for (i = 0; i < nrounds; i += 2) {
		x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],  16);
		x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],  16);
		x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],  16);
		x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],  16);

		x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],  12);
		x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],  12);
		x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10], 12);
		x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11], 12);

		x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],   8);
		x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],   8);
		x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],   8);
		x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],   8);

		x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],   7);
		x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],   7);
		x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10],  7);
		x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11],  7);

		x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],  16);
		x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],  16);
		x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],  16);
		x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],  16);

		x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10], 12);
		x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11], 12);
		x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],  12);
		x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],  12);

		x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],   8);
		x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],   8);
		x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],   8);
		x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],   8);

		x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10],  7);
		x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11],  7);
		x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],   7);
		x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],   7);
	}
}

static void
chacha20_block(u32 *state, u8 *stream)
{
	u32 x[CHACHA_STATE_WORDS];
	int i;

	memcpy(x, state, 64);

	chacha_permute(x, CHACHA_NROUNDS);

	for (i = 0; i < ARRAY_SIZE(x); i++)
		put_unaligned_le32(x[i] + state[i], &stream[i * sizeof(u32)]);

	state[12]++;
}

static void
crng_fast_key_erasure(u8 key[CHACHA_KEY_SIZE],
											u32 chacha_state[CHACHA_STATE_WORDS],
											u8 *random_data, size_t random_data_len)
{
	u8 first_block[CHACHA_BLOCK_SIZE];

	chacha_init_consts(chacha_state);
	memcpy(&chacha_state[4], key, CHACHA_KEY_SIZE);
	memset(&chacha_state[12], 0, sizeof(u32) * 4);
	chacha20_block(chacha_state, first_block);

	memcpy(key, first_block, CHACHA_KEY_SIZE);
	memcpy(random_data, first_block + CHACHA_KEY_SIZE, random_data_len);
	memset(first_block, 0, sizeof(first_block));
}

static void
crng_make_state(u32 chacha_state[CHACHA_STATE_WORDS], u8 *random_data,
								size_t random_data_len)
{
	/*
	 * If we start with the scenario where the L1 crng is reseeded then we need
	 * to do fast key erasure on the L1 crng and use its output as the new key
	 * for the L2 crng. This basically syncs the L1 and L2 crngs.
	 */
	if (start_from_l1 && !l1_key_calculated) {
		crng_fast_key_erasure(l1_crng.key, chacha_state,
													l2_crng.key, sizeof(l2_crng.key));
		l1_key_calculated = 1;
	}

	crng_fast_key_erasure(l2_crng.key, chacha_state,
												random_data, random_data_len);
}

static void
get_random_bytes(void *buf, size_t len)
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8 tmp[CHACHA_BLOCK_SIZE];
	size_t first_block_len;

	if (!len)
		return;

	first_block_len = MIN(32, len);
	crng_make_state(chacha_state, buf, first_block_len);
	len -= first_block_len;
	buf += first_block_len;

	while (len) {
		if (len < CHACHA_BLOCK_SIZE) {
			chacha20_block(chacha_state, tmp);
			memcpy(buf, tmp, len);
			memset(tmp, 0, sizeof(tmp));
			break;
		}

		chacha20_block(chacha_state, buf);
		if (chacha_state[12] == 0)
			++chacha_state[13];
		len -= CHACHA_BLOCK_SIZE;
		buf += CHACHA_BLOCK_SIZE;
	}

	memset(chacha_state, 0, sizeof(chacha_state));
}

static void
get_random_bytes_user(void *buf, size_t len)
{
	u32 chacha_state[CHACHA_STATE_WORDS];
	u8  tmp[CHACHA_BLOCK_SIZE];

	crng_make_state(chacha_state, (u8 *)&chacha_state[4], CHACHA_KEY_SIZE);

	if (len <= CHACHA_KEY_SIZE) {
		memcpy(buf, &chacha_state[4], len);
		memset(chacha_state, 0, sizeof(chacha_state));
		return;
	}

	while (len) {
		if (len < CHACHA_BLOCK_SIZE) {
			chacha20_block(chacha_state, tmp);
			memcpy(buf, tmp, len);
			memset(tmp, 0, sizeof(tmp));
			break;
		}

		chacha20_block(chacha_state, buf);
		if (chacha_state[12] == 0)
			++chacha_state[13];
		len -= CHACHA_BLOCK_SIZE;
		buf += CHACHA_BLOCK_SIZE;
	}

	memset(chacha_state, 0, sizeof(chacha_state));
}

static void
usage(const char *prog)
{
	printf("Usage: %s [options]...\n\n", prog);
	printf("Options:\n" \
			   "\t--help\n" \
				 "\t--extractions <num>\n" \
				 "\t--size <num>\n" \
				 "\t--output <file>\n" \
				 "\t--l1\n" \
				 "\t--user\n");
}

static const struct option long_opt[] =
{
	{"help",         no_argument,        NULL,            'h'},
	{"extractions",  required_argument,  NULL,            'x'},
	{"size",         required_argument,  NULL,            's'},
	{"output",       required_argument,  NULL,            'o'},
	{"l1",           no_argument,        &start_from_l1,  1},
	{"user",         no_argument,        &emulate_user,   1},
	{NULL,           0,                  NULL,            0}
};

int
main(int argc, char *argv[])
{
	int opt;                  /* Current option */
	int i;                    /* Iterator */
	u8 *random_data;          /* Extracted random data */
	unsigned int ex_num = 0;  /* Number of extractions to perform */
	size_t ex_size = 0;       /* Size of each extraction */
	int printed = 0;          /* Flag for if the updated L1 key was printed */
	const char *out_path = NULL; /* Optional raw dump of extracted data */
	FILE *out_file = NULL;

	/* Parse arguments */
	if (argc == 1) {
		usage(argv[0]);
		return -1;
	}

	while ((opt = getopt_long(argc, argv, "-:x:s:o:h", long_opt, NULL)) != -1)
	{
		switch (opt)
		{
			case 'x': /* number of extractions */
				ex_num = atoi(optarg);
				break;
			case 's': /* size of extractions */
				ex_size = strtoul(optarg, NULL, 10);
				break;
			case 'o': /* dump extracted data to a file */
				out_path = optarg;
				break;
			case 'h': /* help */
				usage(argv[0]);
				break;
			case '?':
				handle_error_no_en("Unknown option for: %c\n", optopt);
			case ':':
				handle_error_no_en("Missing argument for: %c\n", optopt);
		}
	}

	/* Allocate space for random data */
	random_data = (u8 *)malloc(ex_size);

	if (start_from_l1) {
		verbose(2, "[1/2] Starting L1 ChaCha key:\n");
		verbose_hexdump(2, l1_crng.key, sizeof(l1_crng.key));
	}

	verbose(2, "[0/%d] Starting L2 ChaCha key:\n", ex_num);
	verbose_hexdump(2, l2_crng.key, sizeof(l2_crng.key));

	/* Optional raw dump of everything extracted, for offline analysis. */
	if (out_path) {
		out_file = fopen(out_path, "wb");
		if (!out_file)
			handle_error_no_en("Failed to open %s\n", out_path);
	}

	for (i = 0; i < ex_num; i++) {
		if (emulate_user)
			get_random_bytes_user(random_data, ex_size);
		else
			get_random_bytes(random_data, ex_size);

		if (out_file)
			fwrite(random_data, 1, ex_size, out_file);

		if (start_from_l1 && !printed) {
			verbose(2, "[2/2] L1 Chacha20 key:\n");
			verbose_hexdump(2, l1_crng.key, sizeof(l1_crng.key));
			printed = 1;
		}

		verbose(2, "[%d/%d] L2 Chacha20 key:\n", i+1, ex_num);
		verbose_hexdump(2, l2_crng.key, sizeof(l2_crng.key));

		verbose(1, "[%d/%d] Random data:\n", i+1, ex_num);
		verbose_hexdump(1, random_data, ex_size);
		newline();
		verbose_hexdump_le_array(1, random_data, ex_size);
	}

	if (out_file)
		fclose(out_file);

	/* Free allocated space */
	free(random_data);

	return 0;
}
