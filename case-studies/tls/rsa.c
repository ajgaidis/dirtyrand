#include <stdio.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include "libprimitive.h"

/* Key parameters */
#define KEY_BITS 4096
#define PUBLIC_EXPONENT 65537

/* Helper function that prints OpenSSL errors */
void handle_errors(void)
{
    fprintf(stderr, "[!] An OpenSSL error occurred:\n");
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

int main(void)
{
    EVP_PKEY_CTX *ctx = NULL; /* Context for the key generation operation */
    EVP_PKEY *pkey = NULL; /* Generic container for a private key */
    BIGNUM *bn = NULL; /* BIGNUM for the public exponent */
    FILE *private_key_file = NULL; /* Private key file stream */
    FILE *public_key_file = NULL; /* Public key file stream */

    /* Zap RNG  */
    trigger_overwrite_of_l1_key();

    printf("[o] Generating RSA-%d key pair...\n", KEY_BITS);

    /*
     * Create the key generation context for RSA.
     * EVP_PKEY_RSA is the algorithm identifier.
     */
    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        fprintf(stderr, "[!] Could not create EVP_PKEY_CTX.\n");
        handle_errors();
    }

    /* Initialize the key generation operation */
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        fprintf(stderr, "[!] Could not initialize keygen context.\n");
        handle_errors();
    }

    /* Set key generation parameters. Set the RSA key bit length. */
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, KEY_BITS) <= 0) {
        fprintf(stderr, "[!] Could not set RSA keygen bits.\n");
        handle_errors();
    }

    /* Set the public exponent */
    bn = BN_new();
    if (!bn || !BN_set_word(bn, PUBLIC_EXPONENT)) {
        fprintf(stderr, "[!] Could not create BIGNUM for public exponent.\n");
        handle_errors();
    }

    if (EVP_PKEY_CTX_set1_rsa_keygen_pubexp(ctx, bn) <= 0) {
        fprintf(stderr, "[!] Could not set RSA public exponent.\n");
        handle_errors();
    }
    
    /* The BIGNUM is copied into the context, so we can free our copy */
    BN_free(bn);
    bn = NULL;


    /* Generate the key pair; generated key is stored in the pkey object. */
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        fprintf(stderr, "[!] Key generation failed.\n");
        handle_errors();
    }

    /* Verbose */
    printf("[+] Key generation successful.\n");

    /* Save the private key in PEM format */
    private_key_file = fopen("private.pem", "wb");
    if (!private_key_file) {
        perror("[!] Error opening private.pem for writing");
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        return EXIT_FAILURE;
    }

    if (PEM_write_PrivateKey(private_key_file, pkey,
                              NULL, NULL, 0, NULL, NULL) <= 0) {
        fprintf(stderr, "[!] Could not write private key to private.pem.\n");
        handle_errors();
    }
    fclose(private_key_file);
    printf("[+] Private key saved to private.pem\n");

    /* Save the public key in PEM format */
    public_key_file = fopen("public.pem", "wb");
    if (!public_key_file) {
        perror("[!] Error opening public.pem for writing");
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        return EXIT_FAILURE;
    }

    /* Use PEM_write_PUBKEY for the standard SubjectPublicKeyInfo format */
    if (PEM_write_PUBKEY(public_key_file, pkey) <= 0) {
        fprintf(stderr, "[!] Could not write public key to public.pem.\n");
        handle_errors();
    }
    fclose(public_key_file);
    printf("[+] Public key saved to public.pem\n");

    /* Cleanup */
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    ERR_free_strings();

    return EXIT_SUCCESS;
}
