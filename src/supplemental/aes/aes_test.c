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
