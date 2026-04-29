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
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);

	int   invalid_cipher_len = 16;
	int   plain_len          = 0;
	char *plain = (char *) nni_aes_gcm_decrypt((uint8_t *) cipher,
	    invalid_cipher_len, (uint8_t *) key, strlen(key), &plain_len,
	    "AES128-GCM");
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	cipher[33] = 'b';
	plain = (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	    (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
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
	char *key  = "0123456789abcdef";
	char *key2 = "1123456789abcdef";
	char  input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len = 0;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key2, strlen(key2), &plain_len, "AES128-GCM");
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
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	for (int i = 0; i < 64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_empty_input(void)
{
	char    *key      = "0123456789abcdef";
	char     input[1] = { 0 };
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 0, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len == 32);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 0);
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_large_input(void)
{
	char *key = "0123456789abcdef";
	char  input[4096];
	for (int i = 0; i < 4096; ++i)
		input[i] = (char) (i % 256);
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 4096, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 4096);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 4096);
	for (int i = 0; i < 4096; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_192bit_key(void)
{
	char *key = "0123456789abcdef01234567";
	char  input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'x' + (i % 3);
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES192-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES192-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	for (int i = 0; i < 64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_256bit_key(void)
{
	char *key = "0123456789abcdef0123456789abcdef";
	char  input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'y' + (i % 3);
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES256-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES256-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 64);
	for (int i = 0; i < 64; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_invalid_key_length(void)
{
	char *key = "short";
	char  input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher == NULL);
}

void
test_aes_gcm_null_key(void)
{
	char input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt(
	    (uint8_t *) input, 64, NULL, 0, &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher == NULL);
}

void
test_aes_gcm_binary_data(void)
{
	char *key = "0123456789abcdef";
	char  input[256];
	for (int i = 0; i < 256; ++i)
		input[i] = (char) i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 256, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 256);
	for (int i = 0; i < 256; ++i) {
		TEST_CHECK(plain[i] == input[i]);
	}
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_single_byte(void)
{
	char    *key      = "0123456789abcdef";
	char     input[1] = { 'X' };
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 1, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len > 1);
	int   plain_len;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain != NULL);
	TEST_CHECK(plain_len == 1);
	TEST_CHECK(plain[0] == 'X');
	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_cipher_len_zero(void)
{
	char *key       = "0123456789abcdef";
	char  dummy[1]  = { 0 };
	int   plain_len = 0;
	char *plain     = (char *) nni_aes_gcm_decrypt((uint8_t *) dummy, 0,
	    (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
}

void
test_aes_gcm_cipher_len_exact_boundary(void)
{
	char *key = "0123456789abcdef";
	char  dummy[32];
	memset(dummy, 0, 32);
	int   plain_len = 0;
	char *plain     = (char *) nni_aes_gcm_decrypt((uint8_t *) dummy, 32,
	    (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);
}

void
test_aes_gcm_corrupted_tag(void)
{
	char *key = "0123456789abcdef";
	char  input[64];
	for (int i = 0; i < 64; ++i)
		input[i] = 'a' + i;
	int      cipher_len;
	uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 64, key,
	    ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
	TEST_CHECK(cipher != NULL);
	TEST_CHECK(cipher_len != 0);

	cipher[0] ^= 0xFF;
	int   plain_len = 0;
	char *plain =
	    (char *) nni_aes_gcm_decrypt((uint8_t *) cipher, cipher_len,
	        (uint8_t *) key, strlen(key), &plain_len, "AES128-GCM");
	TEST_CHECK(plain == NULL);
	TEST_CHECK(plain_len == 0);

	if (cipher)
		nng_free(cipher, cipher_len);
	if (plain)
		nng_free(plain, plain_len);
}

void
test_aes_gcm_multiple_encrypt_decrypt(void)
{
	char *key = "0123456789abcdef";
	char  input[32];
	for (int i = 0; i < 32; ++i)
		input[i] = 'a' + (i % 26);

	for (int iteration = 0; iteration < 5; iteration++) {
		int      cipher_len;
		uint8_t *cipher = nni_aes_gcm_encrypt((uint8_t *) input, 32,
		    key, ((key) ? strlen(key) : 0), &cipher_len, "AES128-GCM");
		TEST_CHECK(cipher != NULL);
		int   plain_len;
		char *plain = (char *) nni_aes_gcm_decrypt((uint8_t *) cipher,
		    cipher_len, (uint8_t *) key, strlen(key), &plain_len,
		    "AES128-GCM");
		TEST_CHECK(plain != NULL);
		TEST_CHECK(plain_len == 32);
		for (int i = 0; i < 32; ++i) {
			TEST_CHECK(plain[i] == input[i]);
		}
		if (cipher)
			nng_free(cipher, cipher_len);
		if (plain)
			nng_free(plain, plain_len);
	}
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
	{ "aes_gcm_null_key", test_aes_gcm_null_key },
	{ "aes_gcm_binary_data", test_aes_gcm_binary_data },
	{ "aes_gcm_single_byte", test_aes_gcm_single_byte },
	{ "aes_gcm_cipher_len_zero", test_aes_gcm_cipher_len_zero },
	{ "aes_gcm_cipher_len_exact_boundary",
	    test_aes_gcm_cipher_len_exact_boundary },
	{ "aes_gcm_corrupted_tag", test_aes_gcm_corrupted_tag },
	{ "aes_gcm_multiple_encrypt_decrypt",
	    test_aes_gcm_multiple_encrypt_decrypt },
	{ NULL, NULL },
};
