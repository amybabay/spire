#ifndef KEY_GENERATION_H
#define KEY_GENERATION_H

#include <stddef.h>
#include <openssl/evp.h>
#include "parser.h"

#define AES_KEYLEN 32 // AES-256
#define AES_IVLEN 12  // IV size for AES-GCM
#define AES_TAGLEN 16 // AES-GCM tag size
#define AES_BLOCK_SIZE 16

typedef struct
{
    unsigned char *signature;
    size_t length;
} Signature;

struct HybridEncrypted
{
    char *ciphertext_hex;
    char *enc_key_hex;
};

struct HybridDecryptionResult
{
    unsigned char *plaintext;
    size_t length;
};

/* Hybrid Encryption */
struct HybridEncrypted hybrid_encrypt(const unsigned char *data, size_t data_len, EVP_PKEY *rsa_pubkey);
struct HybridDecryptionResult hybrid_decrypt(const char *ciphertext_hex, const char *enc_key_hex, EVP_PKEY *rsa_privkey);
char *hybrid_pack(const struct HybridEncrypted *enc);
void hybrid_unpack(const char *packed, char **out_enc_key_hex, char **out_ciphertext_hex);

/* Hex Encoding */
char *hex_encode(const unsigned char *data, size_t len);
unsigned char *hex_decode(const char *hexstr, size_t *out_len);

/* Key Generation */
EVP_PKEY *generate_rsa_key(int bits);
char *generate_and_get_public_key(int bits);
char *generate_and_get_encrypted_private_key(int bits, const char *passphrase);

/* PEM Export */
char *get_public_key(EVP_PKEY *pkey);
char *get_private_key(EVP_PKEY *pkey);
char *get_encrypted_private_key(EVP_PKEY *pkey, const char *passphrase);
void free_rsa_key(EVP_PKEY *pkey);

/* File IO */
int write_key_to_file(const char *filename, const char *key);
unsigned char *read_file(const char *filename, size_t *length);

/* Signing and Verifying */
Signature sign_buffer(const unsigned char *data, size_t data_len, EVP_PKEY *priv_key);
int verify_buffer(const unsigned char *data, size_t data_len,
                  const unsigned char *signature, size_t sig_len,
                  EVP_PKEY *pub_key);
void free_signature(Signature *sig);

/* Symmetric Encryption */
unsigned char *generate_symmetric_key();
unsigned char *aes_encrypt_ecb(const unsigned char *plaintext, size_t plaintext_len,
                               const unsigned char *key, size_t *out_len);
unsigned char *aes_decrypt_ecb(const unsigned char *ciphertext, size_t ciphertext_len,
                               const unsigned char *key, size_t *out_len);

/* RSA Encryption/Decryption */
unsigned char *rsa_encrypt(const unsigned char *data, size_t data_len, EVP_PKEY *pubkey, size_t *out_len);
unsigned char *rsa_decrypt(const unsigned char *ciphertext, size_t ct_len,
                           EVP_PKEY *private_key, size_t *out_len);

void decrypt_all_private_keys(struct config *cfg);

/* PEM Key Loading */
EVP_PKEY *load_public_key_from_pem(const char *pem_str);
EVP_PKEY *load_key_from_file(const char *filepath, int is_private);


#endif // KEY_GENERATION_H