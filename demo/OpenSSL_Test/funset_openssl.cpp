﻿#include "funset.hpp"
#include <string.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <openssl/des.h>
#include <openssl/rc4.h>
#include <openssl/md5.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <b64/b64.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

//////////////////////// AES GCM ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/106113185
namespace {

static const unsigned char gcm_key[] = { // 32 bytes, Key
	0xee, 0xbc, 0x1f, 0x57, 0x48, 0x7f, 0x51, 0x92, 0x1c, 0x04, 0x65, 0x66,
	0x5f, 0x8a, 0xe6, 0xd1, 0x65, 0x8b, 0xb2, 0x6d, 0xe6, 0xf8, 0xa0, 0x69,
	0xa3, 0x52, 0x02, 0x93, 0xa5, 0x72, 0x07, 0x8f
};

static const unsigned char gcm_iv[] = { // 12 bytes, IV(Initialisation Vector)
	0x99, 0xaa, 0x3e, 0x68, 0xed, 0x81, 0x73, 0xa0, 0xee, 0xd0, 0x66, 0x84
};

// Additional Authenticated Data(AAD): it is not encrypted, and is typically passed to the recipient in plaintext along with the ciphertext
static const unsigned char gcm_aad[] = { // 16 bytes
	0x4d, 0x23, 0xc3, 0xce, 0xc3, 0x34, 0xb4, 0x9b, 0xdb, 0x37, 0x0c, 0x43,
	0x7f, 0xec, 0x78, 0xde
};

std::unique_ptr<unsigned char[]> aes_gcm_encrypt(const char* plaintext, int& length, unsigned char* tag)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	// Set cipher type and mode
	EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
	// Set IV length if default 96 bits is not appropriate
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, sizeof(gcm_iv), nullptr);
	// Initialise key and IV
	EVP_EncryptInit_ex(ctx, nullptr, nullptr, gcm_key, gcm_iv);
	// Zero or more calls to specify any AAD
	int outlen;
	EVP_EncryptUpdate(ctx, nullptr, &outlen, gcm_aad, sizeof(gcm_aad));
	unsigned char outbuf[1024];
	// Encrypt plaintext
	EVP_EncryptUpdate(ctx, outbuf, &outlen, (const unsigned char*)plaintext, strlen(plaintext));
	length = outlen;
	std::unique_ptr<unsigned char[]> ciphertext(new unsigned char[length]);
	memcpy(ciphertext.get(), outbuf, length);
	// Finalise: note get no output for GCM
	EVP_EncryptFinal_ex(ctx, outbuf, &outlen);
	// Get tag
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, outbuf);
	memcpy(tag, outbuf, 16);
	// Clean up
	EVP_CIPHER_CTX_free(ctx);
	return ciphertext;
}

std::unique_ptr<unsigned char[]> aes_gcm_decrypt(const unsigned char* ciphertext, int& length, const unsigned char* tag)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	// Select cipher
	EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
	// Set IV length, omit for 96 bits
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, sizeof(gcm_iv), nullptr);
	// Specify key and IV
	EVP_DecryptInit_ex(ctx, nullptr, nullptr, gcm_key, gcm_iv);
	int outlen;
	// Zero or more calls to specify any AAD
	EVP_DecryptUpdate(ctx, nullptr, &outlen, gcm_aad, sizeof(gcm_aad));
	unsigned char outbuf[1024];
	// Decrypt plaintext
	EVP_DecryptUpdate(ctx, outbuf, &outlen, ciphertext, length);
	// Output decrypted block
	length = outlen;
	std::unique_ptr<unsigned char[]> plaintext(new unsigned char[length]);
	memcpy(plaintext.get(), outbuf, length);
	// Set expected tag value
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, (void*)tag);
	// Finalise: note get no output for GCM
	int rv = EVP_DecryptFinal_ex(ctx, outbuf, &outlen);
	// Print out return value. If this is not successful authentication failed and plaintext is not trustworthy.
	fprintf(stdout, "Tag Verify %s\n", rv > 0 ? "Successful!" : "Failed!");
	EVP_CIPHER_CTX_free(ctx);
	return plaintext;
}

} // namespace

int test_openssl_aes_gcm()
{
	/* reference:
		https://github.com/openssl/openssl/blob/master/demos/evp/aesgcm.c
		https://wiki.openssl.org/index.php/EVP_Authenticated_Encryption_and_Decryption
	*/
	fprintf(stdout, "Start AES GCM 256 Encrypt:\n");
	const char* plaintext = "1234567890ABCDEFG!@#$%^&*()_+[]{};':,.<>/?|";
	fprintf(stdout, "src plaintext: %s, length: %d\n", plaintext, strlen(plaintext));
	int length = 0;
	std::unique_ptr<unsigned char[]> tag(new unsigned char[16]);
	std::unique_ptr<unsigned char[]> ciphertext = aes_gcm_encrypt(plaintext, length, tag.get());
	fprintf(stdout, "length: %d, ciphertext: ", length);
	for (int i = 0; i < length; ++i)
		fprintf(stdout, "%02x ", ciphertext.get()[i]);
	fprintf(stdout, "\nTag: ");
	for (int i = 0; i < 16; ++i)
		fprintf(stdout, "%02x ", tag.get()[i]);
	fprintf(stdout, "\n");

	fprintf(stdout, "\nStart AES GCM 256 Decrypt:\n");
	std::unique_ptr<unsigned char[]> result = aes_gcm_decrypt(ciphertext.get(), length, tag.get());
	fprintf(stdout, "length: %d, decrypted plaintext: ", length);
	for (int i = 0; i < length; ++i)
		fprintf(stdout, "%c", result.get()[i]);
	fprintf(stdout, "\n");

	if (strncmp(plaintext, (const char*)result.get(), length) == 0) {
		fprintf(stdout, "decrypt success\n");
		return 0;
	} else {
		fprintf(stderr, "decrypt fail\n");
		return -1;
	}
}

//////////////////////////// HMAC ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/100176887

int test_openssl_hmac()
{
	HMAC_CTX* ctx = HMAC_CTX_new();
	HMAC_CTX_reset(ctx);
	
	const EVP_MD* engine = EVP_sha256(); // it can also be: EVP_md5(), EVP_sha1, etc
	const char* key = "https://github.com/fengbingchun";
	const char* data = "https://blog.csdn.net/fengbingchun";
	std::unique_ptr<unsigned char[]> output(new unsigned char[EVP_MAX_MD_SIZE]);
	unsigned int output_length;

	HMAC_Init_ex(ctx, key, strlen(key), engine, nullptr);
	HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(data), strlen(data));

	HMAC_Final(ctx, output.get(), &output_length);
	HMAC_CTX_free(ctx);

	fprintf(stdout, "output length: %d\noutput result:", output_length);
	std::for_each(output.get(), output.get() + output_length, [](unsigned char v) { fprintf(stdout, "%02X", v); });
	fprintf(stdout, "\n");

	return 0;
}


//////////////////////////// AES ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/100139524

namespace {

const unsigned char aes_key[] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

} // namespace

int test_openssl_aes()
{
	const char* cleartext = "中国北京12345$abcde%ABCDE！！！!";
	fprintf(stdout, "cleartext length: %d, contents: %s\n", strlen(cleartext), cleartext);

	const int key_bits = sizeof(aes_key) / sizeof(aes_key[0]) * 8;

	// encrypt
	AES_KEY enc_key;
	int ret = AES_set_encrypt_key(aes_key, key_bits, &enc_key); 
	if (ret != 0) return ret;

	char* cleartext_encode = b64_encode(reinterpret_cast<const unsigned char*>(cleartext), strlen(cleartext));
	std::shared_ptr<char> ptr1;
	ptr1.reset(cleartext_encode, [](char* p) { free(p); });
	int encoded_length = (strlen(ptr1.get()) + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE;
	std::unique_ptr<unsigned char[]> cleartext_encode2(new unsigned char[encoded_length]);
	memset(cleartext_encode2.get(), 0, encoded_length);
	memcpy(cleartext_encode2.get(), ptr1.get(), strlen(ptr1.get()));

	std::unique_ptr<unsigned char[]> cleartext_encrypt(new unsigned char[encoded_length]);
	memset(cleartext_encrypt.get(), 0, encoded_length);
	int count = 1;
	while (encoded_length + AES_BLOCK_SIZE - 1 >= AES_BLOCK_SIZE * count) {
		const unsigned char* p1 = cleartext_encode2.get() + AES_BLOCK_SIZE * (count - 1);
		unsigned char* p2 = cleartext_encrypt.get() + AES_BLOCK_SIZE * (count - 1);
		AES_encrypt(p1, p2, &enc_key);
		++count;
	}

	fprintf(stdout, "cleartext encrypt: ");
	std::for_each(cleartext_encrypt.get(), cleartext_encrypt.get() + encoded_length, [](unsigned char v) { fprintf(stdout, "%02X", v); });
	fprintf(stdout, "\n");

	// decrypt
	AES_KEY dec_key;
	ret = AES_set_decrypt_key(aes_key, key_bits, &dec_key);
	if (ret != 0) return ret;

	std::unique_ptr<unsigned char[]> ciphertext_decrypt(new unsigned char[encoded_length]);
	memset(ciphertext_decrypt.get(), 0, encoded_length);
	count = 1;
	while (encoded_length + AES_BLOCK_SIZE - 1 >= AES_BLOCK_SIZE * count) {
		const unsigned char* p1 = cleartext_encrypt.get() + AES_BLOCK_SIZE * (count - 1);
		unsigned char* p2 = ciphertext_decrypt.get() + AES_BLOCK_SIZE * (count - 1);
		AES_decrypt(p1, p2, &dec_key);
		++count;
	}

	fprintf(stdout, "ciphertext decrypt: ");
	std::for_each(ciphertext_decrypt.get(), ciphertext_decrypt.get() + encoded_length, [](unsigned char v) { fprintf(stdout, "%02X", v); });
	fprintf(stdout, "\n");

	unsigned char* decrypt_decode = b64_decode(reinterpret_cast<const char*>(ciphertext_decrypt.get()), encoded_length);
	std::shared_ptr<unsigned char> ptr2;
	ptr2.reset(decrypt_decode, [](unsigned char* p) { free(p); });
	fprintf(stdout, "decrypt result: %s\n", ptr2.get());

	if (strcmp(cleartext, reinterpret_cast<char*>(ptr2.get())) != 0) {
		fprintf(stderr, "aes decrypt fail\n");
		return -1;
	}

	return 0;
}

//////////////////////////// DES ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/42611875
namespace {
unsigned char cbc_iv[] = { '0', '1', 'A', 'B', 'a', 'b', '9', '8' };

std::string des_encrypt(const std::string& cleartext, const std::string& key, CRYPTO_MODE mode)
{
	std::string strCipherText;

	switch (mode) {
	case GENERAL:
	case ECB: {
		DES_cblock keyEncrypt;
		memset(keyEncrypt, 0, 8);

		if (key.length() <= 8)
			memcpy(keyEncrypt, key.c_str(), key.length());
		else
			memcpy(keyEncrypt, key.c_str(), 8);

		DES_key_schedule keySchedule;
		DES_set_key_unchecked(&keyEncrypt, &keySchedule);

		const_DES_cblock inputText;
		DES_cblock outputText;
		std::vector<unsigned char> vecCiphertext;
		unsigned char tmp[8];

		for (int i = 0; i < cleartext.length() / 8; i++) {
			memcpy(inputText, cleartext.c_str() + i * 8, 8);
			DES_ecb_encrypt(&inputText, &outputText, &keySchedule, DES_ENCRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCiphertext.push_back(tmp[j]);
		}

		if (cleartext.length() % 8 != 0) {
			int tmp1 = cleartext.length() / 8 * 8;
			int tmp2 = cleartext.length() - tmp1;
			memset(inputText, 0, 8);
			memcpy(inputText, cleartext.c_str() + tmp1, tmp2);

			DES_ecb_encrypt(&inputText, &outputText, &keySchedule, DES_ENCRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCiphertext.push_back(tmp[j]);
		}

		strCipherText.clear();
		strCipherText.assign(vecCiphertext.begin(), vecCiphertext.end());
	}
		break;
	case CBC: {
		DES_cblock keyEncrypt, ivec;
		memset(keyEncrypt, 0, 8);

		if (key.length() <= 8)
			memcpy(keyEncrypt, key.c_str(), key.length());
		else
			memcpy(keyEncrypt, key.c_str(), 8);

		DES_key_schedule keySchedule;
		DES_set_key_unchecked(&keyEncrypt, &keySchedule);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		int iLength = cleartext.length() % 8 ? (cleartext.length() / 8 + 1) * 8 : cleartext.length();
		unsigned char* tmp = new unsigned char[iLength + 16];
		memset(tmp, 0, iLength);

		DES_ncbc_encrypt((const unsigned char*)cleartext.c_str(), tmp, cleartext.length() + 1, &keySchedule, &ivec, DES_ENCRYPT);

		// strClearText = (char*)tmp; 这种写法有问题，从unsigned char*强转为char*，如果遇到内存中间存在0x0，
		// strClearText实际是截断的，应该做个base64加密运算再返回，解密时数据才是完整的
		// strCipherText = (char*)tmp;
		strCipherText = reinterpret_cast<char*>(tmp);

		delete[] tmp;
	}
		break;
	case CFB: {
		DES_cblock keyEncrypt, ivec;
		memset(keyEncrypt, 0, 8);

		if (key.length() <= 8)
			memcpy(keyEncrypt, key.c_str(), key.length());
		else
			memcpy(keyEncrypt, key.c_str(), 8);

		DES_key_schedule keySchedule;
		DES_set_key_unchecked(&keyEncrypt, &keySchedule);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		unsigned char* outputText = new unsigned char[cleartext.length()];
		memset(outputText, 0, cleartext.length());

		const unsigned char* tmp = (const unsigned char*)cleartext.c_str();

		DES_cfb_encrypt(tmp, outputText, 8, cleartext.length(), &keySchedule, &ivec, DES_ENCRYPT);

		strCipherText = (char*)outputText;

		delete[] outputText;
	}
		break;
	case TRIPLE_ECB: {
		DES_cblock ke1, ke2, ke3;
		memset(ke1, 0, 8);
		memset(ke2, 0, 8);
		memset(ke2, 0, 8);

		if (key.length() >= 24) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, 8);
		}
		else if (key.length() >= 16) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, key.length() - 16);
		}
		else if (key.length() >= 8) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, key.length() - 8);
			memcpy(ke3, key.c_str(), 8);
		}
		else {
			memcpy(ke1, key.c_str(), key.length());
			memcpy(ke2, key.c_str(), key.length());
			memcpy(ke3, key.c_str(), key.length());
		}

		DES_key_schedule ks1, ks2, ks3;
		DES_set_key_unchecked(&ke1, &ks1);
		DES_set_key_unchecked(&ke2, &ks2);
		DES_set_key_unchecked(&ke3, &ks3);

		const_DES_cblock inputText;
		DES_cblock outputText;
		std::vector<unsigned char> vecCiphertext;
		unsigned char tmp[8];

		for (int i = 0; i < cleartext.length() / 8; i++) {
			memcpy(inputText, cleartext.c_str() + i * 8, 8);
			DES_ecb3_encrypt(&inputText, &outputText, &ks1, &ks2, &ks3, DES_ENCRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCiphertext.push_back(tmp[j]);
		}

		if (cleartext.length() % 8 != 0) {
			int tmp1 = cleartext.length() / 8 * 8;
			int tmp2 = cleartext.length() - tmp1;
			memset(inputText, 0, 8);
			memcpy(inputText, cleartext.c_str() + tmp1, tmp2);

			DES_ecb3_encrypt(&inputText, &outputText, &ks1, &ks2, &ks3, DES_ENCRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCiphertext.push_back(tmp[j]);
		}

		strCipherText.clear();
		strCipherText.assign(vecCiphertext.begin(), vecCiphertext.end());
	}
		break;
	case TRIPLE_CBC: {
		DES_cblock ke1, ke2, ke3, ivec;
		memset(ke1, 0, 8);
		memset(ke2, 0, 8);
		memset(ke2, 0, 8);

		if (key.length() >= 24) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, 8);
		}
		else if (key.length() >= 16) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, key.length() - 16);
		}
		else if (key.length() >= 8) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, key.length() - 8);
			memcpy(ke3, key.c_str(), 8);
		}
		else {
			memcpy(ke1, key.c_str(), key.length());
			memcpy(ke2, key.c_str(), key.length());
			memcpy(ke3, key.c_str(), key.length());
		}

		DES_key_schedule ks1, ks2, ks3;
		DES_set_key_unchecked(&ke1, &ks1);
		DES_set_key_unchecked(&ke2, &ks2);
		DES_set_key_unchecked(&ke3, &ks3);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		int iLength = cleartext.length() % 8 ? (cleartext.length() / 8 + 1) * 8 : cleartext.length();
		unsigned char* tmp = new unsigned char[iLength + 16];
		memset(tmp, 0, iLength);

		DES_ede3_cbc_encrypt((const unsigned char*)cleartext.c_str(), tmp, cleartext.length() + 1, &ks1, &ks2, &ks3, &ivec, DES_ENCRYPT);

		strCipherText = (char*)tmp;

		delete[] tmp;
	}
		break;
	default:
		fprintf(stderr, "Error: DES don't support this mode encrypt: %d\n", mode);
	}

	return strCipherText;
}

std::string des_decrypt(const std::string& ciphertext, const std::string& key, CRYPTO_MODE mode)
{
	std::string strClearText;

	switch (mode) {
	case GENERAL:
	case ECB: {
	DES_cblock keyEncrypt;
	memset(keyEncrypt, 0, 8);

	if (key.length() <= 8)
		memcpy(keyEncrypt, key.c_str(), key.length());
	else
		memcpy(keyEncrypt, key.c_str(), 8);

	DES_key_schedule keySchedule;
	DES_set_key_unchecked(&keyEncrypt, &keySchedule);

	const_DES_cblock inputText;
	DES_cblock outputText;
	std::vector<unsigned char> vecCleartext;
	unsigned char tmp[8];

	for (int i = 0; i < ciphertext.length() / 8; i++) {
		memcpy(inputText, ciphertext.c_str() + i * 8, 8);
		DES_ecb_encrypt(&inputText, &outputText, &keySchedule, DES_DECRYPT);
		memcpy(tmp, outputText, 8);

		for (int j = 0; j < 8; j++)
			vecCleartext.push_back(tmp[j]);
	}

	if (ciphertext.length() % 8 != 0) {
		int tmp1 = ciphertext.length() / 8 * 8;
		int tmp2 = ciphertext.length() - tmp1;
		memset(inputText, 0, 8);
		memcpy(inputText, ciphertext.c_str() + tmp1, tmp2);

		DES_ecb_encrypt(&inputText, &outputText, &keySchedule, DES_DECRYPT);
		memcpy(tmp, outputText, 8);

		for (int j = 0; j < 8; j++)
			vecCleartext.push_back(tmp[j]);
	}

	strClearText.clear();
	strClearText.assign(vecCleartext.begin(), vecCleartext.end());
	}
		break;
	case CBC: {
		DES_cblock keyEncrypt, ivec;
		memset(keyEncrypt, 0, 8);

		if (key.length() <= 8)
			memcpy(keyEncrypt, key.c_str(), key.length());
		else
			memcpy(keyEncrypt, key.c_str(), 8);

		DES_key_schedule keySchedule;
		DES_set_key_unchecked(&keyEncrypt, &keySchedule);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		int iLength = ciphertext.length() % 8 ? (ciphertext.length() / 8 + 1) * 8 : ciphertext.length();
		unsigned char* tmp = new unsigned char[iLength + 16];
		memset(tmp, 0, iLength);

		DES_ncbc_encrypt((const unsigned char*)ciphertext.c_str(), tmp, ciphertext.length() + 1, &keySchedule, &ivec, DES_DECRYPT);

		strClearText = (char*)tmp;

		delete[] tmp;
	}
		break;
	case CFB: {
		DES_cblock keyEncrypt, ivec;
		memset(keyEncrypt, 0, 8);

		if (key.length() <= 8)
			memcpy(keyEncrypt, key.c_str(), key.length());
		else
			memcpy(keyEncrypt, key.c_str(), 8);

		DES_key_schedule keySchedule;
		DES_set_key_unchecked(&keyEncrypt, &keySchedule);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		unsigned char* outputText = new unsigned char[ciphertext.length()];
		memset(outputText, 0, ciphertext.length());

		const unsigned char* tmp = (const unsigned char*)ciphertext.c_str();

		DES_cfb_encrypt(tmp, outputText, 8, 32/*ciphertext.length() - 16*/, &keySchedule, &ivec, DES_DECRYPT);

		strClearText = (char*)outputText;

		delete[] outputText;
	}
		break;
	case TRIPLE_ECB: {
		DES_cblock ke1, ke2, ke3;
		memset(ke1, 0, 8);
		memset(ke2, 0, 8);
		memset(ke2, 0, 8);

		if (key.length() >= 24) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, 8);
		}
		else if (key.length() >= 16) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, key.length() - 16);
		}
		else if (key.length() >= 8) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, key.length() - 8);
			memcpy(ke3, key.c_str(), 8);
		}
		else {
			memcpy(ke1, key.c_str(), key.length());
			memcpy(ke2, key.c_str(), key.length());
			memcpy(ke3, key.c_str(), key.length());
		}

		DES_key_schedule ks1, ks2, ks3;
		DES_set_key_unchecked(&ke1, &ks1);
		DES_set_key_unchecked(&ke2, &ks2);
		DES_set_key_unchecked(&ke3, &ks3);

		const_DES_cblock inputText;
		DES_cblock outputText;
		std::vector<unsigned char> vecCleartext;
		unsigned char tmp[8];

		for (int i = 0; i < ciphertext.length() / 8; i++) {
			memcpy(inputText, ciphertext.c_str() + i * 8, 8);
			DES_ecb3_encrypt(&inputText, &outputText, &ks1, &ks2, &ks3, DES_DECRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCleartext.push_back(tmp[j]);
		}

		if (ciphertext.length() % 8 != 0) {
			int tmp1 = ciphertext.length() / 8 * 8;
			int tmp2 = ciphertext.length() - tmp1;
			memset(inputText, 0, 8);
			memcpy(inputText, ciphertext.c_str() + tmp1, tmp2);

			DES_ecb3_encrypt(&inputText, &outputText, &ks1, &ks2, &ks3, DES_DECRYPT);
			memcpy(tmp, outputText, 8);

			for (int j = 0; j < 8; j++)
				vecCleartext.push_back(tmp[j]);
		}

		strClearText.clear();
		strClearText.assign(vecCleartext.begin(), vecCleartext.end());
	}
		break;
	case TRIPLE_CBC: {
		DES_cblock ke1, ke2, ke3, ivec;
		memset(ke1, 0, 8);
		memset(ke2, 0, 8);
		memset(ke2, 0, 8);

		if (key.length() >= 24) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, 8);
		}
		else if (key.length() >= 16) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, 8);
			memcpy(ke3, key.c_str() + 16, key.length() - 16);
		}
		else if (key.length() >= 8) {
			memcpy(ke1, key.c_str(), 8);
			memcpy(ke2, key.c_str() + 8, key.length() - 8);
			memcpy(ke3, key.c_str(), 8);
		}
		else {
			memcpy(ke1, key.c_str(), key.length());
			memcpy(ke2, key.c_str(), key.length());
			memcpy(ke3, key.c_str(), key.length());
		}

		DES_key_schedule ks1, ks2, ks3;
		DES_set_key_unchecked(&ke1, &ks1);
		DES_set_key_unchecked(&ke2, &ks2);
		DES_set_key_unchecked(&ke3, &ks3);

		memcpy(ivec, cbc_iv, sizeof(cbc_iv));

		int iLength = ciphertext.length() % 8 ? (ciphertext.length() / 8 + 1) * 8 : ciphertext.length();
		unsigned char* tmp = new unsigned char[iLength];
		memset(tmp, 0, iLength);

		DES_ede3_cbc_encrypt((const unsigned char*)ciphertext.c_str(), tmp, ciphertext.length() + 1, &ks1, &ks2, &ks3, &ivec, DES_DECRYPT);

		strClearText = (char*)tmp;

		delete[] tmp;
	}
		break;
	default:
		fprintf(stderr, "Error: DES don't support this mode decrypt: %d\n", mode);
	}

	return strClearText;
}

} // namespace

int test_openssl_des()
{
	const std::string cleartext = "中国北京12345$abcde%ABCDE！！！!";
	const std::string key = "beijingchina1234567890ABCDEFGH!!!";

	char* cleartext_encode = b64_encode((const unsigned char*)cleartext.c_str(), cleartext.length());
	std::string str_encode(cleartext_encode);
	free(cleartext_encode);

	CRYPTO_MODE mode = CBC;
	std::string ciphertext = des_encrypt(str_encode, key, mode);
	std::string decrypt = des_decrypt(ciphertext, key, mode);
	unsigned char* ciphertext_decode = b64_decode(decrypt.c_str(), decrypt.length());
	std::string str_decode((char*)ciphertext_decode);
	free(ciphertext_decode);

	fprintf(stdout, "src cleartext: %s, size: %d\n", cleartext.c_str(), cleartext.length());
	fprintf(stdout, "genarate ciphertext: %s, size: %d\n", ciphertext.c_str(), ciphertext.length());
	fprintf(stdout, "dst cleartext: %s, size: %d\n", str_decode.c_str(), str_decode.length());

	if (strcmp(cleartext.c_str(), str_decode.c_str()) == 0) {
		fprintf(stdout, "DES decrypt success\n");
		return 0;
	} else {
		fprintf(stderr, "DES decrypt fail\n");
		return -1;
	}	
}

//////////////////////////// RC4 ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/42929883
namespace {
void RC4_Encrypt(const unsigned char* cleartext, int length, const std::string& key, unsigned char* ciphertext)
{
	RC4_KEY rc4key;

	RC4_set_key(&rc4key, key.length(), (const unsigned char*)key.c_str());
	RC4(&rc4key, length, cleartext, ciphertext);
}

void RC4_Decrypt(const unsigned char* ciphertext, int length, const std::string& key, unsigned char* cleartext)
{
	RC4_KEY rc4key;

	RC4_set_key(&rc4key, key.length(), (const unsigned char*)key.c_str());
	RC4(&rc4key, length, ciphertext, cleartext);
}

} // namespace

int test_openssl_rc4()
{
	const std::string cleartext = "中国北京12345$abcde%ABCDE@！！！!";
	const std::string key = "beijingchina1234567890ABCDEFGH!!!";

	char* cleartext_encode = b64_encode((const unsigned char*)cleartext.c_str(), cleartext.length());
	int length = strlen(cleartext_encode);
	fprintf(stdout, "cleartext encode length: %d\n", length);

	std::unique_ptr<unsigned char[]> ciphertext(new unsigned char[length]);
	RC4_Encrypt((const unsigned char*)cleartext_encode, length, key, ciphertext.get());

	std::unique_ptr<unsigned char[]> decrypt(new unsigned char[length]);
	RC4_Decrypt(ciphertext.get(), length, key, decrypt.get());

	unsigned char* ciphertext_decode = b64_decode((char*)decrypt.get(), length);

	fprintf(stdout, "src cleartext: %s\n", cleartext.c_str());
	fprintf(stdout, "genarate ciphertext: %s\n", ciphertext.get());
	fprintf(stdout, "dst cleartext: %s\n", ciphertext_decode);
	int ret = 0;

	if (strcmp(cleartext.c_str(), (const char*)ciphertext_decode) == 0) {
		fprintf(stdout, "RC4 decrypt success\n");
	} else {
		fprintf(stderr, "RC4 decrypt fail\n");
		ret = -1;
	}

	free(cleartext_encode);
	free(ciphertext_decode);
	return ret;
}

////////////////////////////// MD5 /////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/42978603
namespace {
std::string MD5_Digest(const std::string& cleartext)
{
	std::string strDigest;
	unsigned char tmp[16] = { 0 };

#if 0
	MD5((const unsigned char*)cleartext.c_str(), cleartext.length(), tmp);
#else
	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, cleartext.c_str(), cleartext.length());
	MD5_Final(tmp, &c);
#endif

	char* tmp1 = new char[32 + 1];
	memset(tmp1, 0, 32 + 1);

	for (int i = 0; i < 16; i++)
		sprintf(&(tmp1[i * 2]), "%02x", tmp[i]);
	//cout<<hex<<setw(2)<<setfill('0')<<(int)tmp[i]; 

	strDigest = (char*)tmp1;

	delete[] tmp1;
	return strDigest;
}

} // namespace

int test_openssl_md5()
{
	std::string strSrc[7] = {
		"",
		"a",
		"abc",
		"message digest",
		"abcdefghijklmnopqrstuvwxyz",
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
		"12345678901234567890123456789012345678901234567890123456789012345678901234567890" };
	std::string strDigest[7] = { 
		"d41d8cd98f00b204e9800998ecf8427e",
		"0cc175b9c0f1b6a831c399e269772661",
		"900150983cd24fb0d6963f7d28e17f72",
		"f96b697d7cb7938d525a2f31aaf161d0",
		"c3fcd3d76192e4007dfb496cca67e13b",
		"d174ab98d277d9f5a5611c2c9f419d9f",
		"57edf4a22be3c955ac49da2e2107b67a" };

	for (int i = 0; i < 7; i++) {
		std::string str = MD5_Digest(strSrc[i]);
		fprintf(stdout, "str: %s\n", str.c_str());

		if (strcmp(strDigest[i].c_str(), str.c_str()) != 0) {
			fprintf(stderr, "i: %d, MD5 error\n", i);
			return -1;
		}
	}

	return 0;
}

//////////////////////////// RSA ///////////////////////////////
// Blog: https://blog.csdn.net/fengbingchun/article/details/43638013
namespace {
const int KEY_LENGTH = 2048;
const int padding = RSA_PKCS1_PADDING;
const unsigned long PUB_EXP = 3;

void Generate_RSA_Key(std::string& public_key, std::string& private_key)
{
	size_t pri_len;          // Length of private key
	size_t pub_len;          // Length of public key
	char *pri_key = NULL;           // Private key
	char *pub_key = NULL;           // Public key

	RSA* keypair = RSA_generate_key(KEY_LENGTH, RSA_3, NULL, NULL);

	BIO *pri = BIO_new(BIO_s_mem());
	BIO *pub = BIO_new(BIO_s_mem());

	PEM_write_bio_RSAPrivateKey(pri, keypair, NULL, NULL, 0, NULL, NULL);
	PEM_write_bio_RSAPublicKey(pub, keypair);

	pri_len = BIO_pending(pri);
	pub_len = BIO_pending(pub);

	pri_key = new char[pri_len + 1];
	pub_key = new char[pub_len + 1];

	BIO_read(pri, pri_key, pri_len);
	BIO_read(pub, pub_key, pub_len);

	pri_key[pri_len] = '\0';
	pub_key[pub_len] = '\0';

	public_key = std::string(pub_key, pub_len+1);
	private_key = std::string(pri_key, pri_len+1);

	RSA_free(keypair);
	BIO_free_all(pub);
	BIO_free_all(pri);
	delete[] pri_key;
	delete[] pub_key;
}

RSA* createRSA(const unsigned char* key, int flag)
{
	RSA *rsa = NULL;
	BIO *keybio;
	keybio = BIO_new_mem_buf((unsigned char*)key, -1);

	if (keybio == NULL) {
		fprintf(stderr, "Error: fail to create key BIO\n");
		return nullptr;
	}

	if (flag)
		rsa = PEM_read_bio_RSA_PUBKEY(keybio, &rsa, NULL, NULL);
	else
		rsa = PEM_read_bio_RSAPrivateKey(keybio, &rsa, NULL, NULL);

	if (rsa == NULL) {
		fprintf(stderr, "Error: fail to create RSA\n");
		return nullptr;
	}

	return rsa;
}

int public_encrypt(const unsigned char* data, int data_len, const unsigned char* key, unsigned char* encrypted)
{
	RSA * rsa = createRSA(key, 1);
	int result = RSA_public_encrypt(data_len, data, encrypted, rsa, padding);
	return result;
}

int private_decrypt(const unsigned char* enc_data, int data_len, const unsigned char* key, unsigned char* decrypted)
{
	RSA * rsa = createRSA(key, 0);
	int  result = RSA_private_decrypt(data_len, enc_data, decrypted, rsa, padding);
	return result;
}

int private_encrypt(const unsigned char* data, int data_len, const unsigned char* key, unsigned char* encrypted)
{
	RSA * rsa = createRSA(key, 0);
	int result = RSA_private_encrypt(data_len, data, encrypted, rsa, padding);
	return result;
}

int public_decrypt(const unsigned char* enc_data, int data_len, const unsigned char* key, unsigned char* decrypted)
{
	RSA * rsa = createRSA(key, 1);
	int  result = RSA_public_decrypt(data_len, enc_data, decrypted, rsa, padding);
	return result;
}

} // namespace

int test_openssl_rsa()
{
	std::vector<std::string> strKey(2);//[0]:public key; [1]:private key 
	Generate_RSA_Key(strKey[0], strKey[1]);
	fprintf(stdout, "public key:\n%s\n", strKey[0].c_str());
	fprintf(stdout, "private key:\n%s\n", strKey[1].c_str());

	strKey[0] = "-----BEGIN PUBLIC KEY-----\n" \
		"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAy8Dbv8prpJ/0kKhlGeJY\n" \
		"ozo2t60EG8L0561g13R29LvMR5hyvGZlGJpmn65+A4xHXInJYiPuKzrKUnApeLZ+\n" \
		"vw1HocOAZtWK0z3r26uA8kQYOKX9Qt/DbCdvsF9wF8gRK0ptx9M6R13NvBxvVQAp\n" \
		"fc9jB9nTzphOgM4JiEYvlV8FLhg9yZovMYd6Wwf3aoXK891VQxTr/kQYoq1Yp+68\n" \
		"i6T4nNq7NWC+UNVjQHxNQMQMzU6lWCX8zyg3yH88OAQkUXIXKfQ+NkvYQ1cxaMoV\n" \
		"PpY72+eVthKzpMeyHkBn7ciumk5qgLTEJAfWZpe4f4eFZj/Rc8Y8Jj2IS5kVPjUy\n" \
		"wQIDAQAB\n" \
		"-----END PUBLIC KEY-----\n";
	strKey[1] = "-----BEGIN RSA PRIVATE KEY-----\n"\
		"MIIEowIBAAKCAQEAy8Dbv8prpJ/0kKhlGeJYozo2t60EG8L0561g13R29LvMR5hy\n"\
		"vGZlGJpmn65+A4xHXInJYiPuKzrKUnApeLZ+vw1HocOAZtWK0z3r26uA8kQYOKX9\n"\
		"Qt/DbCdvsF9wF8gRK0ptx9M6R13NvBxvVQApfc9jB9nTzphOgM4JiEYvlV8FLhg9\n"\
		"yZovMYd6Wwf3aoXK891VQxTr/kQYoq1Yp+68i6T4nNq7NWC+UNVjQHxNQMQMzU6l\n"\
		"WCX8zyg3yH88OAQkUXIXKfQ+NkvYQ1cxaMoVPpY72+eVthKzpMeyHkBn7ciumk5q\n"\
		"gLTEJAfWZpe4f4eFZj/Rc8Y8Jj2IS5kVPjUywQIDAQABAoIBADhg1u1Mv1hAAlX8\n"\
		"omz1Gn2f4AAW2aos2cM5UDCNw1SYmj+9SRIkaxjRsE/C4o9sw1oxrg1/z6kajV0e\n"\
		"N/t008FdlVKHXAIYWF93JMoVvIpMmT8jft6AN/y3NMpivgt2inmmEJZYNioFJKZG\n"\
		"X+/vKYvsVISZm2fw8NfnKvAQK55yu+GRWBZGOeS9K+LbYvOwcrjKhHz66m4bedKd\n"\
		"gVAix6NE5iwmjNXktSQlJMCjbtdNXg/xo1/G4kG2p/MO1HLcKfe1N5FgBiXj3Qjl\n"\
		"vgvjJZkh1as2KTgaPOBqZaP03738VnYg23ISyvfT/teArVGtxrmFP7939EvJFKpF\n"\
		"1wTxuDkCgYEA7t0DR37zt+dEJy+5vm7zSmN97VenwQJFWMiulkHGa0yU3lLasxxu\n"\
		"m0oUtndIjenIvSx6t3Y+agK2F3EPbb0AZ5wZ1p1IXs4vktgeQwSSBdqcM8LZFDvZ\n"\
		"uPboQnJoRdIkd62XnP5ekIEIBAfOp8v2wFpSfE7nNH2u4CpAXNSF9HsCgYEA2l8D\n"\
		"JrDE5m9Kkn+J4l+AdGfeBL1igPF3DnuPoV67BpgiaAgI4h25UJzXiDKKoa706S0D\n"\
		"4XB74zOLX11MaGPMIdhlG+SgeQfNoC5lE4ZWXNyESJH1SVgRGT9nBC2vtL6bxCVV\n"\
		"WBkTeC5D6c/QXcai6yw6OYyNNdp0uznKURe1xvMCgYBVYYcEjWqMuAvyferFGV+5\n"\
		"nWqr5gM+yJMFM2bEqupD/HHSLoeiMm2O8KIKvwSeRYzNohKTdZ7FwgZYxr8fGMoG\n"\
		"PxQ1VK9DxCvZL4tRpVaU5Rmknud9hg9DQG6xIbgIDR+f79sb8QjYWmcFGc1SyWOA\n"\
		"SkjlykZ2yt4xnqi3BfiD9QKBgGqLgRYXmXp1QoVIBRaWUi55nzHg1XbkWZqPXvz1\n"\
		"I3uMLv1jLjJlHk3euKqTPmC05HoApKwSHeA0/gOBmg404xyAYJTDcCidTg6hlF96\n"\
		"ZBja3xApZuxqM62F6dV4FQqzFX0WWhWp5n301N33r0qR6FumMKJzmVJ1TA8tmzEF\n"\
		"yINRAoGBAJqioYs8rK6eXzA8ywYLjqTLu/yQSLBn/4ta36K8DyCoLNlNxSuox+A5\n"\
		"w6z2vEfRVQDq4Hm4vBzjdi3QfYLNkTiTqLcvgWZ+eX44ogXtdTDO7c+GeMKWz4XX\n"\
		"uJSUVL5+CVjKLjZEJ6Qc2WZLl94xSwL71E41H4YciVnSCQxVc4Jw\n"\
		"-----END RSA PRIVATE KEY-----\n";

	std::string cleartext = "中国北京12345$abcde%ABCDE@！！！!";

	unsigned char  encrypted[4098] = {};
	unsigned char decrypted[4098] = {};

	int encrypted_length = public_encrypt((const unsigned char*)cleartext.c_str(), cleartext.length(), (const unsigned char*)strKey[0].c_str(), encrypted);
	if (encrypted_length == -1) {
		fprintf(stderr, "Error: fail to public key encrypt\n");
		return -1;
	}
	fprintf(stdout, "Encrypted length = %d\n", encrypted_length);

	int decrypted_length = private_decrypt(encrypted, encrypted_length, (const unsigned char*)strKey[1].c_str(), decrypted);
	if (decrypted_length == -1) {
		fprintf(stderr, "Error: fail to private key decrypt\n");
		return -1;
	}
	fprintf(stdout, "Decrypted Length = %d\n", decrypted_length);
	fprintf(stdout, "Decrypted Text = %s\n", decrypted);
	
	encrypted_length = private_encrypt((const unsigned char*)cleartext.c_str(), cleartext.length(), (const unsigned char*)strKey[1].c_str(), encrypted);
	if (encrypted_length == -1) {
		fprintf(stderr, "Error: fail to private key encrypt\n");
		return -1;
	}
	fprintf(stdout, "Encrypted length = %d\n", encrypted_length);

	decrypted_length = public_decrypt(encrypted, encrypted_length, (const unsigned char*)strKey[0].c_str(), decrypted);
	if (decrypted_length == -1) {
		fprintf(stderr, "Error: fail to public key decrypt\n");
		return -1;
	}
	fprintf(stdout, "Decrypted Length = %d\n", decrypted_length);
	fprintf(stdout, "Decrypted Text = %s \n", decrypted);

	return 0;
}
