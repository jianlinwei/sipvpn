/*
 * encrypt.c - encryption and decryption
 *
 * Copyright (C) 2014 - 2015, Xiaoxiao <i@xiaoxiao.im>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "encrypt.h"
#include "md5.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define SWAP(x, y) do {register uint8_t tmp = (x); (x) = (y); (y) = tmp; } while (0)

// in, out 必须 sizeof(long) 字节对齐
static void rc4(void *out, void *in, size_t len, const void *key, size_t key_len)
{
	register int i, j;
	uint8_t s[256];
	for (i = 0; i < 256; i++)
	{
		s[i] = (uint8_t)i;
	}
	for (i = 0, j = 0; i < 256; i++)
	{
		j = (j + s[i] + ((uint8_t *)key)[i % key_len]) & 255;
		SWAP(s[i], s[j]);
	}

#if defined(__GNUC__) && defined(USE_ASSEMBLY)
#  if defined(__amd64__) || defined(__x86_64__)
#    define RC4_ASM 1
	assert(((long)out & 0x07) == 0);
	assert(((long)in & 0x07) == 0);
	__asm__  (
		// 中间对齐的部分，每次处理 8 字节
		"lea 8(%[in]), %%r8\n\t"
		"cmpq %%r8, %[end]\n\t"
		"jbe 4f\n\t"
		"3:\n\t"
		// i = (i + 1) & 255
		"incl %[i]\n\t"
		"movzbl %b[i], %[i]\n\t"
		// j = (j + s[i] ) & 255
		"movzbl (%[s], %q[i]), %%ecx\n\t"
		"addb %%cl, %b[j]\n\t"
		// SWAP(s[i], s[j])
		// r8 ^= s[(s[i] + s[j]) & 255]
		"movzbl (%[s], %q[j]), %%edx\n\t"
		"movb %%dl, (%[s], %q[i])\n\t"
		"addl %%ecx, %%edx\n\t"
		"movb %%cl, (%[s], %q[j])\n\t"
		"movzbl %%dl, %%edx\n\t"
		"shl $8, %%r8\n\t"
		"movb (%[s], %%rdx), %%r8b\n\t"
		// packet++
		"incq %[in]\n\t"
		"testq $7, %[in]\n\t"
		"jne 3b\n\t"
		"bswap %%r8\n\t"
		"xorq %%r8, -8(%[in])\n\t"
		"lea 8(%[in]), %%r8\n\t"
		"cmpq %%r8, %[end]\n\t"
		"jg 3b\n\t"
		"4:\n\t"

		// 末尾未对齐的部分，每次处理 1 字节
		"cmpq %[in], %[end]\n\t"
		"jbe 6f\n\t"
		"5:\n\t"
		// i = (i + 1) & 255
		"incl %[i]\n\t"
		"movzbl %b[i], %[i]\n\t"
		// j = (j + s[i] ) & 255
		"movzbl (%[s], %q[i]), %%ecx\n\t"
		"addb %%cl, %b[j]\n\t"
		// SWAP(s[i], s[j])
		// *((uint8_t *)in) ^= s[(s[i] + s[j]) & 255]
		"movzbl (%[s], %q[j]), %%edx\n\t"
		"movb %%dl, (%[s], %q[i])\n\t"
		"addl %%ecx, %%edx\n\t"
		"movb %%cl, (%[s], %q[j])\n\t"
		"movzbl %%dl, %%edx\n\t"
		"movb (%[s], %%rdx), %%cl\n\t"
		"xorb %%cl, (%[in])\n\t"
		// packet++
		"incq %[in]\n\t"
		"cmpq %[in], %[end]\n\t"
		"jg 5b\n\t"
		"6:\n\t"
		:
		: [in] "r"(in),
		  [out] "r"(out),
		  [end] "g"(in+ len),
		  [s] "r"(s),
		  [i] "a"(0),
		  [j] "b"(0)
		: "memory", "rcx", "rdx", "r8"
	);
#  elif defined(__i386__)
#    define RC4_ASM 1
	assert(((long)out & 0x03) == 0);
	assert(((long)in & 0x03) == 0);
	__asm__ __volatile__ (
		"cmpl %[packet], %[end]\n\t"
		"je 2f\n\t"
		"1:\n\t"
		/* i = (i + 1) & 255; */
		"incl %[i]\n\t"
		"movzbl %b[i], %[i]\n\t"
		/* j = (j + s[i] ) & 255 */
		"movzbl (%[s], %[i]), %%ecx\n\t"
		"addb %%cl, %b[j]\n\t"
		/* SWAP(s[i], s[j]); */
		/* *((uint8_t *)packet) ^= s[(s[i] + s[j]) & 255]; */
		"movzbl (%[s], %[j]), %%edx\n\t"
		"movb %%dl, (%[s], %[i])\n\t"
		"addl %%ecx, %%edx\n\t"
		"movb %%cl, (%[s], %[j])\n\t"
		"movzbl %%dl, %%edx\n\t"
		"movb (%[s], %%edx), %%cl\n\t"
		"xorb %%cl, (%[packet])\n\t"
		/* packet++ */
		"incl %[packet]\n\t"
		"cmpl %[packet], %[end]\n\t"
		"jne 1b\n\t"
		"2:\n\t"
		:
		: [in] "r"(in),
		  [out] "r"(out),
		  [end] "g"(in+ len),
		  [s] "r"(s),
		  [i] "a"(0),
		  [j] "b"(0)
		: "memory", "ecx", "edx"
	);
#  elif defined(__arm__)
#    define RC4_ASM 1
	assert(((long)out & 0x03) == 0);
	assert(((long)in & 0x03) == 0);
	__asm__ __volatile__ (
		"cmp %[in], %[end]\n\t"
		"bcs 2f\n\t"
		"1:\n\t"
		/* i = (i + 1) & 255; */
		"add %[i], %[i], #1\n\t"
		"and %[i], %[i], #255\n\t"
		/* j = (j + s[i] ) & 255 */
		"ldrb r4, [%[s], %[i]]\n\t"
		"add %[j], %[j], r4\n\t"
		"and %[j], %[j], #255\n\t"
		/* SWAP(s[i], s[j]); */
		/* *((uint8_t *)packet) ^= s[(s[i] + s[j]) & 255]; */
		"ldrb r5, [%[s], %[j]]\n\t"
		"strb r5, [%[s], %[i]]\n\t"
		"ldrb r6, [%[packet]]\n\t"
		"add r5, r5, r4\n\t"
		"strb r4, [%[s], %[j]]\n\t"
		"and r5, r5, #255\n\t"
		"ldrb r7, [%[s], r5]\n\t"
		"eor r6, r6, r7\n\t"
		"strb r6, [%[packet]], #1\n\t"
		"cmp %[packet], %[end]\n\t"
		"bne 1b\n\t"
		"2:\n\t"
		:
		: [in] "r"(in),
		  [out] "r"(out),
		  [end] "r"(in+ len),
		  [s] "r"(s),
		  [i] "r"(0),
		  [j] "r"(0)
		: "memory", "r4", "r5", "r6", "r7"
	);
#  endif
#endif

#ifndef RC4_ASM
	i = 0;
	j = 0;
	register uint8_t *end = (uint8_t *)in + len;
	for (; (uint8_t *)in < end; in++, out++)
	{
		i = (i + 1) & 255;
		j = (j + s[i]) & 255;
		SWAP(s[i], s[j]);
		*((uint8_t *)out) = *((uint8_t *)in) ^ s[(s[i] + s[j]) & 255];
	}
#endif
}

// IV = md5(in)
// out = IV + rc4(in , md5(IV + key))
void encrypt(void *out, void *in, size_t len, const void *key, size_t key_len)
{
	char real_key[16];
	md5(out, in, len);
	memcpy(out + 16, key, key_len);
	md5(real_key, out, 16 + key_len);
	rc4(out + 16, in, len, real_key, 16);
}

int decrypt(void *out, void *in, size_t len, const void *key, size_t key_len)
{
	char real_key[16];
	memcpy(out, in, 16);
	memcpy(out + 16, key, key_len);
	md5(real_key, out, 16 + key_len);
	rc4(out, in + 16, len - 16, real_key, 16);
	md5(real_key, out, len - 16);
	if (memcmp(real_key, in, 16) == 0)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}
