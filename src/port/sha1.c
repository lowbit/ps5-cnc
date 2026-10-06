/* SHA-1 (FIPS 180-4), see sha1.h. */
#include "sha1.h"

#include <string.h>

static uint32_t rotate(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80], a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 |
               block[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5a827999;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        uint32_t t = rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotate(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(sha1_t *sha1)
{
    static const uint32_t start[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    memcpy(sha1->state, start, sizeof(start));
    sha1->length = 0;
    sha1->used = 0;
}

void sha1_update(sha1_t *sha1, const void *data, size_t size)
{
    const uint8_t *at = data;
    sha1->length += size;
    if (sha1->used > 0)
    {
        size_t take = 64 - sha1->used < size ? 64 - sha1->used : size;
        memcpy(sha1->block + sha1->used, at, take);
        sha1->used += take;
        at += take;
        size -= take;
        if (sha1->used < 64)
            return;
        transform(sha1->state, sha1->block);
        sha1->used = 0;
    }
    for (; size >= 64; at += 64, size -= 64)
        transform(sha1->state, at);
    memcpy(sha1->block, at, size);
    sha1->used = size;
}

void sha1_final(sha1_t *sha1, uint8_t digest[20])
{
    uint64_t bits = sha1->length * 8;
    uint8_t pad = 0x80, zero = 0, length[8];

    sha1_update(sha1, &pad, 1);
    while (sha1->used != 56)
        sha1_update(sha1, &zero, 1);
    for (int i = 0; i < 8; i++)
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(sha1, length, 8);
    for (int i = 0; i < 20; i++)
        digest[i] = (uint8_t)(sha1->state[i / 4] >> (24 - 8 * (i % 4)));
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int sha1_parse(uint8_t digest[20], const char *hex)
{
    for (int i = 0; i < 20; i++)
    {
        int high = hex_digit(hex[i * 2]), low = high < 0 ? -1 : hex_digit(hex[i * 2 + 1]);
        if (low < 0)
            return -1;
        digest[i] = (uint8_t)(high << 4 | low);
    }
    return hex[40] == '\0' ? 0 : -1;
}
