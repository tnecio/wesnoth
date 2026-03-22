/*
	Copyright (C) 2008 - 2025
	by Thomas Baumhauer <thomas.baumhauer@NOSPAMgmail.com>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "hash.hpp"

#include "serialization/base64.hpp"

#include <string>
#include <sstream>
#include <string.h>
#include <assert.h>

extern "C" {
#include "crypt_blowfish/crypt_blowfish.h"
}

#if defined(HEADLESS_ENGINE)

#include <cstring>

// Standalone MD5 — no OpenSSL dependency in headless builds.
// Based on the RSA Data Security, Inc. MD5 Message-Digest Algorithm (RFC 1321).

#elif defined(__APPLE__)

#include <CommonCrypto/CommonDigest.h>

static_assert(utils::md5::DIGEST_SIZE == CC_MD5_DIGEST_LENGTH, "Constants mismatch");

#else

#include <openssl/evp.h>

#endif

namespace {

const std::string hash_prefix = "$H$";

#if defined(HEADLESS_ENGINE)

struct MD5Context {
	uint32_t state[4];
	uint32_t count[2];
	unsigned char buffer[64];
};

static void MD5Transform(uint32_t state[4], const unsigned char block[64]);

static void MD5Init(MD5Context* ctx)
{
	ctx->count[0] = ctx->count[1] = 0;
	ctx->state[0] = 0x67452301u;
	ctx->state[1] = 0xefcdab89u;
	ctx->state[2] = 0x98badcfeu;
	ctx->state[3] = 0x10325476u;
}

static void MD5Update(MD5Context* ctx, const unsigned char* input, std::size_t len)
{
	std::size_t index = static_cast<std::size_t>((ctx->count[0] >> 3) & 0x3f);
	if((ctx->count[0] += static_cast<uint32_t>(len << 3)) < static_cast<uint32_t>(len << 3))
		ctx->count[1]++;
	ctx->count[1] += static_cast<uint32_t>(len >> 29);

	std::size_t part_len = 64 - index;
	std::size_t i = 0;
	if(len >= part_len) {
		std::memcpy(&ctx->buffer[index], input, part_len);
		MD5Transform(ctx->state, ctx->buffer);
		for(i = part_len; i + 63 < len; i += 64)
			MD5Transform(ctx->state, &input[i]);
		index = 0;
	}
	std::memcpy(&ctx->buffer[index], &input[i], len - i);
}

static void MD5Final(unsigned char digest[16], MD5Context* ctx)
{
	unsigned char bits[8];
	for(std::size_t k = 0; k < 4; ++k) {
		bits[k]   = static_cast<unsigned char>(ctx->count[0] >> (k * 8));
		bits[k+4] = static_cast<unsigned char>(ctx->count[1] >> (k * 8));
	}

	static const unsigned char padding[64] = {0x80};
	std::size_t index = static_cast<std::size_t>((ctx->count[0] >> 3) & 0x3f);
	std::size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
	MD5Update(ctx, padding, pad_len);
	MD5Update(ctx, bits, 8);

	for(std::size_t k = 0; k < 4; ++k)
		for(std::size_t j = 0; j < 4; ++j)
			digest[k*4+j] = static_cast<unsigned char>(ctx->state[k] >> (j * 8));

	std::memset(ctx, 0, sizeof(*ctx));
}

static inline uint32_t md5_F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
static inline uint32_t md5_G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
static inline uint32_t md5_H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static inline uint32_t md5_I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
static inline uint32_t md5_rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void MD5Transform(uint32_t state[4], const unsigned char block[64])
{
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t x[16];
	for(int i = 0; i < 16; ++i)
		x[i] = static_cast<uint32_t>(block[i*4])
		     | (static_cast<uint32_t>(block[i*4+1]) << 8)
		     | (static_cast<uint32_t>(block[i*4+2]) << 16)
		     | (static_cast<uint32_t>(block[i*4+3]) << 24);

#define MD5_FF(a,b,c,d,k,s,t) a += md5_F(b,c,d)+x[k]+(t); a = md5_rotl(a,s)+b
#define MD5_GG(a,b,c,d,k,s,t) a += md5_G(b,c,d)+x[k]+(t); a = md5_rotl(a,s)+b
#define MD5_HH(a,b,c,d,k,s,t) a += md5_H(b,c,d)+x[k]+(t); a = md5_rotl(a,s)+b
#define MD5_II(a,b,c,d,k,s,t) a += md5_I(b,c,d)+x[k]+(t); a = md5_rotl(a,s)+b

	MD5_FF(a,b,c,d, 0, 7,0xd76aa478u); MD5_FF(d,a,b,c, 1,12,0xe8c7b756u);
	MD5_FF(c,d,a,b, 2,17,0x242070dbu); MD5_FF(b,c,d,a, 3,22,0xc1bdceeeu);
	MD5_FF(a,b,c,d, 4, 7,0xf57c0fafu); MD5_FF(d,a,b,c, 5,12,0x4787c62au);
	MD5_FF(c,d,a,b, 6,17,0xa8304613u); MD5_FF(b,c,d,a, 7,22,0xfd469501u);
	MD5_FF(a,b,c,d, 8, 7,0x698098d8u); MD5_FF(d,a,b,c, 9,12,0x8b44f7afu);
	MD5_FF(c,d,a,b,10,17,0xffff5bb1u); MD5_FF(b,c,d,a,11,22,0x895cd7beu);
	MD5_FF(a,b,c,d,12, 7,0x6b901122u); MD5_FF(d,a,b,c,13,12,0xfd987193u);
	MD5_FF(c,d,a,b,14,17,0xa679438eu); MD5_FF(b,c,d,a,15,22,0x49b40821u);

	MD5_GG(a,b,c,d, 1, 5,0xf61e2562u); MD5_GG(d,a,b,c, 6, 9,0xc040b340u);
	MD5_GG(c,d,a,b,11,14,0x265e5a51u); MD5_GG(b,c,d,a, 0,20,0xe9b6c7aau);
	MD5_GG(a,b,c,d, 5, 5,0xd62f105du); MD5_GG(d,a,b,c,10, 9,0x02441453u);
	MD5_GG(c,d,a,b,15,14,0xd8a1e681u); MD5_GG(b,c,d,a, 4,20,0xe7d3fbc8u);
	MD5_GG(a,b,c,d, 9, 5,0x21e1cde6u); MD5_GG(d,a,b,c,14, 9,0xc33707d6u);
	MD5_GG(c,d,a,b, 3,14,0xf4d50d87u); MD5_GG(b,c,d,a, 8,20,0x455a14edu);
	MD5_GG(a,b,c,d,13, 5,0xa9e3e905u); MD5_GG(d,a,b,c, 2, 9,0xfcefa3f8u);
	MD5_GG(c,d,a,b, 7,14,0x676f02d9u); MD5_GG(b,c,d,a,12,20,0x8d2a4c8au);

	MD5_HH(a,b,c,d, 5, 4,0xfffa3942u); MD5_HH(d,a,b,c, 8,11,0x8771f681u);
	MD5_HH(c,d,a,b,11,16,0x6d9d6122u); MD5_HH(b,c,d,a,14,23,0xfde5380cu);
	MD5_HH(a,b,c,d, 1, 4,0xa4beea44u); MD5_HH(d,a,b,c, 4,11,0x4bdecfa9u);
	MD5_HH(c,d,a,b, 7,16,0xf6bb4b60u); MD5_HH(b,c,d,a,10,23,0xbebfbc70u);
	MD5_HH(a,b,c,d,13, 4,0x289b7ec6u); MD5_HH(d,a,b,c, 0,11,0xeaa127fau);
	MD5_HH(c,d,a,b, 3,16,0xd4ef3085u); MD5_HH(b,c,d,a, 6,23,0x04881d05u);
	MD5_HH(a,b,c,d, 9, 4,0xd9d4d039u); MD5_HH(d,a,b,c,12,11,0xe6db99e5u);
	MD5_HH(c,d,a,b,15,16,0x1fa27cf8u); MD5_HH(b,c,d,a, 2,23,0xc4ac5665u);

	MD5_II(a,b,c,d, 0, 6,0xf4292244u); MD5_II(d,a,b,c, 7,10,0x432aff97u);
	MD5_II(c,d,a,b,14,15,0xab9423a7u); MD5_II(b,c,d,a, 5,21,0xfc93a039u);
	MD5_II(a,b,c,d,12, 6,0x655b59c3u); MD5_II(d,a,b,c, 3,10,0x8f0ccc92u);
	MD5_II(c,d,a,b,10,15,0xffeff47du); MD5_II(b,c,d,a, 1,21,0x85845dd1u);
	MD5_II(a,b,c,d, 8, 6,0x6fa87e4fu); MD5_II(d,a,b,c,15,10,0xfe2ce6e0u);
	MD5_II(c,d,a,b, 6,15,0xa3014314u); MD5_II(b,c,d,a,13,21,0x4e0811a1u);
	MD5_II(a,b,c,d, 4, 6,0xf7537e82u); MD5_II(d,a,b,c,11,10,0xbd3af235u);
	MD5_II(c,d,a,b, 2,15,0x2ad7d2bbu); MD5_II(b,c,d,a, 9,21,0xeb86d391u);

#undef MD5_FF
#undef MD5_GG
#undef MD5_HH
#undef MD5_II

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	std::memset(x, 0, sizeof(x));
}

#endif // HEADLESS_ENGINE

template<std::size_t len>
std::string encode_hash(const std::array<uint8_t, len>& bytes) {
	return crypt64::encode(bytes);
}

template<std::size_t len>
std::string hexencode_hash(const std::array<uint8_t, len>& input) {
	std::ostringstream sout;
	sout << std::hex;
	for(uint8_t c : input) {
		sout << static_cast<int>(c);
	}
	return sout.str();
}

}

namespace utils {

md5::md5(const std::string& input) {

#if defined(HEADLESS_ENGINE)
	MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, reinterpret_cast<const unsigned char*>(input.c_str()), input.size());
	MD5Final(hash.data(), &ctx);
#elif defined(__APPLE__)
	CC_MD5(input.data(), static_cast<CC_LONG>(input.size()), hash.data());
#else
	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	unsigned int md5_digest_len = EVP_MD_size(EVP_md5());
	assert(utils::md5::DIGEST_SIZE == md5_digest_len);

	// MD5_Init
	EVP_DigestInit_ex(mdctx, EVP_md5(), nullptr);

	// MD5_Update
	EVP_DigestUpdate(mdctx, input.c_str(), input.size());

	// MD5_Final
	EVP_DigestFinal_ex(mdctx, hash.data(), &md5_digest_len);
	EVP_MD_CTX_free(mdctx);
#endif

}

int md5::get_iteration_count(const std::string& hash) {
	return crypt64::decode(hash[3]);
}

std::string md5::get_salt(const std::string& hash) {
	return hash.substr(4,8);
}

bool md5::is_valid_prefix(const std::string& hash)
{
	return hash.substr(0,3) == hash_prefix;
}

bool md5::is_valid_hash(const std::string& hash) {
	if(hash.size() != 34) return false;
	if(!is_valid_prefix(hash)) return false;

	const int iteration_count = get_iteration_count(hash);
	if(iteration_count < 7 || iteration_count > 30) return false;

	return true;
}

md5::md5(const std::string& password, const std::string& salt, int iteration_count)
{
	iteration_count = 1 << iteration_count;

	hash = md5(salt + password).raw_digest();
	do {
		hash = md5(std::string(hash.begin(), hash.end()).append(password)).raw_digest();
	} while(--iteration_count);
}

std::string md5::hex_digest() const
{
	return hexencode_hash<DIGEST_SIZE>(hash);
}

std::string md5::base64_digest() const
{
	return encode_hash<DIGEST_SIZE>(hash);
}

bcrypt::bcrypt(const std::string& input)
{
	assert(is_valid_prefix(input));

	iteration_count_delim_pos = input.find('$', 4);
	if(iteration_count_delim_pos == std::string::npos)
		throw hash_error("hash string malformed");
}

bcrypt bcrypt::from_salted_salt(const std::string& input)
{
	bcrypt hash { input };
	std::string bcrypt_salt = input.substr(0, hash.iteration_count_delim_pos + 23);
	if(bcrypt_salt.size() >= BCRYPT_HASHSIZE)
		throw hash_error("hash string too large");
	strcpy(hash.hash.data(), bcrypt_salt.c_str());

	return hash;
}

bcrypt bcrypt::from_hash_string(const std::string& input)
{
	bcrypt hash { input };
	if(input.size() >= BCRYPT_HASHSIZE)
		throw hash_error("hash string too large");
	strcpy(hash.hash.data(), input.c_str());

	return hash;
}

bcrypt bcrypt::hash_pw(const std::string& password, bcrypt& salt)
{
	bcrypt hash;
	if(!php_crypt_blowfish_rn(password.c_str(), salt.hash.data(), hash.hash.data(), BCRYPT_HASHSIZE))
		throw hash_error("failed to hash password");

	return hash;
}

bool bcrypt::is_valid_prefix(const std::string& hash) {
	return ((hash.compare(0, 4, "$2a$") == 0)
	     || (hash.compare(0, 4, "$2b$") == 0)
	     || (hash.compare(0, 4, "$2x$") == 0)
	     || (hash.compare(0, 4, "$2y$") == 0));
}

std::string bcrypt::get_salt() const
{
	std::size_t salt_pos = iteration_count_delim_pos + 23;
	if(salt_pos >= BCRYPT_HASHSIZE)
		throw hash_error("malformed hash");
	return std::string(hash.data(), salt_pos);
}

std::string bcrypt::hex_digest() const
{
	return std::string(hash.data());
}

std::string bcrypt::base64_digest() const
{
	return std::string(hash.data());
}

} // namespace utils
