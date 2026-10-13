//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

// SHA-256 (FIPS 180-4), fed a piece at a time, so that an update is hashed as
// it downloads. The same code on every platform, with nothing to link: the
// Windows build has no OpenSSL, and the Linux one should not need CNG.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class Sha256
{
public:
	Sha256() { Reset(); }

	void Reset()
	{
		state = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
		length = 0;
		used = 0;
	}

	void Update(const void *data, size_t size)
	{
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		length += size;
		while (size > 0) {
			size_t take = block.size() - used;
			if (take > size)
				take = size;
			for (size_t i = 0; i < take; i++)
				block[used + i] = bytes[i];
			used += take;
			bytes += take;
			size -= take;
			if (used == block.size()) {
				Compress();
				used = 0;
			}
		}
	}

	// The digest as 64 lowercase hex characters. The hash is reset after.
	std::string HexDigest()
	{
		uint64_t bits = length * 8;
		unsigned char pad = 0x80;
		Update(&pad, 1);
		unsigned char zero = 0;
		while (used != 56)
			Update(&zero, 1);
		unsigned char tail[8];
		for (int i = 0; i < 8; i++)
			tail[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
		Update(tail, 8);

		static const char kHex[] = "0123456789abcdef";
		std::string hex;
		hex.reserve(64);
		for (uint32_t word : state) {
			for (int shift = 28; shift >= 0; shift -= 4)
				hex += kHex[(word >> shift) & 0xf];
		}
		Reset();
		return hex;
	}

	static std::string Of(const void *data, size_t size)
	{
		Sha256 hash;
		hash.Update(data, size);
		return hash.HexDigest();
	}

private:
	static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

	void Compress()
	{
		static const uint32_t k[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
		};
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
				(uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
		}
		for (int i = 16; i < 64; i++) {
			uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
		uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
		for (int i = 0; i < 64; i++) {
			uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = h + s1 + ch + k[i] + w[i];
			uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
			uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			uint32_t t2 = s0 + maj;
			h = g; g = f; f = e; e = d + t1;
			d = c; c = b; b = a; a = t1 + t2;
		}
		state[0] += a; state[1] += b; state[2] += c; state[3] += d;
		state[4] += e; state[5] += f; state[6] += g; state[7] += h;
	}

	std::array<uint32_t, 8> state;
	std::array<unsigned char, 64> block{};
	uint64_t length = 0;
	size_t used = 0;
};
