#include <stdint.h>
#include <string.h>

#include <nng/nng.h>

#include <acutest.h>

#include "aes.h"

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

	int   invalid_cipher_len = 16;
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, invalid_cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	cipher[33] = 'b';
	plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

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

TEST_LIST = {
	{ "aes_gcm", test_aes_gcm },
	{ "aes_gcm_error_key", test_aes_gcm_error_key },
	{ "aes_gcm_invalid_cipher ", test_aes_gcm_invalid_cipher },
	{ NULL, NULL },
};

// ============================================================================
// Additional Comprehensive AES GCM Tests
// ============================================================================

void
test_aes_gcm_roundtrip_128bit(void)
{
	char *key = "0123456789abcdef"; // 16 bytes = 128 bit
	char  input[128];
	for (int i=0; i<128; ++i)
		input[i] = (char)(i % 256);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 128, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 128); // Should be larger due to tag
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 128);
	
	for (int i=0; i<128; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_roundtrip_192bit(void)
{
	char *key = "0123456789abcdef01234567"; // 24 bytes = 192 bit
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
	
	TEST_CHECK(memcmp(input, plain, 64) == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_roundtrip_256bit(void)
{
	char *key = "0123456789abcdef0123456789abcdef"; // 32 bytes = 256 bit
	char  input[256];
	for (int i=0; i<256; ++i)
		input[i] = (char)(255 - i);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 256, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 256);
	
	TEST_CHECK(memcmp(input, plain, 256) == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_empty_input(void)
{
	char *key = "0123456789abcdef";
	char  input[1] = {0};
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 0, key, &cipher_len);
	// Should handle empty input - may return NULL or minimal cipher
	
	if (cipher) {
		int   plain_len;
		char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
		if (plain) {
			TEST_CHECK(plain_len == 0);
			nng_free(plain, plain_len);
		}
		nng_free(cipher, cipher_len);
	}
}

void
test_aes_gcm_single_byte(void)
{
	char *key = "0123456789abcdef";
	char  input[1] = {'X'};
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 1, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 0);
	
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

void
test_aes_gcm_large_input(void)
{
	char *key = "0123456789abcdef";
	int   input_size = 16384; // 16KB
	char *input = malloc(input_size);
	TEST_CHECK(input != NULL);
	
	for (int i=0; i<input_size; ++i)
		input[i] = (char)(i % 256);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, input_size, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > input_size);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == input_size);
	TEST_CHECK(memcmp(input, plain, input_size) == 0);
	
	free(input);
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_all_zeros(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	memset(input, 0, 64);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	TEST_CHECK(memcmp(input, plain, 64) == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_all_ones(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	memset(input, 0xFF, 64);
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	
	int   plain_len;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	TEST_CHECK(memcmp(input, plain, 64) == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_decrypt_truncated_cipher(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'T';
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 32);
	
	// Try to decrypt truncated ciphertext (should fail)
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, 10, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
}

void
test_aes_gcm_decrypt_modified_cipher(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'M';
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	
	// Modify the ciphertext (not the tag)
	if (cipher_len > 40) {
		cipher[40] ^= 0x01;
	}
	
	// Decryption should fail due to authentication error
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
}

void
test_aes_gcm_decrypt_modified_tag(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'T';
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, key, &cipher_len);
	TEST_CHECK(cipher != NULL);
	
	// Modify the tag (first 16 bytes)
	if (cipher_len > 16) {
		cipher[8] ^= 0xFF;
	}
	
	// Decryption should fail
	int   plain_len = 0;
	char *plain = nni_aes_gcm_decrypt(cipher, cipher_len, key, &plain_len);
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
	
	if (cipher)
		nng_free(cipher, cipher_len);
}

void
test_aes_gcm_same_plaintext_different_keys(void)
{
	char *key1 = "0123456789abcdef";
	char *key2 = "fedcba9876543210";
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'S';
	
	int   cipher_len1, cipher_len2;
	char *cipher1 = nni_aes_gcm_encrypt(input, 64, key1, &cipher_len1);
	char *cipher2 = nni_aes_gcm_encrypt(input, 64, key2, &cipher_len2);
	
	TEST_CHECK(cipher1 != NULL);
	TEST_CHECK(cipher2 != NULL);
	TEST_CHECK(cipher_len1 == cipher_len2);
	
	// Ciphertexts should be different (high probability)
	int different = 0;
	for (int i=0; i<cipher_len1 && i<cipher_len2; ++i) {
		if (cipher1[i] != cipher2[i]) {
			different = 1;
			break;
		}
	}
	TEST_CHECK(different == 1);
	
	if (cipher1)
		nng_free(cipher1, cipher_len1);
	if (cipher2)
		nng_free(cipher2, cipher_len2);
}

void
test_aes_gcm_invalid_key_length(void)
{
	char *short_key = "short"; // Invalid key length
	char  input[64];
	for (int i=0; i<64; ++i)
		input[i] = 'X';
	
	int   cipher_len;
	char *cipher = nni_aes_gcm_encrypt(input, 64, short_key, &cipher_len);
	TEST_CHECK(cipher == NULL); // Should fail with invalid key length
}

void
test_aes_gcm_multiple_encryptions_same_key(void)
{
	char *key = "0123456789abcdef";
	char  input1[32] = "First message to encrypt!!!";
	char  input2[32] = "Second message for encrypt!!";
	
	int   cipher_len1, cipher_len2;
	char *cipher1 = nni_aes_gcm_encrypt(input1, 32, key, &cipher_len1);
	char *cipher2 = nni_aes_gcm_encrypt(input2, 32, key, &cipher_len2);
	
	TEST_CHECK(cipher1 != NULL);
	TEST_CHECK(cipher2 != NULL);
	
	int   plain_len1, plain_len2;
	char *plain1 = nni_aes_gcm_decrypt(cipher1, cipher_len1, key, &plain_len1);
	char *plain2 = nni_aes_gcm_decrypt(cipher2, cipher_len2, key, &plain_len2);
	
	TEST_CHECK(plain1 != NULL);
	TEST_CHECK(plain2 != NULL);
	TEST_CHECK(plain_len1 == 32);
	TEST_CHECK(plain_len2 == 32);
	TEST_CHECK(memcmp(input1, plain1, 32) == 0);
	TEST_CHECK(memcmp(input2, plain2, 32) == 0);
	
	if (cipher1)
		nng_free(cipher1, cipher_len1);
	if (cipher2)
		nng_free(cipher2, cipher_len2);
	if (plain1)
		nng_free(plain1, plain_len1);
	if (plain2)
		nng_free(plain2, plain_len2);
}

// Update the TEST_LIST to include new tests
#undef TEST_LIST
TEST_LIST = {
	{ "aes_gcm", test_aes_gcm },
	{ "aes_gcm_error_key", test_aes_gcm_error_key },
	{ "aes_gcm_invalid_cipher", test_aes_gcm_invalid_cipher },
	{ "aes_gcm_roundtrip_128bit", test_aes_gcm_roundtrip_128bit },
	{ "aes_gcm_roundtrip_192bit", test_aes_gcm_roundtrip_192bit },
	{ "aes_gcm_roundtrip_256bit", test_aes_gcm_roundtrip_256bit },
	{ "aes_gcm_empty_input", test_aes_gcm_empty_input },
	{ "aes_gcm_single_byte", test_aes_gcm_single_byte },
	{ "aes_gcm_large_input", test_aes_gcm_large_input },
	{ "aes_gcm_all_zeros", test_aes_gcm_all_zeros },
	{ "aes_gcm_all_ones", test_aes_gcm_all_ones },
	{ "aes_gcm_decrypt_truncated_cipher", test_aes_gcm_decrypt_truncated_cipher },
	{ "aes_gcm_decrypt_modified_cipher", test_aes_gcm_decrypt_modified_cipher },
	{ "aes_gcm_decrypt_modified_tag", test_aes_gcm_decrypt_modified_tag },
	{ "aes_gcm_same_plaintext_different_keys", test_aes_gcm_same_plaintext_different_keys },
	{ "aes_gcm_invalid_key_length", test_aes_gcm_invalid_key_length },
	{ "aes_gcm_multiple_encryptions_same_key", test_aes_gcm_multiple_encryptions_same_key },
	{ NULL, NULL },
};
