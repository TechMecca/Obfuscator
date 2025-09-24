#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>

#if defined(_MSC_VER)
#define JNK_NOINLINE __declspec(noinline)
#else
#define JNK_NOINLINE __attribute__((noinline))
#endif

// Disable optimizations for all pattern functions (MSVC requires this at file scope)
#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

namespace junk_detail {

    // ---- tiny, constexpr PRNGs (no tables, no strings) ----
    constexpr uint32_t rotl(uint32_t x, unsigned r) { return (x << (r & 31)) | (x >> ((32 - r) & 31)); }
    constexpr uint32_t rotr(uint32_t x, unsigned r) { return (x >> (r & 31)) | (x << ((32 - r) & 31)); }

    constexpr uint32_t xorshift32(uint32_t x) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x;
    }

    // Weyl-ish mix
    constexpr uint32_t mix32(uint32_t x) {
        x ^= 0x9E3779B9u; x = xorshift32(x + 0x85EBCA6Bu);
        x = rotl(x ^ 0xC2B2AE35u, 17) * 0x27D4EB2Du;
        return x ^ rotr(x, 15);
    }

    // Compile-time unique seed from line & counter only (no strings)
    template<int Line, int Ctr>
    struct site_seed {
        static constexpr uint32_t value =
            mix32(static_cast<uint32_t>(Line * 1664525u) ^ static_cast<uint32_t>(Ctr * 1013904223u));
    };

    template <typename T>
    JNK_NOINLINE inline void keep(volatile T& v) { (void)v; }

    // ---- Pattern pieces (small, different-looking blocks) ----
    // All pattern functions are now automatically unoptimized in MSVC
    JNK_NOINLINE inline void p_mix_u32(uint32_t s, int rounds) {
        volatile uint32_t a = s ^ 0xA5A5A5A5u;
        volatile uint32_t b = s + 0x7F4A7C15u;
        for (int i = 0; i < rounds; ++i) {
            a = mix32(a + static_cast<uint32_t>(i * 2654435761u));
            b = xorshift32(b ^ a ^ static_cast<uint32_t>(i * 1013904223u));
            if ((i & 1) == 0) a ^= rotr(b, (i & 31));
            else              b ^= rotl(a, ((i * 3) & 31));
        }
        keep(a); keep(b);
    }

    JNK_NOINLINE inline void p_arith_int(uint32_t s, int n) {
        volatile int x = static_cast<int>(s ^ 0xDEADBEEFu);
        for (int i = 0; i < n; ++i) {
            x ^= (x << 7);
            x += static_cast<int>(0x9E3779B9u + (s ^ static_cast<uint32_t>(i)));
            x ^= (x >> 13);
            x *= 0x10001 + (i & 3);
        }
        keep(x);
    }

    JNK_NOINLINE inline void p_fp_mix(uint32_t s, int n) {
        volatile float  f = (static_cast<int>(s) & 0x7FFF) * 1.0009765625f; // ~/1024
        volatile double d = (static_cast<int>(rotl(s, 9)) & 0xFFFF) * 0.0001220703125; // ~/8192
        for (int i = 0; i < n; ++i) {
            f = f * (1.0f + ((s >> (i & 7)) & 7) * 0.03125f) - 0.0625f;
            d = d + (((s >> ((i + 3) & 7)) & 15) * 0.0078125) - 0.00390625;
            if (i & 1) f = f * 1.41421356f - 0.70710678f;
            else       d = d * 1.7320508075688772 - 0.5773502691896258;
        }
        keep(f); keep(d);
    }

    JNK_NOINLINE inline void p_small_vec(uint32_t s) {
        volatile uint32_t v[4] = {
            mix32(s + 0x100u), mix32(s + 0x200u), mix32(s + 0x300u), mix32(s + 0x400u)
        };
        // tiny shuffle + xor cascade
        for (int i = 0; i < 7; ++i) {
            int a = (s + i) & 3, b = ((s >> (i & 3)) + i) & 3;
            uint32_t t = v[a]; v[a] = v[b]; v[b] = t;
            v[a] ^= rotl(v[b], (i * 5) & 31);
            v[b] += 0x9E3779B9u ^ static_cast<uint32_t>(i * 2654435761u);
        }
        keep(v[0]); keep(v[1]); keep(v[2]); keep(v[3]);
    }

    JNK_NOINLINE inline void p_ptr_jiggle(uint32_t s) {
        // simulate pointer math without touching real memory
        alignas(16) volatile uint8_t scratch[32] = {};
        volatile uintptr_t p = reinterpret_cast<uintptr_t>(&scratch[0]) ^ (static_cast<uintptr_t>(s) << 1);
        volatile uintptr_t q = reinterpret_cast<uintptr_t>(&scratch[16]) ^ (static_cast<uintptr_t>(rotl(s, 7)) << 2);
        volatile ptrdiff_t d = static_cast<ptrdiff_t>(q - p);
        d ^= static_cast<ptrdiff_t>(rotl(static_cast<uint32_t>(d), (s & 7)));
        keep(p); keep(q); keep(d);
    }

    template<int K> JNK_NOINLINE inline void p_structs() {
        struct S { int a; unsigned b; short c; unsigned char d; };
        volatile S s = { static_cast<int>(0x12345678u ^ K), static_cast<unsigned>(0x9E3779B9u * (K + 1)),
                     static_cast<short>((K * 73) & 0x7FFF), static_cast<unsigned char>((K * 37) & 0xFF) };
        s.a ^= (s.a << 5); s.b += 0x7F4A7C15u; s.c ^= static_cast<short>(s.b); s.d += static_cast<unsigned char>(s.c);
        keep(s.a); keep(s.b); keep(s.c); keep(s.d);
    }

    // ---- Dispatcher that *varies shape* per site ----
    // Note: emit functions are NOT unoptimized - they should be inlined/optimized normally
    template<int Line, int Ctr>
    JNK_NOINLINE inline void emit() {
        constexpr uint32_t S0 = site_seed<Line, Ctr>::value;
        constexpr uint32_t S1 = mix32(S0 ^ 0x85EBCA6Bu);
        constexpr uint32_t S2 = mix32(S1 ^ 0xC2B2AE35u);

        const int r0 = 1 + static_cast<int>((S0 >> 27) & 3);  // 1..4
        const int r1 = 2 + static_cast<int>((S1 >> 29) & 5);  // 2..7

        // Choose a different primary pattern
        switch ((S0 ^ S1 ^ S2) & 7) {
        case 0: p_mix_u32(S0, r0 + 2); break;
        case 1: p_arith_int(S1, r1);   break;
        case 2: p_fp_mix(S2, r0 + r1); break;
        case 3: p_small_vec(S1);       break;
        case 4: p_ptr_jiggle(S2);      break;
        case 5: p_structs<(S0 & 0x3FF) ^ 0x155>(); break;
        case 6: p_structs<(S1 & 0x7FF) ^ 0x2AA>(); break;
        default: p_mix_u32(S2, r0);    break;
        }

        // Optional secondaries decided by seed bits (affects code layout)
        if (S0 & 0x00020000u) p_small_vec(S0 ^ 0x11111111u);
        if (S1 & 0x00008000u) p_ptr_jiggle(S1 ^ 0x22222222u);
        if (S2 & 0x00001000u) p_fp_mix(S2 ^ 0x33333333u, (S0 & 3) + 1);
    }

    // A bit heavier pattern selection
    template<int Line, int Ctr>
    JNK_NOINLINE inline void emit_heavy() {
        emit<Line, Ctr>();
        emit<Line + 7, Ctr + 3>();
        if (((Line * 13) ^ (Ctr * 17)) & 1) p_arith_int(mix32(Line ^ (Ctr << 1)), 3);
        else                                p_mix_u32(mix32(Ctr ^ (Line << 2)), 4);
    }

} // namespace junk_detail

// Re-enable optimizations after the pattern functions (MSVC only)
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

// ---- Public macros -----------------------------------------------------------
// Unique per call site via __LINE__ and __COUNTER__ (no strings involved).
#define JUNK_CODE_BLOCK()            ::junk_detail::emit<__LINE__, (__COUNTER__ & 0x3FFF)>()
#define JUNK_CODE_BLOCK_ADVANCED()   ::junk_detail::emit_heavy<__LINE__, ((__COUNTER__ + 11) & 0x3FFF)>()