/*
 * Signalforge Container Extension
 * src/simd.h - SIMD intrinsics abstraction layer
 *
 * Provides a unified interface for SIMD operations across different platforms:
 * - x86_64: SSE2 (universally available since ~2003)
 * - ARM64: NEON (Apple Silicon, ARM servers)
 * - Fallback: Scalar implementation for unsupported platforms
 */

#ifndef SF_SIMD_H
#define SF_SIMD_H

#include <stdint.h>
#include <string.h>

/* Platform detection and SIMD availability */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || _M_IX86_FP >= 2))
        #include <emmintrin.h>
        #define SF_HAS_SIMD 1
        #define SF_SIMD_WIDTH 16
        #define SF_SIMD_PLATFORM "SSE2"
    #else
        #define SF_HAS_SIMD 0
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(__ARM_NEON) || defined(__ARM_NEON__)
        #include <arm_neon.h>
        #define SF_HAS_SIMD 1
        #define SF_SIMD_WIDTH 16
        #define SF_SIMD_PLATFORM "NEON"
    #else
        #define SF_HAS_SIMD 0
    #endif
#else
    #define SF_HAS_SIMD 0
#endif

#ifndef SF_SIMD_PLATFORM
    #define SF_SIMD_PLATFORM "Scalar"
#endif

/* ============================================================================
 * SIMD Vector Types
 * ============================================================================ */

#if SF_HAS_SIMD

#if defined(__SSE2__)
    /* SSE2 types */
    typedef __m128i sf_simd_i32x4;
    typedef __m128i sf_simd_i8x16;
#elif defined(__ARM_NEON)
    /* NEON types */
    typedef uint32x4_t sf_simd_i32x4;
    typedef uint8x16_t sf_simd_i8x16;
#endif

#else
    /* Scalar fallback types */
    typedef struct { uint32_t v[4]; } sf_simd_i32x4;
    typedef struct { uint8_t v[16]; } sf_simd_i8x16;
#endif

/* ============================================================================
 * SIMD Operations - 32-bit integers (for hash comparison)
 * ============================================================================ */

/**
 * Load 4 aligned 32-bit integers into a SIMD register
 */
static inline sf_simd_i32x4 sf_simd_load_i32x4(const uint32_t *ptr)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_load_si128((const __m128i *)ptr);
    #elif defined(__ARM_NEON)
        return vld1q_u32(ptr);
    #endif
#else
    sf_simd_i32x4 result;
    memcpy(result.v, ptr, sizeof(result.v));
    return result;
#endif
}

/**
 * Load 4 unaligned 32-bit integers into a SIMD register
 */
static inline sf_simd_i32x4 sf_simd_loadu_i32x4(const uint32_t *ptr)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_loadu_si128((const __m128i *)ptr);
    #elif defined(__ARM_NEON)
        return vld1q_u32(ptr);
    #endif
#else
    sf_simd_i32x4 result;
    memcpy(result.v, ptr, sizeof(result.v));
    return result;
#endif
}

/**
 * Set all 4 lanes to the same 32-bit value
 */
static inline sf_simd_i32x4 sf_simd_set1_i32(uint32_t value)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_set1_epi32((int32_t)value);
    #elif defined(__ARM_NEON)
        return vdupq_n_u32(value);
    #endif
#else
    sf_simd_i32x4 result;
    result.v[0] = result.v[1] = result.v[2] = result.v[3] = value;
    return result;
#endif
}

/**
 * Compare 4 pairs of 32-bit integers for equality
 * Returns a vector where each lane is all 1s if equal, all 0s if not
 */
static inline sf_simd_i32x4 sf_simd_cmpeq_i32(sf_simd_i32x4 a, sf_simd_i32x4 b)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_cmpeq_epi32(a, b);
    #elif defined(__ARM_NEON)
        return vceqq_u32(a, b);
    #endif
#else
    sf_simd_i32x4 result;
    for (int i = 0; i < 4; i++) {
        result.v[i] = (a.v[i] == b.v[i]) ? 0xFFFFFFFF : 0;
    }
    return result;
#endif
}

/**
 * Create a bitmask from the most significant bit of each byte
 * Used to quickly check if any comparison matched
 */
static inline uint32_t sf_simd_movemask_i32(sf_simd_i32x4 vec)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        /* SSE2: movemask on bytes, then extract relevant bits */
        return (uint32_t)_mm_movemask_epi8(vec);
    #elif defined(__ARM_NEON)
        /* NEON: shrink to 8-bit and extract bits */
        uint8x16_t narrow = vreinterpretq_u8_u32(vec);
        uint64_t low = vget_lane_u64(vreinterpret_u64_u8(vget_low_u8(narrow)), 0);
        uint64_t high = vget_lane_u64(vreinterpret_u64_u8(vget_high_u8(narrow)), 0);
        /* Extract MSB from each 32-bit lane */
        uint32_t mask = 0;
        if (low & 0x80000000ULL) mask |= 0x000F;
        if (low & 0x8000000000000000ULL) mask |= 0x00F0;
        if (high & 0x80000000ULL) mask |= 0x0F00;
        if (high & 0x8000000000000000ULL) mask |= 0xF000;
        return mask;
    #endif
#else
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        if (vec.v[i] & 0x80000000) {
            mask |= (0xF << (i * 4));
        }
    }
    return mask;
#endif
}

/* ============================================================================
 * SIMD Operations - 8-bit integers (for Swiss Table control bytes)
 * ============================================================================ */

/**
 * Load 16 aligned 8-bit integers into a SIMD register
 */
static inline sf_simd_i8x16 sf_simd_load_i8x16(const uint8_t *ptr)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_load_si128((const __m128i *)ptr);
    #elif defined(__ARM_NEON)
        return vld1q_u8(ptr);
    #endif
#else
    sf_simd_i8x16 result;
    memcpy(result.v, ptr, sizeof(result.v));
    return result;
#endif
}

/**
 * Set all 16 lanes to the same 8-bit value
 */
static inline sf_simd_i8x16 sf_simd_set1_i8(uint8_t value)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_set1_epi8((char)value);
    #elif defined(__ARM_NEON)
        return vdupq_n_u8(value);
    #endif
#else
    sf_simd_i8x16 result;
    for (int i = 0; i < 16; i++) {
        result.v[i] = value;
    }
    return result;
#endif
}

/**
 * Compare 16 pairs of 8-bit integers for equality
 */
static inline sf_simd_i8x16 sf_simd_cmpeq_i8(sf_simd_i8x16 a, sf_simd_i8x16 b)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return _mm_cmpeq_epi8(a, b);
    #elif defined(__ARM_NEON)
        return vceqq_u8(a, b);
    #endif
#else
    sf_simd_i8x16 result;
    for (int i = 0; i < 16; i++) {
        result.v[i] = (a.v[i] == b.v[i]) ? 0xFF : 0;
    }
    return result;
#endif
}

/**
 * Create a bitmask from the most significant bit of each byte
 */
static inline uint32_t sf_simd_movemask_i8(sf_simd_i8x16 vec)
{
#if SF_HAS_SIMD
    #if defined(__SSE2__)
        return (uint32_t)_mm_movemask_epi8(vec);
    #elif defined(__ARM_NEON)
        /* NEON doesn't have direct movemask, so we extract bits manually */
        static const uint8_t shift_arr[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        uint8x16_t shift = vld1q_u8(shift_arr);
        uint8x16_t masked = vshrq_n_u8(vec, 7);
        uint16x8_t shifted = vshlq_u16(vreinterpretq_u16_u8(masked), vreinterpretq_s16_u8(shift));
        return vgetq_lane_u16(shifted, 0) | (vgetq_lane_u16(shifted, 1) << 1) |
               (vgetq_lane_u16(shifted, 2) << 2) | (vgetq_lane_u16(shifted, 3) << 3) |
               (vgetq_lane_u16(shifted, 4) << 4) | (vgetq_lane_u16(shifted, 5) << 5) |
               (vgetq_lane_u16(shifted, 6) << 6) | (vgetq_lane_u16(shifted, 7) << 7);
    #endif
#else
    uint32_t mask = 0;
    for (int i = 0; i < 16; i++) {
        if (vec.v[i] & 0x80) {
            mask |= (1U << i);
        }
    }
    return mask;
#endif
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Check if any lane in a 32-bit comparison result is non-zero
 */
static inline int sf_simd_any_match_i32(sf_simd_i32x4 cmp_result)
{
    uint32_t mask = sf_simd_movemask_i32(cmp_result);
    return mask != 0;
}

/**
 * Check if any lane in an 8-bit comparison result is non-zero
 */
static inline int sf_simd_any_match_i8(sf_simd_i8x16 cmp_result)
{
    uint32_t mask = sf_simd_movemask_i8(cmp_result);
    return mask != 0;
}

/**
 * Count trailing zeros in a bitmask (for finding first match)
 */
static inline int sf_simd_ctz(uint32_t mask)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(mask);
#elif defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, mask);
    return (int)index;
#else
    /* Fallback */
    int count = 0;
    if (mask == 0) return 32;
    while ((mask & 1) == 0) {
        mask >>= 1;
        count++;
    }
    return count;
#endif
}

#endif /* SF_SIMD_H */
