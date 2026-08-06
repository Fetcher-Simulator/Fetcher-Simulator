#ifndef OPENMW_MP_SHA256_HPP
#define OPENMW_MP_SHA256_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <sstream>
#include <string>
#include <string_view>

namespace mwmp::crypto
{
    namespace detail
    {
        constexpr std::array<std::uint32_t, 64> sSha256Constants = { 0x428a2f98, 0x71374491, 0xb5c0fbcf,
            0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6,
            0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
            0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70,
            0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c,
            0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814,
            0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

        inline std::uint32_t rotateRight(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
        inline std::uint32_t choice(std::uint32_t x, std::uint32_t y, std::uint32_t z)
        {
            return (x & y) ^ (~x & z);
        }
        inline std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z)
        {
            return (x & y) ^ (x & z) ^ (y & z);
        }
        inline std::uint32_t sum0(std::uint32_t x)
        {
            return rotateRight(x, 2) ^ rotateRight(x, 13) ^ rotateRight(x, 22);
        }
        inline std::uint32_t sum1(std::uint32_t x)
        {
            return rotateRight(x, 6) ^ rotateRight(x, 11) ^ rotateRight(x, 25);
        }
        inline std::uint32_t gamma0(std::uint32_t x)
        {
            return rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
        }
        inline std::uint32_t gamma1(std::uint32_t x)
        {
            return rotateRight(x, 17) ^ rotateRight(x, 19) ^ (x >> 10);
        }

        inline void transform(std::array<std::uint32_t, 8>& hash, const std::uint8_t* block)
        {
            std::array<std::uint32_t, 64> words{};
            for (int i = 0; i < 16; ++i)
            {
                words[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16)
                    | (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
            }
            for (int i = 16; i < 64; ++i)
                words[i] = gamma1(words[i - 2]) + words[i - 7] + gamma0(words[i - 15]) + words[i - 16];

            auto [a, b, c, d, e, f, g, h] = hash;
            for (int i = 0; i < 64; ++i)
            {
                const std::uint32_t t1 = h + sum1(e) + choice(e, f, g) + sSha256Constants[i] + words[i];
                const std::uint32_t t2 = sum0(a) + majority(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            hash[0] += a;
            hash[1] += b;
            hash[2] += c;
            hash[3] += d;
            hash[4] += e;
            hash[5] += f;
            hash[6] += g;
            hash[7] += h;
        }
    }

    class Sha256
    {
    public:
        void update(const std::uint8_t* data, std::size_t size)
        {
            mTotalBytes += size;
            while (size > 0)
            {
                const std::size_t count = std::min(size, mBlock.size() - mBlockSize);
                std::copy_n(data, count, mBlock.data() + mBlockSize);
                mBlockSize += count;
                data += count;
                size -= count;
                if (mBlockSize == mBlock.size())
                {
                    detail::transform(mHash, mBlock.data());
                    mBlockSize = 0;
                }
            }
        }

        std::string finish()
        {
            const std::uint64_t bitLength = mTotalBytes * 8;
            mBlock[mBlockSize++] = 0x80;
            if (mBlockSize > 56)
            {
                std::fill(mBlock.begin() + mBlockSize, mBlock.end(), 0);
                detail::transform(mHash, mBlock.data());
                mBlockSize = 0;
            }
            std::fill(mBlock.begin() + mBlockSize, mBlock.begin() + 56, 0);
            for (int i = 0; i < 8; ++i)
                mBlock[56 + i] = static_cast<std::uint8_t>(bitLength >> ((7 - i) * 8));
            detail::transform(mHash, mBlock.data());

            std::ostringstream out;
            out << std::hex << std::setfill('0');
            for (std::uint32_t word : mHash)
                out << std::setw(8) << word;
            return out.str();
        }

    private:
        std::array<std::uint32_t, 8> mHash = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        std::array<std::uint8_t, 64> mBlock{};
        std::size_t mBlockSize = 0;
        std::uint64_t mTotalBytes = 0;
    };

    inline std::string sha256hex(std::string_view input)
    {
        Sha256 hash;
        hash.update(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
        return hash.finish();
    }

    inline std::string sha256hex(std::istream& input)
    {
        Sha256 hash;
        std::array<char, 64 * 1024> buffer{};
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0)
                hash.update(reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(count));
        }
        return hash.finish();
    }
}

#endif
