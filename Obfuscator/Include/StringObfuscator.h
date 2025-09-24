// StringObfuscator.h — C++17-friendly, MSVC-safe
#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <iostream>
#include <algorithm>

namespace StringObfuscator {

    // ---------- small constexpr utils ----------
    constexpr uint8_t rotl8(uint8_t v, unsigned r) {
        return static_cast<uint8_t>((v << (r & 7)) | (v >> ((8 - (r & 7)) & 7)));
    }
    constexpr uint8_t rotr8(uint8_t v, unsigned r) {
        return static_cast<uint8_t>((v >> (r & 7)) | (v << ((8 - (r & 7)) & 7)));
    }
    template <typename T>
    constexpr void cswap(T& a, T& b) { T t = a; a = b; b = t; }

    // a simple constexpr mixer (xorshift-ish)
    constexpr uint32_t mix32(uint32_t x) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x;
    }

    // --------- Layer 1 ----------
    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer1_XOR(const char(&str)[N]) {
        std::array<uint8_t, N> out{};
        const uint64_t key1 = (static_cast<uint64_t>(SEED) * 0x100000001B3ull) ^ 0xDEADBEEFull;
        const uint64_t key2 = (static_cast<uint64_t>(SEED) * 0x1000000001B3ull) ^ 0xCAFEBABEull;
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t c = static_cast<uint8_t>(str[i]);
            const unsigned s1 = static_cast<unsigned>((i * 8u) % 56u);
            const unsigned s2 = static_cast<unsigned>((i * 3u) % 56u);
            c ^= static_cast<uint8_t>((key1 >> s1) & 0xFFu);
            c ^= static_cast<uint8_t>((key2 >> s2) & 0xFFu);
            out[i] = c;
        }
        return out;
    }

    // --------- Layer 2 ----------
    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer2_BitRotate(const std::array<uint8_t, N>& in) {
        std::array<uint8_t, N> out{};
        const unsigned base = static_cast<unsigned>(SEED % 7u) + 1u;
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t c = in[i];
            const unsigned r = (base + static_cast<unsigned>(i)) % 7u + 1u;
            c = rotl8(c, r);
            c ^= (i % 2 == 0) ? uint8_t{ 0xAA } : uint8_t{ 0x55 };
            out[i] = c;
        }
        return out;
    }

    // --------- Layer 3 ----------
    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer3_Shuffle(const std::array<uint8_t, N>& in) {
        std::array<uint8_t, N> out = in;
        if constexpr (N > 1) {
            for (std::size_t i = N - 1; i > 0; --i) {
                const std::size_t j = (static_cast<std::size_t>(SEED) * (i + 1)) % (i + 1);
                cswap(out[i], out[j]);
            }
        }
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = static_cast<uint8_t>((static_cast<uint8_t>(out[i] + 13u)) ^ 42u);
        }
        return out;
    }

    // --------- Layer 4 ----------
    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer4_MultiPass(const std::array<uint8_t, N>& in) {
        std::array<uint8_t, N> out = in;
        for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<uint8_t>(out[i] ^ static_cast<uint8_t>((SEED + i) & 0xFFu));
        for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<uint8_t>(~out[i]);
        for (std::size_t i = 0; i < N; ++i) out[i] = rotr8(out[i], 2);
        return out;
    }

    // --------- Layer 5 (ASCII-breaker, bijective over byte) ----------
    // Affine byte transform with per-index tweak. Strongly reduces accidental printable runs.
    //   enc[i] = ((val * 197u + 101u) ^ 0xA5u ^ (i * 139u)) & 0xFF
    // Inverse uses mul^-1 == 13 mod 256: val = ( ( (enc ^ 0xA5 ^ (i*139)) - 101 ) * 13 ) & 0xFF
    constexpr uint8_t mul197(uint8_t v) { return static_cast<uint8_t>((197u * v) & 0xFFu); }
    constexpr uint8_t mul197_inv(uint8_t v) { return static_cast<uint8_t>((13u * v) & 0xFFu); } // 197^-1 mod 256

    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer5_AsciiBreaker_Enc(const std::array<uint8_t, N>& in) {
        (void)SEED; // already mixed into previous layers; we use index-local tweak here
        std::array<uint8_t, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t v = in[i];
            uint8_t t = static_cast<uint8_t>((i * 139u) & 0xFFu);
            uint8_t e = static_cast<uint8_t>(mul197(v) + 101u);
            e = static_cast<uint8_t>((e ^ 0xA5u) ^ t);
            out[i] = e;
        }
        return out;
    }

    template <std::size_t N, uint32_t SEED>
    constexpr auto Layer5_AsciiBreaker_Dec(const std::array<uint8_t, N>& in) {
        (void)SEED;
        std::array<uint8_t, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t e = in[i];
            uint8_t t = static_cast<uint8_t>((i * 139u) & 0xFFu);
            uint8_t d = static_cast<uint8_t>((e ^ 0xA5u) ^ t);
            d = static_cast<uint8_t>(d - 101u);
            out[i] = mul197_inv(d);
        }
        return out;
    }

    // --------- Compile-time encryption (no pointers, all constexpr) ----------
    template <std::size_t N, uint32_t SEED>
    constexpr auto ObfuscateString(const char(&str)[N]) {
        constexpr uint32_t K = mix32(SEED * 0x9E3779B1u + static_cast<uint32_t>(N));
        const auto l1 = Layer1_XOR<N, K>(str);
        const auto l2 = Layer2_BitRotate<N, K>(l1);
        const auto l3 = Layer3_Shuffle<N, K>(l2);
        const auto l4 = Layer4_MultiPass<N, K>(l3);
        return Layer5_AsciiBreaker_Enc<N, K>(l4); // NEW final layer
    }

    // --------- Runtime decryption (needs the same SEED) ----------
    template <std::size_t N, uint32_t SEED>
    std::string DecryptString(const std::array<uint8_t, N>& enc) {
        constexpr uint32_t K = mix32(SEED * 0x9E3779B1u + static_cast<uint32_t>(N));

        // undo Layer5 first
        std::array<uint8_t, N> data = Layer5_AsciiBreaker_Dec<N, K>(enc);

        // undo Layer4
        for (auto& c : data) c = rotl8(c, 2);
        for (auto& c : data) c = static_cast<uint8_t>(~c);
        for (std::size_t i = 0; i < N; ++i) data[i] = static_cast<uint8_t>(data[i] ^ static_cast<uint8_t>((K + i) & 0xFFu));

        // undo Layer3
        for (auto& c : data) c = static_cast<uint8_t>((c ^ 42u) - 13u);
        std::array<std::size_t, N> idx{};
        for (std::size_t i = 0; i < N; ++i) idx[i] = i;
        if constexpr (N > 1) {
            for (std::size_t i = N - 1; i > 0; --i) {
                const std::size_t j = (static_cast<std::size_t>(K) * (i + 1)) % (i + 1);
                cswap(idx[i], idx[j]);
            }
        }
        std::array<uint8_t, N> unshuf{};
        for (std::size_t i = 0; i < N; ++i) unshuf[idx[i]] = data[i];
        data = unshuf;

        // undo Layer2
        const unsigned base = static_cast<unsigned>(K % 7u) + 1u;
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t c = data[i];
            c ^= (i % 2 == 0) ? uint8_t{ 0xAA } : uint8_t{ 0x55 };
            const unsigned r = (base + static_cast<unsigned>(i)) % 7u + 1u;
            data[i] = rotr8(c, r); // inverse of rotl
        }

        // undo Layer1
        std::string out; out.resize(N);
        const uint64_t key1 = (static_cast<uint64_t>(K) * 0x100000001B3ull) ^ 0xDEADBEEFull;
        const uint64_t key2 = (static_cast<uint64_t>(K) * 0x1000000001B3ull) ^ 0xCAFEBABEull;
        for (std::size_t i = 0; i < N; ++i) {
            uint8_t c = data[i];
            const unsigned s1 = static_cast<unsigned>((i * 8u) % 56u);
            const unsigned s2 = static_cast<unsigned>((i * 3u) % 56u);
            c ^= static_cast<uint8_t>((key2 >> s2) & 0xFFu);
            c ^= static_cast<uint8_t>((key1 >> s1) & 0xFFu);
            out[i] = static_cast<char>(c);
        }
        return out;
    }

    // --------- Holder stores SEED as template arg so decryption matches ----------
    template <std::size_t N, uint32_t SEED>
    class ObfuscatedString {
        std::array<uint8_t, N> encrypted_;
        mutable std::string decrypted_;
        mutable bool dec_ = false;

        void ensure() const {
            if (!dec_) { decrypted_ = DecryptString<N, SEED>(encrypted_); dec_ = true; }
        }
    public:
        constexpr explicit ObfuscatedString(const std::array<uint8_t, N>& enc) : encrypted_(enc) {}
        operator const std::string& () const { ensure(); return decrypted_; }
        const char* c_str() const { ensure(); return decrypted_.c_str(); }
        std::size_t length() const { ensure(); return decrypted_.length(); }
        friend std::ostream& operator<<(std::ostream& os, const ObfuscatedString& s) { return os << s.c_str(); }
        ~ObfuscatedString() { if (dec_) std::fill(decrypted_.begin(), decrypted_.end(), 0); }

        ObfuscatedString(const ObfuscatedString&) = delete;
        ObfuscatedString& operator=(const ObfuscatedString&) = delete;
    };

    // ---- Single-eval seed + macros ----
#ifdef __COUNTER__
#define OBF_UNIQUE_SEED ::StringObfuscator::mix32(static_cast<uint32_t>((__COUNTER__ * 1664525u) ^ static_cast<uint32_t>(__LINE__)))
#else
#define OBF_UNIQUE_SEED ::StringObfuscator::mix32(static_cast<uint32_t>(static_cast<uint32_t>(__LINE__) * 2654435761u))
#endif

#define OBF_MAKE_OBS(lit, SEED)                                                       \
    ([]() -> const ::StringObfuscator::ObfuscatedString<sizeof(lit), (SEED)>& {       \
        constexpr auto _enc = ::StringObfuscator::ObfuscateString<sizeof(lit), (SEED)>(lit); \
        static ::StringObfuscator::ObfuscatedString<sizeof(lit), (SEED)> _inst(_enc); \
        return _inst;                                                                  \
    }())

#define OBF_MAKE_OBS_STR(lit, SEED)                                                   \
    ([]() -> const std::string& {                                                     \
        constexpr auto _enc = ::StringObfuscator::ObfuscateString<sizeof(lit), (SEED)>(lit); \
        static ::StringObfuscator::ObfuscatedString<sizeof(lit), (SEED)> _inst(_enc); \
        return static_cast<const std::string&>(_inst);                                 \
    }())

#define OBF_MAKE_OBS_CSTR(lit, SEED)                                                  \
    ([]() -> const char* {                                                            \
        constexpr auto _enc = ::StringObfuscator::ObfuscateString<sizeof(lit), (SEED)>(lit); \
        static ::StringObfuscator::ObfuscatedString<sizeof(lit), (SEED)> _inst(_enc); \
        return _inst.c_str();                                                          \
    }())
} // namespace StringObfuscator

// ---- Narrow (existing)
#define OBS(lit)      OBF_MAKE_OBS(lit,      OBF_UNIQUE_SEED)
#define OBS_STR(lit)  OBF_MAKE_OBS_STR(lit,  OBF_UNIQUE_SEED)
#define OBS_CSTR(lit) OBF_MAKE_OBS_CSTR(lit, OBF_UNIQUE_SEED)

// ---- Additional literal kinds ----
#define OBS_U8(lit)   OBS(lit)
#define OBS_W(lit)    OBS(lit)
#define OBS_U16(lit)  OBS(lit)
#define OBS_U32(lit)  OBS(lit)

#define OBS_R(lit)    OBS(lit)
#define OBS_RU8(lit)  OBS_U8(lit)
#define OBS_RW(lit)   OBS_W(lit)
#define OBS_RU16(lit) OBS_U16(lit)
#define OBS_RU32(lit) OBS_U32(lit)
