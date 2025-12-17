#ifndef SHA512_H
#define SHA512_H

#include <stdint.h>
#include <stddef.h>

#define SHA512_BLOCK_SIZE 128
#define SHA512_DIGEST_SIZE 64

typedef struct {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t buffer[SHA512_BLOCK_SIZE];
} sha512_ctx_t;

// Initialize SHA-512 context
void sha512_init(sha512_ctx_t *ctx);

// Update hash with new data
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len);

// Finalize and produce digest
void sha512_final(sha512_ctx_t *ctx, uint8_t *digest);

// Convenience function: hash data in one call
void sha512_hash(const uint8_t *data, size_t len, uint8_t *digest);

// HMAC-SHA512
void hmac_sha512(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t *hmac);

#endif // SHA512_H