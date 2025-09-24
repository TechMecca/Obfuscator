// Junk.h — highly-varied junk emitter (MSVC/Clang/GCC)
// - Each call site produces a different code *shape* and a *different amount* of junk.
// - Per-build variation via __TIME__/__DATE__/__FILE__ (already mixed in).
// - Includes a size-jitter pad so the final DLL/EXE size varies each build.
// - Works in Release; resists dead-code elimination with volatile + noinline.

#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>

// -------- Attributes --------
#if defined(_MSC_VER)
#define JNK_NOINLINE __declspec(noinline)
#else
#define JNK_NOINLINE __attribute__((noinline))
#endif

// Disable optimizations for the pattern functions (MSVC needs file-scope pragma)
#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

namespace junk_detail {

    // -------- tiny constexpr PRNGs / mixers --------
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

    // -------- compile-time seeds (per-build & per-TU) --------
    constexpr uint32_t fnv1a32_cstr(const char* s) {
        uint32_t h = 0x811C9DC5u;
        for (size_t i = 0; s[i] != '\0'; ++i) {
            h ^= static_cast<uint8_t>(s[i]);
            h *= 0x01000193u;
        }
        return h;
    }

    constexpr uint32_t time_seed() {
#ifdef __TIME__  // "hh:mm:ss"
        const uint32_t hh = static_cast<uint32_t>((__TIME__[0] - '0') * 10 + (__TIME__[1] - '0'));
        const uint32_t mm = static_cast<uint32_t>((__TIME__[3] - '0') * 10 + (__TIME__[4] - '0'));
        const uint32_t ss = static_cast<uint32_t>((__TIME__[6] - '0') * 10 + (__TIME__[7] - '0'));
        uint32_t t = hh * 3600u + mm * 60u + ss;     // 0..86399
        t ^= (t << 7); t ^= (t >> 11); t *= 2654435761u;
        return t;
#else
        return 0u;
#endif
    }

    constexpr uint32_t tu_seed() {
#ifdef __FILE__
        uint32_t h = fnv1a32_cstr(__FILE__);
#else
        uint32_t h = 0u;
#endif
#ifdef __DATE__
        h ^= fnv1a32_cstr(__DATE__);
#endif
        h ^= time_seed();
        // extra tiny mix
        h ^= (h << 13); h ^= (h >> 17); h ^= (h << 5);
        return h;
    }

    // -------- site seed: call-site + TU/build salt --------
    template<int Line, int Ctr>
    struct site_seed {
        static constexpr uint32_t value =
            mix32(static_cast<uint32_t>(Line * 1664525u) ^
                static_cast<uint32_t>(Ctr * 1013904223u) ^
                tu_seed());
    };

    template <typename T>
    JNK_NOINLINE inline void keep(volatile T& v) { (void)v; }

    // -------- Pattern pieces (small, different-looking blocks) --------
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

    // Compile-time (template) variant — use only when K is a constant expression
    template<int K> JNK_NOINLINE inline void p_structs() {
        struct S { int a; unsigned b; short c; unsigned char d; };
        volatile S s = { static_cast<int>(0x12345678u ^ K), static_cast<unsigned>(0x9E3779B9u * (K + 1)),
                         static_cast<short>((K * 73) & 0x7FFF), static_cast<unsigned char>((K * 37) & 0xFF) };
        s.a ^= (s.a << 5); s.b += 0x7F4A7C15u; s.c ^= static_cast<short>(s.b); s.d += static_cast<unsigned char>(s.c);
        keep(s.a); keep(s.b); keep(s.c); keep(s.d);
    }

    // Runtime variant — for when K is not a constant expression
    JNK_NOINLINE inline void p_structs_rt(int K) {
        struct S { int a; unsigned b; short c; unsigned char d; };
        volatile S s = { static_cast<int>(0x12345678u ^ K), static_cast<unsigned>(0x9E3779B9u * (K + 1)),
                         static_cast<short>((K * 73) & 0x7FFF), static_cast<unsigned char>((K * 37) & 0xFF) };
        s.a ^= (s.a << 5); s.b += 0x7F4A7C15u; s.c ^= static_cast<short>(s.b); s.d += static_cast<unsigned char>(s.c);
        keep(s.a); keep(s.b); keep(s.c); keep(s.d);
    }

    // -------- core emitter building block --------
    JNK_NOINLINE inline void emit_core(uint32_t S, int r0, int r1, uint32_t sel) {
        switch (sel & 7u) {
        case 0: p_mix_u32(S ^ 0x11111111u, r0 + 2); break;
        case 1: p_arith_int(S ^ 0x22222222u, r1);   break;
        case 2: p_fp_mix(S ^ 0x33333333u, r0 + r1); break;
        case 3: p_small_vec(S ^ 0x44444444u);       break;
        case 4: p_ptr_jiggle(S ^ 0x55555555u);      break;
        case 5: p_structs_rt(static_cast<int>(0x155 ^ ((S >> 10) & 0x3FF))); break; // runtime K
        case 6: p_structs_rt(static_cast<int>(0x2AA ^ ((S >> 11) & 0x7FF))); break; // runtime K
        default: p_mix_u32(S ^ 0x66666666u, r0);   break;
        }
    }

    // -------- Dispatcher: varies shape *and amount* per site/build --------
    template<int Line, int Ctr>
    JNK_NOINLINE inline void emit() {
        constexpr uint32_t S0 = site_seed<Line, Ctr>::value;
        constexpr uint32_t S1 = mix32(S0 ^ 0x85EBCA6Bu);
        constexpr uint32_t S2 = mix32(S1 ^ 0xC2B2AE35u);

        // Variable work amounts (compile-time constants derived from seeds)
        constexpr int r0_base = 1 + static_cast<int>((S0 >> 25) & 7); // 1..8
        constexpr int r1_base = 2 + static_cast<int>((S1 >> 26) & 7); // 2..9
        constexpr int repeats = 1 + static_cast<int>((S2 >> 28) & 3); // 1..4
        constexpr uint32_t sec_mask = ((S0 ^ (S1 << 1) ^ (S2 << 2)) | 0x1u); // ensure >=1 secondary

        for (int i = 0; i < repeats; ++i) {
            const uint32_t Si = mix32(S0 + static_cast<uint32_t>(i) * 0x9E3779B9u);
            const int r0 = r0_base + static_cast<int>((Si >> 21) & 3); // jitter
            const int r1 = r1_base + static_cast<int>((Si >> 23) & 3);
            emit_core(Si, r0, r1, (S0 ^ S1 ^ S2 ^ (Si << 3)));

            if (sec_mask & (1u << (i & 7)))        p_small_vec(Si ^ 0xA5A5A5A5u);
            if (sec_mask & (1u << ((i + 3) & 7)))    p_ptr_jiggle(Si ^ 0x7F4A7C15u);
            if (sec_mask & (1u << ((i + 5) & 7)))    p_fp_mix(Si ^ 0xC3ECEB5Du, 1 + (Si & 3));
        }
    }

    // Heavier variant (stacks more blocks based on seed)
    template<int Line, int Ctr>
    JNK_NOINLINE inline void emit_heavy() {
        constexpr uint32_t Sx = site_seed<Line, Ctr>::value;
        constexpr int extra = 1 + static_cast<int>(((Sx >> 22) & 7)); // 1..8 extra cores
        emit<Line, Ctr>();
        for (int k = 0; k < extra; ++k) {
            const uint32_t Sk = mix32(Sx + static_cast<uint32_t>(k) * 0x27D4EB2Du);
            const int r0 = 1 + static_cast<int>((Sk >> 20) & 7);
            const int r1 = 2 + static_cast<int>((Sk >> 23) & 7);
            emit_core(Sk, r0, r1, (Sk ^ (Sx << 1) ^ 0xDEADBEEFu));
            if (Sk & 0x00004000u) p_arith_int(Sk ^ 0x12345678u, 2 + static_cast<int>((Sk >> 17) & 3));
        }
    }

} // namespace junk_detail

// Re-enable optimizations after the pattern functions (MSVC only)
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

// -------- Public macros --------
// Unique per call site via __LINE__/__COUNTER__, and per build/TU via __TIME__/__DATE__/__FILE__.
// Each build → different binary bytes (hash) as long as reproducible-build modes don't fix time/date.
#define JUNK_CODE_BLOCK()          ::junk_detail::emit<__LINE__, (__COUNTER__ & 0x3FFF)>()
#define JUNK_CODE_BLOCK_ADVANCED() ::junk_detail::emit_heavy<__LINE__, ((__COUNTER__ + 11) & 0x3FFF)>()

// -------- Size-jitter pad (ensures DLL/EXE size changes across builds) --------
// Purpose: even if code-size stays in the same PE alignment bucket, vary file size by
// emitting a kept, read-only blob whose length depends on the per-build/TU seed.

namespace junk_detail {
    constexpr size_t RJUNK_SZ = 128u + (tu_seed() % 1536u); // 128..1663 bytes
}

#if defined(_MSC_VER)
// Create a custom read-only section and place our pad in it.
#pragma section(".rjunk", read)

// Give the object **external linkage** so selectany is valid.
extern __declspec(allocate(".rjunk")) __declspec(selectany)
const unsigned char g_rjunk_pad_msvc[junk_detail::RJUNK_SZ] = { 1 };

// Anchor symbol to prevent /OPT:REF from discarding the section.
extern "C" __declspec(selectany) int rjunk_anchor = 0;
#if defined(_M_IX86)
#pragma comment(linker, "/include:_rjunk_anchor")
#else
#pragma comment(linker, "/include:rjunk_anchor")
#endif
#else
__attribute__((section(".rjunk,\"a\""), used, aligned(16)))
const unsigned char g_rjunk_pad_gcc[junk_detail::RJUNK_SZ] = { 1 };
#endif
