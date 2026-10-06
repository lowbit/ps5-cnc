/* SHA-1, for telling the game files apart by content (the importer's table of known files). */
#ifndef PS5_SHA1_H
#define PS5_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t state[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} sha1_t;

void sha1_init(sha1_t *sha1);
void sha1_update(sha1_t *sha1, const void *data, size_t size);
void sha1_final(sha1_t *sha1, uint8_t digest[20]);

/* Reads 40 hex digits into digest; 0 on success. */
int sha1_parse(uint8_t digest[20], const char *hex);

#endif
