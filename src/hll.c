#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

#include "../deps/MurmurHash3/MurmurHash3.h"
#include "hll.h"

int hll_init(struct HLL *hll, uint8_t bits) {
	if(bits < 4 || bits > 20) {
		errno = ERANGE;
		return -1;
	}

	hll->bits = bits;
	hll->size = (size_t)1 << bits;
	hll->registers = calloc(hll->size, 1);

	return 0;
}

void hll_destroy(struct HLL *hll) {
	free(hll->registers);

	hll->registers = NULL;
}

static __inline uint8_t _hll_rank(uint32_t hash, uint8_t bits) {
	uint8_t i;

	for(i = 1; i <= 32 - bits; i++) {
		if(hash & 1)
			break;

		hash >>= 1;
	}

	return i;
}

static __inline void _hll_add_hash(struct HLL *hll, uint32_t hash) {
	uint32_t index = hash >> (32 - hll->bits);
	uint8_t rank = _hll_rank(hash, hll->bits);

	if(rank > hll->registers[index]) {
		hll->registers[index] = rank;
	}
}

static __inline uint32_t _hash(const void *buf, size_t size) {
	return MurmurHash3_x86_32((const char *)buf, (uint32_t)size, 0x5f61767a);
}

void hll_add(struct HLL *hll, const void *buf, size_t size) {
	uint32_t hash = _hash(buf, size);

	_hll_add_hash(hll, hash);
}

double hll_count(const struct HLL *hll) {
	double alpha_mm;
	uint32_t i;

	switch (hll->bits) {
		case 4:
			alpha_mm = 0.673;
		break;
		case 5:
			alpha_mm = 0.697;
		break;
		case 6:
			alpha_mm = 0.709;
		break;
		default:
			alpha_mm = 0.7213 / (1.0 + 1.079 / (double)hll->size);
		break;
	}

	alpha_mm *= ((double)hll->size * (double)hll->size);

	double sum = 0;
	for(i = 0; i < hll->size; i++) {
		sum += 1.0 / (1 << hll->registers[i]);
	}

	double estimate = alpha_mm / sum;

	if (estimate <= 5.0 / 2.0 * (double)hll->size) {
		int zeros = 0;

		for(i = 0; i < hll->size; i++)
			zeros += (hll->registers[i] == 0);

		if(zeros)
			estimate = (double)hll->size * log((double)hll->size / zeros);

	} else if (estimate > (1.0 / 30.0) * 4294967296.0) {
		estimate = -4294967296.0 * log(1.0 - (estimate / 4294967296.0));
	}

	return estimate;
}

int hll_merge(struct HLL *dst, const struct HLL *src) {
	uint32_t i;

	if(dst->bits != src->bits) {
		errno = EINVAL;
		return -1;
	}

	for(i = 0; i < dst->size; i++) {
		if(src->registers[i] > dst->registers[i])
			dst->registers[i] = src->registers[i];
	}

	return 0;
}

int hll_load(struct HLL *hll, const void *registers, size_t size) {
	uint8_t bits = 0;
	size_t s = size;

	while(s) {
		if(s & 1)
			break;

		bits++;

		s >>= 1;
	}

	if(!bits || (1 << bits) != size) {
		errno = EINVAL;
		return -1;
	}

	if(hll_init(hll, bits) == -1)
		return -1;

	memcpy(hll->registers, registers, size);

	return 0;
}

uint32_t _hll_hash(const struct HLL *hll) {
	return _hash(hll->registers, hll->size);
}

/**
 * Number of bits per register
 * @param bits
 * @return
 */
static __inline uint8_t _hll_registerSize(uint8_t bits) {
	if(bits >= 4 && bits <= 16)
		return 4;

	if(bits >= 17 && bits <= 20)
		return 5;

	assert(bits >= 4 && bits <= 20);
}

/**
 * Calculate offset of first bit of specified register
 * @param bits
 * @param
 * @return
 */
static __inline size_t _hll_offset(uint8_t bits, size_t reg) {
	uint8_t regSize = _hll_registerSize(bits);

	return reg * regSize;
}

/**
 * Get buffer size for N = 2^bits registers
 * @param bits
 * @return buffer size in bytes
 */
static __inline size_t _hll_size(uint8_t bits) {
	size_t bitsCount = _hll_offset(bits, (size_t)1 << bits);

	if(bitsCount % 8)
		return bitsCount / 8 + 1;

	return bitsCount / 8;
}
