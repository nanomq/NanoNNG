#include <stdint.h>
#include <string.h>

#include <nng/nng.h>

#include <acutest.h>

#include "aes.h"

// Test basic AES GCM encryption and decryption
void
test_aes_gcm(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'a' + i;
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	TEST_CHECK(cipher_len > 64); // Cipher should be longer due to tag
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	for (int i=0; i<64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with wrong key
void
test_aes_gcm_error_key(void)
{
	char *key = "0123456789abcdef";
	char *key2 = "1123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'a' + i;
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key2, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with invalid cipher
void
test_aes_gcm_invalid_cipher(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'a' + i;
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);

	// Cipher too short (less than tag size)
	int   invalid_cipher_len = 16;
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, invalid_cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	// Corrupted cipher
	cipher[33] = 'b';
	plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with empty input
void
test_aes_gcm_empty_input(void)
{
	char *key = "0123456789abcdef";
	char  input[1] = {0};
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 0, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len >= 32); // Should at least have tag
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with large input
void
test_aes_gcm_large_input(void)
{
	char *key = "0123456789abcdef";
	int input_size = 10000;
	char *input = malloc(input_size);
	TEST_CHECK(input != NULL);
	
	for (int i = 0; i < input_size; ++i)
		input[i] = (char)(i % 256);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, input_size, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > input_size);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == input_size);
	
	for (int i = 0; i < input_size; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	
	free(input);
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with 192-bit key
void
test_aes_gcm_192bit_key(void)
{
	char *key = "0123456789abcdef01234567"; // 24 bytes = 192 bits
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'a' + (i % 26);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	
	for (int i=0; i<64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with 256-bit key
void
test_aes_gcm_256bit_key(void)
{
	char *key = "0123456789abcdef0123456789abcdef"; // 32 bytes = 256 bits
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'A' + (i % 26);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	
	for (int i=0; i<64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test AES GCM with unsupported key length
void
test_aes_gcm_invalid_key_length(void)
{
	char *key = "0123456789"; // 10 bytes - invalid
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'a' + i;
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher == NULL); // Should fail with invalid key length
}

// Test AES GCM with binary data
void
test_aes_gcm_binary_data(void)
{
	char *key = "0123456789abcdef";
	char  input[256];
	// Fill with all possible byte values
	for (int i=0; i<256; ++i)
		input[i] = (char)i;
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 256, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 256);
	
	for (int i=0; i<256; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test multiple encrypt/decrypt cycles
void
test_aes_gcm_multiple_cycles(void)
{
	char *key = "0123456789abcdef";
	char  input[32];
	for (int i=0; i<32; ++i)
		input[i] = 'x';
	
	for (int cycle = 0; cycle < 5; cycle++) {
		int   cipher_len;
		char *cipher = nni_aes_gcm_encrypt(input, 32, key, &cipher_len);
		TEST_CHECK(cipher != NULL);
		
		int   plain_len;
		char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
		TEST_CHECK(plain != NULL);
		TEST_CHECK(plain_len == 32);
		
		for (int i=0; i<32; ++i) {
			TEST_CHECK(plain[i] == input[i]);
		}
		
		if (cipher)
			nng_free(cipher, cipher_len);
		if (plain)
			nng_free(plain, plain_len);
	}
}

// Test AES GCM with single byte
void
test_aes_gcm_single_byte(void)
{
	char *key = "0123456789abcdef";
	char  input[1] = {'X'};
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 1, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 1);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 1);
	TEST_CHECK(plain[0] == 'X');
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

// Test that different plaintexts produce different ciphertexts
void
test_aes_gcm_different_plaintexts(void)
{
	char *key = "0123456789abcdef";
	char  input1[32];
	char  input2[32];
	
	memset(input1, 'A', 32);
	memset(input2, 'B', 32);
	
	int   cipher1_len, cipher2_len;
	char *cipher1 = nni_aes_gcm_encrypt(input1, 32, key, &cipher1_len);
	char *cipher2 = nni_aes_gcm_encrypt(input2, 32, key, &cipher2_len);
	
	TEST_CHECK(cipher1 != NULL);
	TEST_CHECK(cipher2 != NULL);
	TEST_CHECK(cipher1_len == cipher2_len);
	
	// Ciphertexts should be different
	int different = 0;
	for (int i = 0; i < cipher1_len; i++) {
		if (cipher1[i] != cipher2[i]) {
			different = 1;
			break;
		}
	}
	TEST_CHECK(different == 1);
	
	if (cipher1)
		nng_free(cipher1, cipher1_len);
	if (cipher2)
		nng_free(cipher2, cipher2_len);
}

TEST_LIST = {
	{ "aes_gcm", test_aes_gcm },
	{ "aes_gcm_error_key", test_aes_gcm_error_key },
	{ "aes_gcm_invalid_cipher", test_aes_gcm_invalid_cipher },
	{ "aes_gcm_empty_input", test_aes_gcm_empty_input },
	{ "aes_gcm_large_input", test_aes_gcm_large_input },
	{ "aes_gcm_192bit_key", test_aes_gcm_192bit_key },
	{ "aes_gcm_256bit_key", test_aes_gcm_256bit_key },
	{ "aes_gcm_invalid_key_length", test_aes_gcm_invalid_key_length },
	{ "aes_gcm_binary_data", test_aes_gcm_binary_data },
	{ "aes_gcm_multiple_cycles", test_aes_gcm_multiple_cycles },
	{ "aes_gcm_single_byte", test_aes_gcm_single_byte },
	{ "aes_gcm_different_plaintexts", test_aes_gcm_different_plaintexts },
	{ NULL, NULL },
};
