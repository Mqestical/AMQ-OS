#ifndef AES256_H
#define AES256_H

#include <stdint.h>
#include <stddef.h>

#define AES256_KEY_SIZE 32
#define AES256_BLOCK_SIZE 16
#define AES256_ROUNDS 14

typedef struct {
    uint32_t round_keys[4 * (AES256_ROUNDS + 1)];
    int nr;  // Number of rounds
} aes256_ctx_t;

// Initialize AES-256 context with key
void aes256_init(aes256_ctx_t *ctx, const uint8_t *key);

// Encrypt single block (16 bytes)
void aes256_encrypt_block(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

// Decrypt single block (16 bytes)
void aes256_decrypt_block(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

// CBC mode encryption
void aes256_cbc_encrypt(aes256_ctx_t *ctx, const uint8_t *iv,
                        const uint8_t *in, uint8_t *out, size_t len);

// CBC mode decryption
void aes256_cbc_decrypt(aes256_ctx_t *ctx, const uint8_t *iv,
                        const uint8_t *in, uint8_t *out, size_t len);

// CTR mode (encrypt/decrypt - same operation)
void aes256_ctr(aes256_ctx_t *ctx, const uint8_t *nonce,
                const uint8_t *in, uint8_t *out, size_t len);

// PKCS7 padding helpers
size_t aes256_pad_pkcs7(uint8_t *data, size_t len, size_t max_len);
size_t aes256_unpad_pkcs7(const uint8_t *data, size_t len);

#endif // AES256_H