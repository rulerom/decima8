/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm.hpp"
#include <cstring>
#include <chrono>
#include "d8/logger.hpp"
#include <spdlog/spdlog.h>

// SIMD support
// For MSVC x64: SSE4.2 is always available, but headers need to be explicitly included
#if defined(_MSC_VER)
    #include <intrin.h>
    // For MSVC x64, SSE4.2 support is always available
    #if defined(_M_X64) || defined(_M_AMD64) || defined(_WIN64)
        #define D8_SIMD_SSE4_2 1
    #endif
    #if defined(__AVX512F__) || defined(_M_AVX512F)
        #define D8_SIMD_AVX512 1
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
    // Check support via compiler macros
    #if defined(__SSE4_2__) || (defined(__x86_64__) && !defined(__APPLE__))
        #define D8_SIMD_SSE4_2 1
    #endif
    #if defined(__AVX512F__)
        #define D8_SIMD_AVX512 1
    #endif
#endif

// Diagnostics: ensure SIMD is actually enabled
#if defined(D8_SIMD_SSE4_2)
    // Check SSE4.2 intrinsics availability
    #if !defined(_MSC_VER) && !defined(__SSE4_2__)
        #error "D8_SIMD_SSE4_2 defined but SSE4.2 intrinsics not available"
    #endif
#endif

//#define D8_DEBUG_CORE 1

namespace d8 {

    // Helper inline method for core logging (only if D8_DEBUG_CORE is defined)
    template<typename... Args>
    static inline void core_log_info(spdlog::format_string_t<Args...> fmt, Args&&... args) noexcept {
#ifdef D8_DEBUG_CORE
        spdlog::info(fmt, std::forward<Args>(args)...);
#else
        (void)fmt;
        ((void)args, ...); // Suppress unused variable warnings
#endif
    }
    
    template<typename... Args>
    static inline void core_log_warn(spdlog::format_string_t<Args...> fmt, Args&&... args) noexcept {
#ifdef D8_DEBUG_CORE
        spdlog::warn(fmt, std::forward<Args>(args)...);
#else
        (void)fmt;
        ((void)args, ...); // Suppress unused variable warnings
#endif
    }

    static inline void bitset_set(std::span<u8> bits, std::size_t bit_index) noexcept {
        const std::size_t byte = bit_index >> 3;
        const std::size_t off = bit_index & 7;
        bits[byte] |= u8(1u << off);
    }
    
    // Branchless bitset_set_if: set bit only if condition is true
    static inline void bitset_set_if(std::span<u8> bits, std::size_t bit_index, bool condition) noexcept {
        const std::size_t byte = bit_index >> 3;
        const std::size_t off = bit_index & 7;
        bits[byte] |= u8((condition ? 1u : 0u) << off);
    }

    static inline void compute_in16_reference(u8* out_in16, std::span<u8> clip_bits, std::size_t base_bit_idx,
        const std::array<u8, kLanes>& vsb_ingress16, const std::array<u8, kLanes>& /*bus16*/,
        bool is_active) noexcept {
        if (!is_active) {
            for (std::size_t i = 0; i < 8; ++i) {
                out_in16[i] = 0;
            }
            return;
        }
        for (std::size_t i = 0; i < 8; ++i) {
            const u8 raw = vsb_ingress16[i];
            out_in16[i] = clamp15_u8(static_cast<int>(raw));
            if (raw > 15) bitset_set(clip_bits, base_bit_idx + i);
        }
    }

    static inline void compute_row_raw_reference(i16* out_row_raw, const u8* in16,
        const std::array<u8, kTileCount * 64>& w_mag, const std::array<u8, kTileCount * 64>& w_sign,
        std::size_t tile_id) noexcept {
        for (std::size_t r = 0; r < 8; ++r) {
            const std::size_t row_offset = tile_id * 64 + r * 8;
            i16 sum = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                const u8 in_val = in16[i];
                const u8 mag_val = w_mag[row_offset + i];
                const u8 sign_val = w_sign[row_offset + i];
                // Direct multiplication: in_val * mag_val (without dividing by 15)
                // IMPORTANT: If in_val == 0, the result must be 0 regardless of the sign
                // Zero input -> zero contribution (do not multiply by weight!)
                if (in_val == 0) {
                    continue;
                }
                // Apply weight only if input is NOT zero
                const i16 mul_result = sign_val ? static_cast<i16>(in_val * mag_val) : static_cast<i16>(-static_cast<i16>(in_val * mag_val));
                sum += mul_result;
            }
            out_row_raw[r] = sum;
        }
    }

    static inline i16 compute_delta_raw_reference(const i16* row_signed) noexcept {
        i16 delta_raw = 0;
        for (std::size_t r = 0; r < 8; ++r) {
            delta_raw += row_signed[r];  // row_signed[r] = row_raw[r] (unrestricted)
        }
        return delta_raw;
    }

    static inline u8 thr_norm_4bit(i16 thr_cur, i16 thr_lo, i16 thr_hi) noexcept {
        // normalized -8..+7 in 4 bits for visualization
        // Encoding: 0..7 = positive (0..+7), 8..15 = negative (-8..-1)
        // Normalization to range [thr_lo..thr_hi] (v0.2)
        const i32 abs_lo = (thr_lo < 0) ? -static_cast<i32>(thr_lo) : static_cast<i32>(thr_lo);
        const i32 abs_hi = (thr_hi < 0) ? -static_cast<i32>(thr_hi) : static_cast<i32>(thr_hi);
        const i32 scale = (std::max)(abs_lo, abs_hi);
        if (scale <= 0) {
            return 15; // Visual convention: 0 displays as 15
        }
        if (thr_cur < 0) {
            const i32 neg_value = -static_cast<i32>(thr_cur);
            const i32 clamped_neg = (std::min)(neg_value, scale);
            const i32 norm = 8 + (clamped_neg * 7 + scale / 2) / scale; // 8..15
            return static_cast<u8>((std::min)(15, (std::max)(8, norm)));
        }
        const i32 pos_value = static_cast<i32>(thr_cur);
        const i32 clamped_pos = (std::min)(pos_value, scale);
        const i32 norm = (clamped_pos * 7 + scale / 2) / scale; // 0..7
        return static_cast<u8>((std::min)(7, (std::max)(0, norm)));
    }

    // SIMD Helper functions for optimizing critical paths
#if defined(D8_SIMD_SSE4_2)
    #if defined(_MSC_VER)
        // MSVC includes everything via intrin.h (already included above)
        // For MSVC, check that SSE4.2 intrinsics are available
        #ifndef _M_X64
            #error "SIMD optimizations require x64 build"
        #endif
    #else
        #include <smmintrin.h>  // SSE4.2 for GCC/Clang
    #endif

    // Sum 8 i16 elements (for delta_raw)
    // SIMD version: uses _mm_hadd_epi16 (SSE3/SSE4.1) for horizontal addition
    static inline i16 simd_sum8_i16(const i16* arr) noexcept {
        // Load 8 i16 elements (128 bits = 16 bytes = 8 x i16)
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(arr));
        // Horizontal addition: hadd pairs of adjacent elements
        v = _mm_hadd_epi16(v, v);  // [a0+a1, a2+a3, a4+a5, a6+a7, a0+a1, a2+a3, a4+a5, a6+a7]
        v = _mm_hadd_epi16(v, v);  // [sum(0..3), sum(4..7), ...]
        v = _mm_hadd_epi16(v, v);  // [sum(0..7), ...]
        // Extract lowest 32-bit element and convert to i16
        return static_cast<i16>(_mm_cvtsi128_si32(v));
    }

    // Conditional copy 8 bytes (for drive selection)
    static inline void simd_blend8_u8(u8* dst, const u8* a, const u8* b, u8 mask) noexcept {
        __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
        __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
        // Create mask for blendv: if locked_after==1, take a, otherwise b
        // mask is repeated 16 times for each byte
        __m128i mask_vec = _mm_set1_epi8(static_cast<char>(mask ? -1 : 0));
        __m128i result = _mm_blendv_epi8(vb, va, mask_vec);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), result);
    }

    // Clamp15 for 8 u32 elements with clip mask generation
    // IMPORTANT: use only SSE4.2 (not AVX2), so _mm_min_epu32 is not available
    // Use SSE4.2 compatible approach: compare + blend
    static inline void simd_clamp15_8_u32(u8* dst, u8* clip_mask, const u32* src) noexcept {
        // Load 8 u32 (4 at a time)
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 4));

        // Clamp to 15: min(v, 15) - use SSE4.2 compatible approach
        // In SSE4.2 there's no _mm_min_epu32 (that's AVX2), use compare + blend
        __m128i max_val = _mm_set1_epi32(15);

        // Generate clip mask (src > 15) - compare original values
        __m128i clip0 = _mm_cmpgt_epi32(v0, max_val);
        __m128i clip1 = _mm_cmpgt_epi32(v1, max_val);

        // Clamp: if v > 15, use 15, otherwise v
        __m128i clamped0 = _mm_blendv_epi8(v0, max_val, clip0);  // SSE4.1: blendv
        __m128i clamped1 = _mm_blendv_epi8(v1, max_val, clip1);

        // Pack clamped values to u8: packus_epi32 does unsigned saturate (SSE4.1)
        __m128i packed = _mm_packus_epi32(clamped0, clamped1);  // Pack to u16 (saturate at 65535)
        packed = _mm_packus_epi16(packed, packed);  // Pack to u8 (saturate at 255, but our values <= 15)
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst), packed);

        // Assemble clip mask into one byte (8 bits for 8 channels)
        // Convert compare masks to bits
        __m128i clip_packed = _mm_packs_epi32(clip0, clip1);  // Pack to i16
        clip_packed = _mm_packs_epi16(clip_packed, clip_packed);  // Pack to i8
        int clip_bits = _mm_movemask_epi8(clip_packed);
        *clip_mask = static_cast<u8>(clip_bits) & 0xFF;  // Only lowest 8 bits
    }

    // in16[8] computation: VSB ingress when ACTIVE, clamp15
    // SSE4.2: process 8 bytes in parallel
    static inline void simd_compute_in16(u8* dst, std::span<u8> clip_bits, std::size_t base_bit_idx,
                                        const u8* vsb_ingress, const u8* /*bus16*/,
                                        bool is_active) noexcept {
        if (!is_active) {
            for (std::size_t i = 0; i < 8; ++i) {
                dst[i] = 0;
            }
            return;
        }
        // Load vsb_ingress
        // IMPORTANT: use _mm_loadl_epi64 to load only 8 bytes (64 bits), not 16
        // This prevents reading garbage beyond array bounds
        // After load, explicitly zero upper 64 bits since _mm_loadl_epi64 doesn't touch them
        __m128i vsb_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(vsb_ingress));  // Loads only 64 bits (8 bytes) into lowest 64 bits
        vsb_vec = _mm_unpacklo_epi64(vsb_vec, _mm_setzero_si128());  // Zero upper 64 bits
        // raw = vsb
        __m128i raw_16 = vsb_vec;
        // Expand to i16 for clamp15
        __m128i raw_lo = _mm_unpacklo_epi8(raw_16, _mm_setzero_si128());  // Low 8 -> 8 i16
        __m128i raw_hi = _mm_unpackhi_epi8(raw_16, _mm_setzero_si128());  // High 8 -> 8 i16

        // Clamp15: min(raw, 15) - use pminsuw (SSE4.1)
        __m128i max_val = _mm_set1_epi16(15);
        __m128i clamped_lo = _mm_min_epu16(raw_lo, max_val);
        __m128i clamped_hi = _mm_min_epu16(raw_hi, max_val);

        // Pack back to u8
        __m128i clamped = _mm_packus_epi16(clamped_lo, clamped_hi);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(dst), clamped);

        // Generate clip mask (raw > 15)
        __m128i clip_lo = _mm_cmpgt_epi16(raw_lo, max_val);
        __m128i clip_hi = _mm_cmpgt_epi16(raw_hi, max_val);
        __m128i clip_packed = _mm_packs_epi16(clip_lo, clip_hi);  // Pack to i8
        int clip_mask = _mm_movemask_epi8(clip_packed);

        // Set bits in bitset
        for (int bit = 0; bit < 8; ++bit) {
            if (clip_mask & (1 << bit)) {
                bitset_set(clip_bits, base_bit_idx + bit);
            }
        }
    }

    // Matrix multiplication: row_raw[8] = sum(in16[i] * weight[i]) for each row
    // SSE4.1: simple multiplication without division by 15 (iron logic)
    // For each row: compute dot product in16[8] * weights[8]
    static inline void simd_compute_row_raw(i16* row_raw, const u8* in16,
                                           const u8* w_mag, const u8* w_sign,
                                           std::size_t tile_base_offset) noexcept {
        // Process all 8 rows
        for (std::size_t r = 0; r < 8; ++r) {
            const std::size_t row_offset = tile_base_offset + r * 8;

            // Load in16[8] and weights for row r
            // IMPORTANT: use _mm_loadl_epi64 to load only 8 bytes (64 bits), not 16
            // This prevents reading garbage beyond array bounds
            // After load, explicitly zero upper 64 bits since _mm_loadl_epi64 doesn't touch them
            __m128i in_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(in16));  // Loads only 64 bits (8 bytes) into lowest 64 bits
            in_vec = _mm_unpacklo_epi64(in_vec, _mm_setzero_si128());  // Zero upper 64 bits
            __m128i mag_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(w_mag + row_offset));
            mag_vec = _mm_unpacklo_epi64(mag_vec, _mm_setzero_si128());  // Zero upper 64 bits
            __m128i sign_vec = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(w_sign + row_offset));
            sign_vec = _mm_unpacklo_epi64(sign_vec, _mm_setzero_si128());  // Zero upper 64 bits

            // Simple multiplication: in16[i] * mag[i] (without division by 15)
            // Expand u8 to i16 for multiplication
            // IMPORTANT: after _mm_loadl_epi64, upper 64 bits are already zero, so _mm_unpackhi_epi8 gives zeros
            __m128i in_lo = _mm_unpacklo_epi8(in_vec, _mm_setzero_si128());  // Expand lowest 8 bytes to i16
            __m128i in_hi = _mm_unpackhi_epi8(in_vec, _mm_setzero_si128());  // Expand highest 8 bytes (which are zeros) to i16
            __m128i mag_lo = _mm_unpacklo_epi8(mag_vec, _mm_setzero_si128());
            __m128i mag_hi = _mm_unpackhi_epi8(mag_vec, _mm_setzero_si128());

            // Multiply: in16[i] * mag[i] (result can be up to 225 for u8*u8, fits in i16)
            // IMPORTANT: if in_val == 0, result should be 0 regardless of sign
            // This is automatically ensured by multiplication: 0 * mag = 0
            __m128i prod_lo = _mm_mullo_epi16(in_lo, mag_lo);  // i16 * i16 = i16 (low), if in=0 then prod=0
            __m128i prod_hi = _mm_mullo_epi16(in_hi, mag_hi);

            // Apply sign: if sign == 1, keep positive, otherwise invert (multiply by -1)
            // IMPORTANT: sign_vec contains u8 where 1 = positive, 0 = negative (inhibitor)
            // Expand sign to i16 for 16-bit operations
            __m128i sign_lo = _mm_unpacklo_epi8(sign_vec, _mm_setzero_si128());  // Expand to i16: 1 -> 1, 0 -> 0
            __m128i sign_hi = _mm_unpackhi_epi8(sign_vec, _mm_setzero_si128());

            // Convert sign to multiplier: if sign==1, then mul=+1, if sign==0, then mul=-1
            // mul = 2 * sign - 1: if sign=1, then mul=+1; if sign=0, then mul=-1
            __m128i mul_lo = _mm_sub_epi16(_mm_add_epi16(sign_lo, sign_lo), _mm_set1_epi16(1));  // mul = 2*sign - 1
            __m128i mul_hi = _mm_sub_epi16(_mm_add_epi16(sign_hi, sign_hi), _mm_set1_epi16(1));

            // Multiply prod by mul: result = prod * mul = prod * (2*sign - 1)
            // If sign==1, then mul=+1, result=+prod
            // If sign==0, then mul=-1, result=-prod
            __m128i result_lo = _mm_mullo_epi16(prod_lo, mul_lo);  // i16 * i16 = i16 (low)
            __m128i result_hi = _mm_mullo_epi16(prod_hi, mul_hi);

            // Sum all 8 elements
            __m128i sum_vec = _mm_add_epi16(result_lo, result_hi);
            sum_vec = _mm_hadd_epi16(sum_vec, sum_vec);  // Horizontal addition
            sum_vec = _mm_hadd_epi16(sum_vec, sum_vec);
            sum_vec = _mm_hadd_epi16(sum_vec, sum_vec);

            row_raw[r] = static_cast<i16>(_mm_cvtsi128_si32(sum_vec));
        }
    }

    // WORLD update: clamp15 for large array (32,768 u16 elements)
    // SSE4.2: process 16 elements at a time
    static inline void simd_world_clamp15(u8* dst, std::span<u8> clip_bits, const u16* src, std::size_t count) noexcept {
        const std::size_t simd_count = count & ~15u;  // Round down to multiple of 16
        const __m128i max_val = _mm_set1_epi16(15);  // 15 for each u16

        // Process 16 elements at a time
        for (std::size_t i = 0; i < simd_count; i += 16) {
            // Load 16 u16 elements (256 bits = 32 bytes)
            __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 8));

            // Generate clip mask (src > 15)
            __m128i clip0 = _mm_cmpgt_epi16(v0, max_val);
            __m128i clip1 = _mm_cmpgt_epi16(v1, max_val);

            // Clamp to 15: if v > 15, use 15, otherwise v
            __m128i clamped0 = _mm_min_epu16(v0, max_val);  // SSE4.1: pminsuw
            __m128i clamped1 = _mm_min_epu16(v1, max_val);

            // Pack clamped values to u8: packus_epi16 does unsigned saturate
            __m128i packed = _mm_packus_epi16(clamped0, clamped1);  // Pack to u8 (saturate at 255)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), packed);

            // Update clip bitset for each element
            // Convert masks to bits (clip0/clip1 contain 0xFFFF or 0x0000 for each element)
            __m128i clip_packed = _mm_packs_epi16(clip0, clip1);  // Pack to i8 (0xFF or 0x00)

            // Extract mask bits (16 bits)
            int clip_mask = _mm_movemask_epi8(clip_packed);

            // Set bits in bitset for each element
            for (int bit = 0; bit < 16 && (i + bit) < count; ++bit) {
                if (clip_mask & (1 << bit)) {
                    bitset_set(clip_bits, i + bit);
                }
            }
        }

        // Process remaining elements (less than 16)
        for (std::size_t i = simd_count; i < count; ++i) {
            const u16 raw = src[i];
            const bool clip = raw > 15;
            if (clip) {
                bitset_set(clip_bits, i);
            }
            dst[i] = clamp15_u8(static_cast<int>(raw));
        }
    }

#if defined(D8_SIMD_AVX512)
    // AVX512 version for modern machines
    #include <immintrin.h>  // AVX512

    // AVX512 version WORLD update - processes 32 u16 elements at a time (converted to 32 u8)
    static inline void simd_world_clamp15_avx512(u8* dst, std::span<u8> clip_bits, const u16* src, std::size_t count) noexcept {
        const std::size_t simd_count = count & ~31u;  // Round down to multiple of 32
        const __m512i max_val = _mm512_set1_epi16(15);  // 15 for each u16

        // Process 32 u16 elements at a time (512 bit register)
        for (std::size_t i = 0; i < simd_count; i += 32) {
            // Load 32 u16 elements
            __m512i v = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(src + i));

            // Generate clip mask (src > 15) - AVX512 masks (32 bits)
            __mmask32 clip_mask32 = _mm512_cmpgt_epu16_mask(v, max_val);

            // Clamp to 15
            __m512i clamped = _mm512_min_epu16(v, max_val);

            // Pack clamped values to u8 (32 u16 -> 32 u8)
            // AVX512BW: _mm512_cvtepi16_epi8 for truncation packing
            __m256i packed = _mm512_cvtepi16_epi8(clamped);  // Convert i16 to i8 with truncation

            // Store result (32 bytes)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), packed);

            // Update clip bitset using AVX512 mask (32 bits = 32 elements)
            for (int bit = 0; bit < 32 && (i + bit) < count; ++bit) {
                if (clip_mask32 & (1u << bit)) {
                    bitset_set(clip_bits, i + bit);
                }
            }
        }

        // Process remaining elements via SSE4.2
        if (simd_count < count) {
            simd_world_clamp15(dst + simd_count, clip_bits, src + simd_count, count - simd_count);
        }
    }
#endif

#else
    // Fallback versions without SIMD (original code)
    static inline i16 simd_sum8_i16(const i16* arr) noexcept {
        i16 sum = 0;
        for (std::size_t i = 0; i < 8; ++i) sum += arr[i];
        return sum;
    }

    static inline void simd_blend8_u8(u8* dst, const u8* a, const u8* b, u8 mask) noexcept {
        for (std::size_t i = 0; i < 8; ++i) {
            dst[i] = mask ? a[i] : b[i];
        }
    }

    static inline void simd_clamp15_8_u32(u8* dst, u8* clip_mask, const u32* src) noexcept {
        *clip_mask = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            dst[i] = clamp15_u8(static_cast<int>(src[i]));
            if (src[i] > 15) *clip_mask |= u8(1u << i);
        }
    }

    static inline void simd_compute_row_raw(i16* row_raw, const u8* in16, 
                                           const u8* w_mag, const u8* w_sign,
                                           std::size_t tile_base_offset) noexcept {
        // Fallback: simple multiplication without division by 15 (iron logic)
        for (std::size_t r = 0; r < 8; ++r) {
            const std::size_t row_offset = tile_base_offset + r * 8;
            i16 sum = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                const u8 in_val = in16[i];
                const u8 mag_val = w_mag[row_offset + i];
                const u8 sign_val = w_sign[row_offset + i];
                // Direct multiplication: in_val * mag_val (without division by 15)
                const i16 mul_result = sign_val ? static_cast<i16>(in_val * mag_val) : static_cast<i16>(-static_cast<i16>(in_val * mag_val));
                sum += mul_result;
            }
            row_raw[r] = sum;
        }
    }

    static inline void simd_compute_in16(u8* dst, std::span<u8> clip_bits, std::size_t base_bit_idx,
                                        const u8* vsb_ingress, const u8* /*bus16*/, 
                                        bool is_active) noexcept {
        if (!is_active) {
            for (std::size_t i = 0; i < 8; ++i) {
                dst[i] = 0;
            }
            return;
        }
        for (std::size_t i = 0; i < 8; ++i) {
            const u8 raw = vsb_ingress[i];
            dst[i] = clamp15_u8(static_cast<int>(raw));
            if (raw > 15) bitset_set(clip_bits, base_bit_idx + i);
        }
    }
#endif

#if defined(D8_SIMD_SSE4_2)
    static inline void compute_in16_simd(u8* out_in16, std::span<u8> clip_bits, std::size_t base_bit_idx,
        const std::array<u8, kLanes>& vsb_ingress16, const std::array<u8, kLanes>& bus16,
        bool is_active) noexcept {
        simd_compute_in16(out_in16, clip_bits, base_bit_idx,
                          vsb_ingress16.data(), bus16.data(),
                          is_active);
    }

    static inline void compute_row_raw_simd(i16* out_row_raw, const u8* in16,
        const std::array<u8, kTileCount * 64>& w_mag, const std::array<u8, kTileCount * 64>& w_sign,
        std::size_t tile_id) noexcept {
        simd_compute_row_raw(out_row_raw, in16, w_mag.data(), w_sign.data(), tile_id * 64);
    }

    static inline i16 compute_delta_raw_simd(const i16* row_signed) noexcept {
        return simd_sum8_i16(row_signed);
    }
#endif

    static inline void compute_active_dims(u32 limit, std::size_t& out_w, std::size_t& out_h) noexcept {
        if (limit == 0 || limit == kTileCount) {
            out_w = kExpectedW;
            out_h = kExpectedH;
            return;
        }
        if (limit == 128) {
            limit = 256;
        }
        switch (limit) {
        case 256:  out_w = 32;  out_h = 8;  break;
        case 512:  out_w = 32;  out_h = 16; break;
        case 1024: out_w = 64;  out_h = 16; break;
        case 2048: out_w = 64;  out_h = 32; break;
        case 4096: out_w = 128; out_h = 32; break;
        default:
            out_w = kExpectedW;
            out_h = kExpectedH;
            break;
        }
    }

    template <typename Fn>
    static inline void for_each_active_tile(std::size_t w, std::size_t h, Fn&& fn) noexcept {
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                fn(y * kExpectedW + x, x, y);
            }
        }
    }

    static inline bool is_active_tile(std::size_t tile_id, std::size_t w, std::size_t h) noexcept {
        const std::size_t x = tile_id % kExpectedW;
        const std::size_t y = tile_id / kExpectedW;
        return x < w && y < h;
    }

    status swarm::ev_bake(std::span<const u8> blob) noexcept {
        if (flash_in_progress_) {
            // If flash is stuck, reset flag to allow bake application
            // This can happen if flash didn't complete correctly
            flash_in_progress_ = false;
        }
        bake::compiled_image tmp{};
        auto st = bake::compile(blob, tmp);
        if (!st) return st;

        // CRITICAL: save current thr_cur and locked states before applying new bake
        // This is needed to avoid losing tile states when only changing routing masks or other parameters
        // BUT: if loading a new bake (bake_id changed), all states must be reset
        const bool was_baked = bake_applied_;
        // IMPORTANT: save old bake_id BEFORE assigning img_ = tmp
        const u32 old_bake_id = was_baked ? img_.bake_id : 0;
        const bool is_new_bake = !was_baked || (old_bake_id != tmp.bake_id);
        const std::size_t prev_w = active_tile_w_;
        const std::size_t prev_h = active_tile_h_;
        std::array<i16, kTileCount> saved_thr_cur{};
        std::array<u8, kTileCount> saved_locked{};
        if (was_baked && !is_new_bake) {
            // Save current states only if this is the same bake (parameter change)
            for_each_active_tile(prev_w, prev_h, [&](std::size_t t, std::size_t, std::size_t) noexcept {
                saved_thr_cur[t] = thr_cur_[t];
                saved_locked[t] = locked_[t];
            });
        }

        img_ = tmp;
        bake_applied_ = true;
        double_strait_bake_ = (img_.bake_flags & d8::BAKE_FLAG_DOUBLE_STRAIT) != 0;

        const u32 limit = img_.tile_field_limit;
        compute_active_dims(limit, active_tile_w_, active_tile_h_);
        active_tile_count_ = active_tile_w_ * active_tile_h_;
        const std::size_t tile_count = active_tile_count_;
        clear_inactive_shared_buf_ = (tile_count < kTileCount);

        // neighbors for 128x32 (cardinal + diagonal)
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t tid, std::size_t x, std::size_t y) noexcept {
            // Cardinal directions
            nN_[tid] = (y > 0) ? int(tid - kExpectedW) : -1;
            nS_[tid] = (y + 1 < active_tile_h_) ? int(tid + kExpectedW) : -1;
            nW_[tid] = (x > 0) ? int(tid - 1) : -1;
            nE_[tid] = (x + 1 < active_tile_w_) ? int(tid + 1) : -1;
            // Diagonal directions
            nNE_[tid] = (y > 0 && x + 1 < active_tile_w_) ? int(tid - kExpectedW + 1) : -1;  // North-East
            nSE_[tid] = (y + 1 < active_tile_h_ && x + 1 < active_tile_w_) ? int(tid + kExpectedW + 1) : -1;  // South-East
            nSW_[tid] = (y + 1 < active_tile_h_ && x > 0) ? int(tid + kExpectedW - 1) : -1;  // South-West
            nNW_[tid] = (y > 0 && x > 0) ? int(tid - kExpectedW - 1) : -1;  // North-West
        });
        for (std::size_t tid = 0; tid < kTileCount; ++tid) {
            if (is_active_tile(tid, active_tile_w_, active_tile_h_)) {
                continue;
            }
            nN_[tid] = -1; nS_[tid] = -1; nW_[tid] = -1; nE_[tid] = -1;
            nNE_[tid] = -1; nSE_[tid] = -1; nSW_[tid] = -1; nNW_[tid] = -1;
        }

        compute_domain_connectivity_();
        compute_parent_topology_(); // Compute parent/children for IDE

        // Reset only BUS, but keep thr_cur and locked if this is the same bake (parameter change)
        snapshot_.bus16.fill(0);

        if (was_baked && !is_new_bake) {
            // Restore saved thr_cur and locked states (parameter change of existing bake)
            for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t x, std::size_t y) noexcept {
                if (x < prev_w && y < prev_h) {
                    thr_cur_[t] = saved_thr_cur[t];
                    locked_[t] = saved_locked[t];
                } else {
                    thr_cur_[t] = 0;
                    locked_[t] = 0;
                }
                // Adjust locked to v2 range if parameters changed
                if (img_.bake_id == 0) {
                    // Unbaked tile - locked is always 0
                    locked_[t] = 0;
                } else if (locked_[t] != 0) {
                    const i16 thr_lo = img_.thr_lo[t];
                    const i16 thr_hi = img_.thr_hi[t];
                    const bool in_range = (thr_lo <= thr_cur_[t]) && (thr_cur_[t] <= thr_hi);
                    if (!in_range) {
                        locked_[t] = 0;
                    }
                }
            });
        } else {
            // New bake or first bake - use standard initialization (all from zero)
            for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
                thr_cur_[t] = 0;
                locked_[t] = 0;
            });
        }
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (is_active_tile(t, active_tile_w_, active_tile_h_)) {
                continue;
            }
            thr_cur_[t] = 0;
            locked_[t] = 0;
        }

        // Update v_state for all tiles after bake
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const u8 thr_n = thr_norm_4bit(thr_cur_[t], img_.thr_lo[t], img_.thr_hi[t]);
            const bool is_locked = (img_.bake_id > 0) ? (locked_[t] != 0) : false;
            snapshot_.v_state[t] = u8((thr_n << 4) | (is_locked ? 0x08 : 0) | 0x00);
        });
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (is_active_tile(t, active_tile_w_, active_tile_h_)) {
                continue;
            }
            snapshot_.v_state[t] = 0;
        }

        snapshot_.bake_id_active = img_.bake_id;
        snapshot_.profile_id_active = img_.profile_id;

        // Update shared buffer after bake
        if (shared_buf_) {
            shared_buf_->bake_id_active.store(img_.bake_id, std::memory_order_relaxed);
            shared_buf_->profile_id_active.store(img_.profile_id, std::memory_order_relaxed);
            write_to_shared_buffer_(); // Write initial state
        }

        return status{};
    }

    void swarm::full_reset_runtime_() noexcept {
        // BUS16 := 0 (initialization for first tick)
        snapshot_.bus16.fill(0);

        // thr_cur := 0; locked := reset-semantics v0.2
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            thr_cur_[t] = 0;  // i16: can be negative during subtraction
            locked_[t] = 0;

            const u8 thr_n = thr_norm_4bit(thr_cur_[t], img_.thr_lo[t], img_.thr_hi[t]);
            const bool is_locked = (snapshot_.bake_id_active > 0) ? (locked_[t] != 0) : false;
            snapshot_.v_state[t] = u8((thr_n << 4) | (is_locked ? 0x08 : 0) | 0x00);
        });
        for (std::size_t t = 0; t < kTileCount; ++t) {
            const std::size_t x = t % kExpectedW;
            const std::size_t y = t / kExpectedW;
            if (x < active_tile_w_ && y < active_tile_h_) {
                continue;
            }
            thr_cur_[t] = 0;
            locked_[t] = 0;
            snapshot_.v_state[t] = 0;
        }

        // ACTIVE is computed in each flash via closure (see flash_impl_)
    }

    status swarm::ev_reset_domain(u16 mask16) noexcept {
        if (!bake_applied_) return { status::code::NotBaked, 0, "NotBaked" };
        if (flash_in_progress_) return { status::code::BadPhase, 0, "EV_RESET_DOMAIN only between EV_FLASH" };

        // Count tiles to reset (for summary)
        std::size_t tiles_to_reset = 0;
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const u8 d = img_.domain_id[t];
            if (mask16 & (u16(1u) << d)) tiles_to_reset++;
        });
        if (tiles_to_reset > 0) {
            core_log_info("RESET_DOMAIN: mask16={:04X}, resetting {} tiles", mask16, tiles_to_reset);
        }

        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const u8 d = img_.domain_id[t];
            if (mask16 & (u16(1u) << d)) {
                thr_cur_[t] = 0;  // i16: accumulator reset
                locked_[t] = 0;

                // CRITICAL: update v_state immediately after reset so SwarmView can see changes
                const u8 thr_n = thr_norm_4bit(thr_cur_[t], img_.thr_lo[t], img_.thr_hi[t]);
                // CRITICAL: For unbaked tiles (bake_id_active == 0) locked must always be 0
                const bool is_locked = (snapshot_.bake_id_active > 0) ? (locked_[t] != 0) : false;
                snapshot_.v_state[t] = u8((thr_n << 4) | (is_locked ? 0x08 : 0) | 0x00);
            }
        });
        return status{};
    }

    flash_result swarm::ev_flash(u32 tag, const std::array<u8, kLanes>& vsb_ingress16) noexcept {
        if (!bake_applied_) {
            return { {status::code::NotBaked, 0, "NotBaked"}, {}, 0 };
        }
        if (flash_in_progress_) {
            return { {status::code::BadPhase, 0, "Re-entrant EV_FLASH"}, {}, 0 };
        }

        // Double pour — INSIDE swarm, as required by contract
        if (double_strait_bake_) {
            // Intermediate tick: result is ignored (contract 13.2)

            // Temporarily disable write to shared buffer — saves ~3-5 µs
            auto* saved_shared_buf = shared_buf_;
            shared_buf_ = nullptr;

            flash_impl_(tag, vsb_ingress16, {});

            // Restore pointer
            shared_buf_ = saved_shared_buf;

            // Final tick: result is returned
            return flash_impl_(tag, vsb_ingress16, {});
        }
        return flash_impl_(tag, vsb_ingress16, {});
    }

    flash_result swarm::trace_flash(u32 tag, const std::array<u8, kLanes>& vsb_ingress16,
        std::span<tile_trace> out_traces) noexcept {
        const std::size_t tile_count = active_tile_count_;
        if (out_traces.size() < tile_count) {
            return flash_result{ {status::code::BakeBadLen, u32(out_traces.size()), "Trace buffer too small"}, {}, 0 };
        }
        return flash_impl_(tag, vsb_ingress16, out_traces);
    }

    flash_result swarm::flash_impl_(u32 tag, const std::array<u8, kLanes>& vsb_ingress16,
        std::span<tile_trace> traces) noexcept {
        (void)traces;  // Unused parameter (kept for API compatibility)
        flash_result res{};
        if (!bake_applied_) { res.st = { status::code::NotBaked, 0, "NotBaked" }; return res; }
        if (flash_in_progress_) { res.st = { status::code::BadPhase, 0, "Re-entrant EV_FLASH" }; return res; }
        const std::size_t tile_count = active_tile_count_;

        // strict ingress Level16
        for (u8 v : vsb_ingress16) {
            if (v > 15) { res.st = { status::code::BadIngressLevel, v, "Ingress must be 0..15" }; return res; }
        }

        // Measure cycle start time (unified method for all platforms)
        const auto start_time = std::chrono::steady_clock::now();

        flash_in_progress_ = true;

        snapshot_.frame_tag = tag;
        snapshot_.vsb_ingress16 = vsb_ingress16;
        snapshot_.flags32_last = 0;
        snapshot_.bus_clip_mask8 = 0;
        snapshot_.auto_reset_mask16 = 0;
        snapshot_.collide_mask16 = 0;
        snapshot_.domains_fired_mask16 = 0;

        snapshot_.in_clip_bits.fill(0);

        // NOTE: bus16 is stored separately; ingress is not mixed into bus at input

        // ===================================================================
        // PHASE 1: PHASE_READ
        // ===================================================================
        // Iterate over all tiles and accumulate for those who can read from bus
        // (those with IDE read permission bit or from parent)
        // ===================================================================
        
        fire_.fill(0);

        // For domain winner selection
        std::array<u16, kDomains> cnt{};
        cnt.fill(0);
        snapshot_.winner_tile_id.fill(0xFFFF);
        snapshot_.winner_priority.fill(0);
        snapshot_.fired_cnt_sat.fill(0);
        snapshot_.reset_mask_from_winner.fill(0);

        // Snapshot of locked_ before PHASE_READ (for correct lock/unlock transition handling in INTERPHASE)
        std::array<u8, kTileCount> locked_before_tick{};
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            locked_before_tick[t] = (locked_[t] != 0) ? 1 : 0;
        });

        // ACTIVE closure (v0.2): seed by BUS_R, propagate by locked_before
        std::array<u8, kTileCount> active{};
        active.fill(0);
        std::vector<std::size_t> queue;
        queue.reserve(tile_count);
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            if (img_.bus_r[t] != 0) { // BUS_R seed
                active[t] = 1;
                queue.push_back(t);
            }
        });
        for (std::size_t qi = 0; qi < queue.size(); ++qi) {
            const std::size_t t = queue[qi];
            if (locked_before_tick[t] == 0) continue;

            auto try_activate = [&](int nb) noexcept {
                if (nb < 0) return;
                const std::size_t n = std::size_t(nb);
                if (active[n] == 0) {
                    active[n] = 1;
                    queue.push_back(n);
                }
            };

            if (img_.maskN[t])  try_activate(nN_[t]);
            if (img_.maskE[t])  try_activate(nE_[t]);
            if (img_.maskS[t])  try_activate(nS_[t]);
            if (img_.maskW[t])  try_activate(nW_[t]);
            if (img_.maskNE[t]) try_activate(nNE_[t]);
            if (img_.maskSE[t]) try_activate(nSE_[t]);
            if (img_.maskSW[t]) try_activate(nSW_[t]);
            if (img_.maskNW[t]) try_activate(nNW_[t]);
        }

        // READ loop: constant bounds
        // Fast Path: minimum if statements, only locked_before check
        using ComputeIn16Fn = void(*)(u8*, std::span<u8>, std::size_t,
            const std::array<u8, kLanes>&, const std::array<u8, kLanes>&, bool);
        using ComputeRowRawFn = void(*)(i16*, const u8*, const std::array<u8, kTileCount * 64>&,
            const std::array<u8, kTileCount * 64>&, std::size_t);
        using ComputeDeltaRawFn = i16(*)(const i16*);

        #if defined(D8_SIMD_SSE4_2)
        const ComputeIn16Fn do_compute_in16 = compute_in16_simd;
        const ComputeRowRawFn do_compute_row_raw = compute_row_raw_simd;
        const ComputeDeltaRawFn do_compute_delta_raw = compute_delta_raw_simd;
        #else
        const ComputeIn16Fn do_compute_in16 = compute_in16_reference;
        const ComputeRowRawFn do_compute_row_raw = compute_row_raw_reference;
        const ComputeDeltaRawFn do_compute_delta_raw = compute_delta_raw_reference;
        #endif

        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            // in16 formation: VSB ingress + BUS when tile is ACTIVE (v0.2 closure), clamp15, IN_CLIP
            // Data transfer is via BUS16 only
            // Always work on 8 channels (don't mask any strings)
            const bool is_active = (active[t] != 0);

            u8 in16[8]{};
            do_compute_in16(in16, snapshot_.in_clip_bits, t * 8,
                            vsb_ingress16, snapshot_.bus16,
                            is_active);

            const i16 thr_before = thr_cur_[t];  // i16: accumulator can be negative
            const u8 locked_before = locked_[t] ? 1 : 0;

            // row pipeline (always computed for constant work; output path respects LOCK passthrough)
            i16 row_raw[8]{};
            u8  row_out[8]{};
            i16 row_signed[8]{};

            do_compute_row_raw(row_raw, in16, img_.w_mag, img_.w_sign, t);

            // Compute row_out from row_raw (for drive_vec_)
            // row_out is row_raw with division by 8 and clamp15 (for BUS drive)
            for (std::size_t r = 0; r < 8; ++r) {
                const i16 sum = row_raw[r];
                const int pos = (sum > 0) ? int(sum) : 0;
                row_out[r] = clamp15_u8((pos + 7) / 8);
                // row_signed now = row_raw (no clamp, no round_div8)
                row_signed[r] = sum;
            }

            // delta: sum of 8 rows (NO clamp, NO round_div8) - direct summation of row_raw
            const i16 delta = do_compute_delta_raw(row_signed);


            // thr update (p. 6.6 Spec: strictly int16_t for thr_cur)
            // Accumulation is ONLY possible if tile is ACTIVE
            const i16 thr_lo = img_.thr_lo[t];
            const i16 thr_hi = img_.thr_hi[t];
            const u16 decay16 = img_.decay16[t];
            i16 thr_after = thr_before;  // i16: accumulator
            u8 locked_after = locked_before;

            const bool is_baked = (img_.bake_id > 0);

            if (!is_active) {
                // Inactive tiles are forcibly reset to zero (contract v2)
                thr_after = 0;
                locked_after = 0;
            } else if (locked_before == 0) {
                // Tile can accumulate - update accumulator
                // IMPORTANT: need to use intermediate int64_t to prevent i16 overflow
                std::int64_t thr_tmp = static_cast<std::int64_t>(thr_before) + static_cast<std::int64_t>(delta);
                const std::int64_t thr_tmp_no_decay = thr_tmp;

                // Decay pulls toward 0 and doesn't cross 0
                if (decay16 > 0) {
                    if (thr_tmp > 0) {
                        thr_tmp = (std::max)(thr_tmp - static_cast<std::int64_t>(decay16), std::int64_t(0));
                    } else if (thr_tmp < 0) {
                        thr_tmp = (std::min)(thr_tmp + static_cast<std::int64_t>(decay16), std::int64_t(0));
                    }
                }

                // Clamp to i16 bounds (-32768..+32767) to prevent overflow
                thr_after = static_cast<i16>((std::max)(static_cast<std::int64_t>(-32768),
                    (std::min)(thr_tmp, static_cast<std::int64_t>(32767))));

                const bool range_active = (thr_lo < thr_hi);
                const bool in_range_after = range_active && (thr_lo <= thr_after) && (thr_after <= thr_hi);
                const bool in_range_before_decay = range_active && (thr_lo <= thr_tmp_no_decay) && (thr_tmp_no_decay <= thr_hi);
                const bool has_signal = (delta != 0);
                const bool entered_by_decay = (!in_range_before_decay && in_range_after && decay16 > 0);
                locked_after = (is_baked && in_range_after && (has_signal || entered_by_decay)) ? 1 : 0;
                if (locked_after != 0) {
                    core_log_info(
                        "LOCK: t={}, thr_before={}, delta={}, thr_tmp_no_decay={}, thr_after={}, thr_lo={}, thr_hi={}, in_range_before_decay={}, in_range_after={}, has_signal={}, entered_by_decay={}, in16=[{},{},{},{},{},{},{},{}], bus16=[{},{},{},{},{},{},{},{}], vsb=[{},{},{},{},{},{},{},{}]",
                        t, thr_before, delta, thr_tmp_no_decay, thr_after, thr_lo, thr_hi,
                        in_range_before_decay ? 1 : 0, in_range_after ? 1 : 0, has_signal ? 1 : 0, entered_by_decay ? 1 : 0,
                        in16[0], in16[1], in16[2], in16[3], in16[4], in16[5], in16[6], in16[7],
                        snapshot_.bus16[0], snapshot_.bus16[1], snapshot_.bus16[2], snapshot_.bus16[3],
                        snapshot_.bus16[4], snapshot_.bus16[5], snapshot_.bus16[6], snapshot_.bus16[7],
                        vsb_ingress16[0], vsb_ingress16[1], vsb_ingress16[2], vsb_ingress16[3],
                        vsb_ingress16[4], vsb_ingress16[5], vsb_ingress16[6], vsb_ingress16[7]
                    );
                }
            } else {
                // locked_before == 1: tile was already locked
                if (!is_baked) {
                    thr_after = thr_before;
                    locked_after = 0;
                } else {
                    // Decay works EVEN for locked tiles (requirement: pull to zero constantly)
                    std::int64_t thr_tmp = static_cast<std::int64_t>(thr_before);

                    // Apply ONLY decay (no delta — locked tile doesn't accumulate new signals)
                    if (decay16 > 0) {
                        if (thr_tmp > 0) {
                            thr_tmp = (std::max)(thr_tmp - static_cast<std::int64_t>(decay16), std::int64_t(0));
                        }
                        else if (thr_tmp < 0) {
                            thr_tmp = (std::min)(thr_tmp + static_cast<std::int64_t>(decay16), std::int64_t(0));
                        }
                    }

                    // Clamp to i16 bounds
                    thr_after = static_cast<i16>((std::max)(static_cast<std::int64_t>(-32768),
                        (std::min)(thr_tmp, static_cast<std::int64_t>(32767))));

                    // Check: did tile remain in corridor [thr_lo..thr_hi] after decay?
                    const bool range_active = (thr_lo < thr_hi);
                    const bool in_range_after = range_active && (thr_lo <= thr_after) && (thr_after <= thr_hi);

                    // Unlock if exited corridor (even due to single decay step)
                    locked_after = in_range_after ? 1 : 0;

                    // Log unlock (optional, for debugging)
                    if (locked_after == 0 && locked_before == 1) {
                        core_log_info("UNLOCK (decay): t={}, thr_before={}, thr_after={}, thr_lo={}, thr_hi={}, decay16={}",
                            t, thr_before, thr_after, thr_lo, thr_hi, decay16);
                    }
                }
            }

            thr_cur_[t] = thr_after;
            locked_[t] = locked_after;

            const bool fired = (locked_before == 0 && locked_after == 1);
            fire_[t] = fired ? 1 : 0;

            // drive selection (normative): if locked_after==1 => passthrough in16
            // SIMD OPTIMIZATION: conditional copy 8 bytes - _mm_blendv_epi8 (SSE4.1) or _mm512_mask_blend_epi8 (AVX512)
            simd_blend8_u8(&drive_vec_[t * 8], in16, row_out, locked_after);

            // V-State pack (post-ACCUMULATE; locked bit will be updated again after AUTORESET)
            // CRITICAL: thr_after is now i16, can be up to 32767
            // thr_norm_4bit now accepts i16 directly, clamp is performed inside function
            const u8 thr_n = thr_norm_4bit(thr_after, img_.thr_lo[t], img_.thr_hi[t]);
            // Branchless polarity: pol = (locked_before==0) ? (delta>0?2:delta<0?1:0) : 0
            const u8 pol = (locked_before == 0) ? u8((delta > 0) ? 2 : ((delta < 0) ? 1 : 0)) : 0;
            u8 vs = u8((thr_n & 0x0F) << 4);
            // CRITICAL: For unbaked tiles (bake_id_active == 0) locked must always be 0
            // Raw tiles must not be locked, even if locked_after == 1
            const bool is_locked = (snapshot_.bake_id_active > 0) ? (locked_after != 0) : false;
            vs |= (is_locked ? 0x08 : 0);
            // CRITICAL: Fire_Event bit is set when fired=true, but persists until next flash
            // This allows IDE to see all fused tiles, even if they fused in different flashes
            // If tile fused (fired=true), set the bit, otherwise keep previous value
            const u8 fire_bit = fired ? 0x04 : (snapshot_.v_state[t] & 0x04);
            vs |= fire_bit;
            vs |= (pol & 0x03);
            snapshot_.v_state[t] = vs;

            // Domain fired stats + winner tie-break (ignore pattern_id==0)
            const u8 d = img_.domain_id[t];
            const bool fired_solution = fired && (img_.pattern_id[t] != 0);
            const u16 fired_mask = u16(fired_solution ? 1u : 0u);
            const u16 new_cnt = cnt[d] + fired_mask;
            cnt[d] = new_cnt;
            
            // Branchless mask updates
            const u16 domain_bit = u16(1u) << d;
            snapshot_.domains_fired_mask16 |= domain_bit & (fired_mask * 0xFFFF);
            snapshot_.collide_mask16 |= domain_bit & ((new_cnt >= 2) ? 0xFFFF : 0);

        });

        // ===================================================================
        // PHASE 1.5: DETERMINISTIC WINNER SELECTION (fix for unstable solution IDs)
        // ===================================================================
        // Select winner deterministically for each domain by considering ALL firing tiles
        // This ensures consistent results regardless of processing order
        // ===================================================================

        // Reset all winners before re-computing them deterministically
        for (std::size_t d = 0; d < kDomains; ++d) {
            snapshot_.winner_tile_id[d] = 0xFFFF;
            snapshot_.winner_priority[d] = 0;
        }

        // Process all tiles again to find the true winner per domain based on all firing tiles
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const bool fired = (fire_[t] != 0) && (img_.pattern_id[t] != 0);
            if (!fired) return;

            const u8 d = img_.domain_id[t];
            const u8 pri = img_.priority[t];
            const u16 cur_w = snapshot_.winner_tile_id[d];
            const u8 cur_pri = snapshot_.winner_priority[d];

            // Deterministic winner selection: max priority, then min tile ID
            const bool better = (cur_w == 0xFFFF) ||  // No current winner
                                (pri > cur_pri) ||    // Higher priority
                                ((pri == cur_pri) && (u16(t) < cur_w)); // Same priority, lower tile ID

            if (better) {
                snapshot_.winner_tile_id[d] = u16(t);
                snapshot_.winner_priority[d] = pri;
            }
        });

        // ===================================================================
        // PHASE 2: INTERPHASE (between READ and WRITE)
        // ===================================================================
        // Collect AUTO_RESET_MASK16 from winner of each domain (v0.2)
        // ===================================================================

        // Determine who locked and collect reset mask from resetters
        // Any locked tile can request domain reset
        u16 auto_reset_mask16 = 0;
        std::vector<std::size_t> resetters;
        resetters.reserve(32);
        for (std::size_t d = 0; d < kDomains; ++d) {
            const u16 c = cnt[d];
            snapshot_.fired_cnt_sat[d] = (c > 255) ? 255 : u8(c);
            const u16 w = snapshot_.winner_tile_id[d];
            if (w != 0xFFFF && is_active_tile(w, active_tile_w_, active_tile_h_)) {
                const u16 rm = img_.reset_on_fire_mask16[w];
                snapshot_.reset_mask_from_winner[d] = rm;
                auto_reset_mask16 |= rm;
            }
        }
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            if (fire_[t] == 0) return;
            const u16 rm = img_.reset_on_fire_mask16[t];
            if (rm == 0) return;
            auto_reset_mask16 |= rm;
            resetters.push_back(t);
        });

        snapshot_.auto_reset_mask16 = auto_reset_mask16;

        // ===================================================================
        // PHASE 3: PHASE_WRITE
        // ===================================================================
        // Drive to bus all BUS_W tiles with locked ancestor (or locked self)
        // ===================================================================
        // BUS is formed as sum of drive_vec_ from all writing tiles, then clamp15
        std::array<u32, 8> bus_raw{};
        bus_raw.fill(0);

#if defined(D8_SIMD_SSE4_2)
        // Vector accumulators for 8 channels (2 × 4 i32)
        __m128i acc_lo = _mm_setzero_si128();  // channels 0..3
        __m128i acc_hi = _mm_setzero_si128();  // channels 4..7

        for (std::size_t y = 0; y < active_tile_h_; ++y) {
            for (std::size_t x = 0; x < active_tile_w_; ++x) {
                const std::size_t t = y * kExpectedW + x;
                const u8 mB = img_.bus_w[t];

                // Check: either tile itself fused, or there's a fused ancestor in ACTIVATION GRAPH
                bool has_locked_ancestor = (locked_[t] != 0);
                if (!has_locked_ancestor) {
                    // Check all 8 neighbors for edge IN OUR DIRECTION + locked
                    if (nN_[t] >= 0 && img_.maskS[std::size_t(nN_[t])] && locked_[std::size_t(nN_[t])]) has_locked_ancestor = true;
                    else if (nS_[t] >= 0 && img_.maskN[std::size_t(nS_[t])] && locked_[std::size_t(nS_[t])]) has_locked_ancestor = true;
                    else if (nW_[t] >= 0 && img_.maskE[std::size_t(nW_[t])] && locked_[std::size_t(nW_[t])]) has_locked_ancestor = true;
                    else if (nE_[t] >= 0 && img_.maskW[std::size_t(nE_[t])] && locked_[std::size_t(nE_[t])]) has_locked_ancestor = true;
                    else if (nNE_[t] >= 0 && img_.maskSW[std::size_t(nNE_[t])] && locked_[std::size_t(nNE_[t])]) has_locked_ancestor = true;
                    else if (nSE_[t] >= 0 && img_.maskNW[std::size_t(nSE_[t])] && locked_[std::size_t(nSE_[t])]) has_locked_ancestor = true;
                    else if (nSW_[t] >= 0 && img_.maskNE[std::size_t(nSW_[t])] && locked_[std::size_t(nSW_[t])]) has_locked_ancestor = true;
                    else if (nNW_[t] >= 0 && img_.maskSE[std::size_t(nNW_[t])] && locked_[std::size_t(nNW_[t])]) has_locked_ancestor = true;
                }

                const bool can_write = (mB != 0) && has_locked_ancestor;

                if (can_write) {
                    // Load 8 bytes of drive_vec as vector (lowest 64 bits)
                    __m128i dv = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&drive_vec_[t * 8]));

                    // Expand lowest 4 bytes → 4 x i32
                    __m128i dv_lo = _mm_cvtepu8_epi32(dv);
                    // Shift by 4 bytes → expand highest 4 bytes → 4 x i32
                    __m128i dv_hi = _mm_cvtepu8_epi32(_mm_srli_si128(dv, 4));

                    // Sum into accumulators
                    acc_lo = _mm_add_epi32(acc_lo, dv_lo);
                    acc_hi = _mm_add_epi32(acc_hi, dv_hi);
                }
            }
        }

        // Extract result to bus_raw
        alignas(16) std::array<i32, 4> tmp_lo{}, tmp_hi{};
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp_lo.data()), acc_lo);
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp_hi.data()), acc_hi);
        for (size_t i = 0; i < 4; ++i) bus_raw[i] = static_cast<u32>(tmp_lo[i]);
        for (size_t i = 0; i < 4; ++i) bus_raw[i + 4] = static_cast<u32>(tmp_hi[i]);

#else
        // Scalar version (without SIMD)
        for (std::size_t y = 0; y < active_tile_h_; ++y) {
            for (std::size_t x = 0; x < active_tile_w_; ++x) {
                const std::size_t t = y * kExpectedW + x;
                const u8 mB = img_.bus_w[t];

                // Check: either tile itself fused, or there's a fused ancestor in ACTIVATION GRAPH
                bool has_locked_ancestor = (locked_[t] != 0);
                if (!has_locked_ancestor) {
                    if (nN_[t] >= 0 && img_.maskS[std::size_t(nN_[t])] && locked_[std::size_t(nN_[t])]) has_locked_ancestor = true;
                    else if (nS_[t] >= 0 && img_.maskN[std::size_t(nS_[t])] && locked_[std::size_t(nS_[t])]) has_locked_ancestor = true;
                    else if (nW_[t] >= 0 && img_.maskE[std::size_t(nW_[t])] && locked_[std::size_t(nW_[t])]) has_locked_ancestor = true;
                    else if (nE_[t] >= 0 && img_.maskW[std::size_t(nE_[t])] && locked_[std::size_t(nE_[t])]) has_locked_ancestor = true;
                    else if (nNE_[t] >= 0 && img_.maskSW[std::size_t(nNE_[t])] && locked_[std::size_t(nNE_[t])]) has_locked_ancestor = true;
                    else if (nSE_[t] >= 0 && img_.maskNW[std::size_t(nSE_[t])] && locked_[std::size_t(nSE_[t])]) has_locked_ancestor = true;
                    else if (nSW_[t] >= 0 && img_.maskNE[std::size_t(nSW_[t])] && locked_[std::size_t(nSW_[t])]) has_locked_ancestor = true;
                    else if (nNW_[t] >= 0 && img_.maskSE[std::size_t(nNW_[t])] && locked_[std::size_t(nNW_[t])]) has_locked_ancestor = true;
                }

                const bool can_write = (mB != 0) && has_locked_ancestor;

                if (can_write) {
                    for (std::size_t i = 0; i < 8; ++i) {
                        bus_raw[i] += drive_vec_[t * 8 + i];
                    }
                }
            }
        }
#endif

        // BUS clamp + clip mask
        u8 clip_mask8 = 0;
        simd_clamp15_8_u32(snapshot_.bus16.data(), &clip_mask8, bus_raw.data());
        const bool bus_clip_any = (clip_mask8 != 0);
        if (bus_clip_any) {
            snapshot_.bus_clip_mask8 |= clip_mask8;
        }

        // -------------------------
        // READOUT_SAMPLE
        // -------------------------
        std::array<u8, 8> readout{};
        if (img_.readout.mode == readout_mode::R0_RAW_BUS) {
            readout = snapshot_.bus16;
        }
        else {
            // R1_DOMAIN_WINNER_ID32: choose winner tile_id as ID32 for one selected domain.
            // If mask contains multiple bits, select lowest bit deterministically
            u16 mask = img_.readout.winner_domain_mask;
            int dsel = -1;
            for (int d = 0; d < 16; ++d) { if (mask & (u16(1u) << d)) { dsel = d; break; } }
            u32 id32 = 0;
            if (dsel >= 0) {
                const u16 w = snapshot_.winner_tile_id[std::size_t(dsel)];
                id32 = (w == 0xFFFF) ? 0u : u32(w);
            }
            for (std::size_t i = 0; i < 8; ++i) {
                readout[i] = u8((id32 >> (4 * i)) & 0x0Fu);
            }
        }

        // -------------------------
        // FLAGS32_LAST (minimum + collide mask)
        // -------------------------
        bool in_clip_any = false;
        // quick scan: if any byte in in_clip_bits nonzero
        for (u8 b : snapshot_.in_clip_bits) { if (b) { in_clip_any = true; break; } }
        const bool ovf_any = in_clip_any || bus_clip_any;
        const bool collide_any = (snapshot_.collide_mask16 != 0);

        u32 flags = 0;
        flags |= 1u;                 // READY_LAST
        if (ovf_any) flags |= 1u << 1; // OVF_ANY_LAST
        if (collide_any) flags |= 1u << 2; // COLLIDE_ANY_LAST
        // optional: put collide mask in [16..31]
        flags |= (u32(snapshot_.collide_mask16) << 16);

        snapshot_.flags32_last = flags;

        // 3. INTERPHASE_AUTORESET: apply_reset_domain(AUTO_RESET_MASK16) with exclusion of resetters and their ancestors
        if (auto_reset_mask16 != 0) {
            std::array<u8, kTileCount> exclude{};
            exclude.fill(0);
            for (std::size_t t : resetters) {
                if (!is_active_tile(t, active_tile_w_, active_tile_h_)) continue;
                exclude[t] = 1;
                int p = parent_[t];
                while (p >= 0 && is_active_tile(std::size_t(p), active_tile_w_, active_tile_h_)) {
                    exclude[std::size_t(p)] = 1;
                    p = parent_[std::size_t(p)];
                }
            }
            for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
                const u8 d = img_.domain_id[t];
                if ((auto_reset_mask16 & (u16(1u) << d)) == 0) return;
                if (exclude[t]) return;
                thr_cur_[t] = 0;
                locked_[t] = 0;
                const u8 thr_n = thr_norm_4bit(thr_cur_[t], img_.thr_lo[t], img_.thr_hi[t]);
                const bool is_locked = (snapshot_.bake_id_active > 0) ? (locked_[t] != 0) : false;
                snapshot_.v_state[t] = u8((thr_n << 4) | (is_locked ? 0x08u : 0) | 0x00);
            });
        }

        flash_in_progress_ = false;

        // Measure cycle end time and update statistics
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        const u32 cycle_time_us = static_cast<u32>(duration.count());

        // Update cycle time statistics
        snapshot_.cycle_time_us = cycle_time_us;
        if (cycle_time_us > cycle_time_peak_us_) {
            cycle_time_peak_us_ = cycle_time_us;
        }
        snapshot_.cycle_time_peak_us = cycle_time_peak_us_;

        // Calculate average cycle time (simple average over all cycles, maximum 1000)
        cycle_time_sum_us_ += cycle_time_us;
        cycle_count_++;
        constexpr u32 kMaxSamples = 1000;
        if (cycle_count_ > kMaxSamples) {
            // Limit number of samples to prevent overflow
            // Use simple average over last kMaxSamples cycles
            // (approximately, by reducing sum proportionally)
            cycle_time_sum_us_ = (cycle_time_sum_us_ * (kMaxSamples - 1)) / kMaxSamples + cycle_time_us;
            cycle_count_ = kMaxSamples;
        }
        snapshot_.cycle_time_avg_us = static_cast<u32>(cycle_time_sum_us_ / cycle_count_);

        // Fast Path: write to shared buffer for telemetry (lock-free)
        write_to_shared_buffer_();

        res.st = status{};
        res.readout16 = readout;
        res.flags32_last = flags;
        return res;
    }

    void swarm::write_to_shared_buffer_() noexcept {
        if (!shared_buf_) return; // Shared buffer not installed

        // Atomic write of all tiles (memory_order_relaxed for performance)
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            // thr_cur_ is now i16, but shared buffer expects u8 (0..15)
            const i16 thr_i16 = thr_cur_[t];
            const u8 thr = clamp15_u8((std::max)(i16(0), thr_i16));
            const u8 locked = locked_[t];
            const u8 fire = (snapshot_.v_state[t] & 0x04) ? 1 : 0; // Fire_Event bit
            
            write_tile_state(shared_buf_->tiles[t], thr, locked, fire);
        });
        if (clear_inactive_shared_buf_) {
            for (std::size_t t = 0; t < kTileCount; ++t) {
                if (is_active_tile(t, active_tile_w_, active_tile_h_)) {
                    continue;
                }
                write_tile_state(shared_buf_->tiles[t], 0, 0, 0);
            }
            clear_inactive_shared_buf_ = false;
        }
        
        // Frame metadata (frame_tag is stored last as a release "commit")
        shared_buf_->flags32_last.store(snapshot_.flags32_last, std::memory_order_relaxed);
        shared_buf_->bake_id_active.store(snapshot_.bake_id_active, std::memory_order_relaxed);
        shared_buf_->profile_id_active.store(snapshot_.profile_id_active, std::memory_order_relaxed);
        shared_buf_->cycle_time_us.store(snapshot_.cycle_time_us, std::memory_order_relaxed);
        
        // BUS
        for (std::size_t i = 0; i < kLanes; ++i) {
            shared_buf_->bus16[i].store(snapshot_.bus16[i], std::memory_order_relaxed);
        }
        shared_buf_->bus_clip_mask8.store(snapshot_.bus_clip_mask8, std::memory_order_relaxed);
        
        // Domain state
        for (std::size_t d = 0; d < kDomains; ++d) {
            shared_buf_->winner_tile_id[d].store(snapshot_.winner_tile_id[d], std::memory_order_relaxed);
        }
        shared_buf_->auto_reset_mask16.store(snapshot_.auto_reset_mask16, std::memory_order_relaxed);
        shared_buf_->collide_mask16.store(snapshot_.collide_mask16, std::memory_order_relaxed);
        shared_buf_->domains_fired_mask16.store(snapshot_.domains_fired_mask16, std::memory_order_relaxed);

        // Publish frame_tag after all fields to prevent partial reads.
        shared_buf_->frame_tag.store(snapshot_.frame_tag, std::memory_order_release);
    }

    void swarm::compute_domain_connectivity_() noexcept {
        // domain_conn4[t] bits: N=1, E=2, S=4, W=8 if neighbor exists and same domain_id
        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const u8 d = img_.domain_id[t];
            u8 m = 0;
            if (nN_[t] >= 0 && img_.domain_id[std::size_t(nN_[t])] == d) m |= 0x1;
            if (nE_[t] >= 0 && img_.domain_id[std::size_t(nE_[t])] == d) m |= 0x2;
            if (nS_[t] >= 0 && img_.domain_id[std::size_t(nS_[t])] == d) m |= 0x4;
            if (nW_[t] >= 0 && img_.domain_id[std::size_t(nW_[t])] == d) m |= 0x8;
            snapshot_.domain_conn4[t] = m;
        });
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (is_active_tile(t, active_tile_w_, active_tile_h_)) {
                continue;
            }
            snapshot_.domain_conn4[t] = 0;
        }
    }
    
    void swarm::compute_parent_topology_() noexcept {
        // IDE-only: parent by first inbound routing edge (no DAG/layer_id in v0.2)
        for (std::size_t t = 0; t < kTileCount; ++t) {
            children_[t].clear();
        }

        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            parent_[t] = -1;

            // N (North): parent -> t via parent's maskS
            if (nN_[t] >= 0) {
                const std::size_t nN = std::size_t(nN_[t]);
                if (img_.maskS[nN] != 0) { parent_[t] = int(nN); return; }
            }

            // E (East): parent -> t via parent's maskW
            if (nE_[t] >= 0) {
                const std::size_t nE = std::size_t(nE_[t]);
                if (img_.maskW[nE] != 0) { parent_[t] = int(nE); return; }
            }

            // S (South): parent -> t via parent's maskN
            if (nS_[t] >= 0) {
                const std::size_t nS = std::size_t(nS_[t]);
                if (img_.maskN[nS] != 0) { parent_[t] = int(nS); return; }
            }

            // W (West): parent -> t via parent's maskE
            if (nW_[t] >= 0) {
                const std::size_t nW = std::size_t(nW_[t]);
                if (img_.maskE[nW] != 0) { parent_[t] = int(nW); return; }
            }

            // Diagonals
            if (nNE_[t] >= 0) {
                const std::size_t nNE = std::size_t(nNE_[t]);
                if (img_.maskSW[nNE] != 0) { parent_[t] = int(nNE); return; }
            }
            if (nSE_[t] >= 0) {
                const std::size_t nSE = std::size_t(nSE_[t]);
                if (img_.maskNW[nSE] != 0) { parent_[t] = int(nSE); return; }
            }
            if (nSW_[t] >= 0) {
                const std::size_t nSW = std::size_t(nSW_[t]);
                if (img_.maskNE[nSW] != 0) { parent_[t] = int(nSW); return; }
            }
            if (nNW_[t] >= 0) {
                const std::size_t nNW = std::size_t(nNW_[t]);
                if (img_.maskSE[nNW] != 0) { parent_[t] = int(nNW); return; }
            }
        });

        for_each_active_tile(active_tile_w_, active_tile_h_, [&](std::size_t t, std::size_t, std::size_t) noexcept {
            const int parent_id = parent_[t];
            if (parent_id >= 0 && is_active_tile(std::size_t(parent_id), active_tile_w_, active_tile_h_)) {
                children_[std::size_t(parent_id)].push_back(t);
            }
        });
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (is_active_tile(t, active_tile_w_, active_tile_h_)) {
                continue;
            }
            parent_[t] = -1;
        }
    }
    
    status swarm::set_tile_params(std::size_t tile_id, i16 thr_lo, i16 thr_hi, u16 decay16, u8 domain_id, u8 priority) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }
        // Removed flash_in_progress check - parameters can be changed during Play

        // Parameter validation
        if (thr_lo > thr_hi) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "thr_lo must be <= thr_hi" };
        }
        if (decay16 > 32767) {
            return { status::code::BakeReservedNonZero, u32(decay16), "decay16 must be 0..32767" };
        }
        if (domain_id > 15) {
            return { status::code::BakeReservedNonZero, u32(domain_id), "domain_id must be 0..15" };
        }

        // Modify compiled_image
        img_.thr_lo[tile_id] = thr_lo;
        img_.thr_hi[tile_id] = thr_hi;
        img_.decay16[tile_id] = decay16;
        img_.domain_id[tile_id] = domain_id;
        img_.priority[tile_id] = priority;

        // Recalculate domain connectivity (ruler connections)
        compute_domain_connectivity_();

        // Update locked state if range changed
        if (locked_[tile_id]) {
            const i16 thr_cur = thr_cur_[tile_id];
            const bool in_range = (thr_lo < thr_hi) && (thr_lo <= thr_cur) && (thr_cur <= thr_hi);
            if (!in_range) {
                // If range changed and thr_cur fell out of it, unlock
                locked_[tile_id] = 0;
            }
        }

        return status{};
    }

    void swarm::get_tile_weight_visual(std::size_t tile_id, std::uint64_t& out_sign_mask64,
        std::array<u8, 32>& out_mag32) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) { out_sign_mask64 = 0; out_mag32.fill(0); return; }

        // Export exactly in v0.1 packed form: 32 bytes mags (nibbles), 64-bit sign mask LSB-first
        out_mag32.fill(0);
        out_sign_mask64 = 0;

        for (std::size_t k = 0; k < 64; ++k) {
            const u8 mag = img_.w_mag[tile_id * 64 + k] & 0x0F;
            const u8 s1 = img_.w_sign[tile_id * 64 + k] & 1u;

            // mags: 2 nibble per byte
            const std::size_t j = k >> 1;
            const bool hi = (k & 1u) != 0;
            if (!hi) out_mag32[j] = (out_mag32[j] & 0xF0) | mag;
            else     out_mag32[j] = (out_mag32[j] & 0x0F) | u8(mag << 4);

            // signs: bit k (LSB-first) in u64
            if (s1) out_sign_mask64 |= (std::uint64_t(1) << k);
        }
    }
    
    swarm::tile_params swarm::get_tile_params(std::size_t tile_id) const noexcept {
        tile_params params{};
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) return params;
        
        params.thr_lo = img_.thr_lo[tile_id];
        params.thr_hi = img_.thr_hi[tile_id];
        params.decay16 = img_.decay16[tile_id];
        params.domain_id = img_.domain_id[tile_id];
        params.priority = img_.priority[tile_id];
        params.pattern_id = img_.pattern_id[tile_id];
        params.reset_on_fire_mask16 = img_.reset_on_fire_mask16[tile_id];
        
        return params;
    }
    
    status swarm::set_tile_pattern_id(std::size_t tile_id, u16 pattern_id) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }
        if (flash_in_progress_) {
            return { status::code::BadPhase, 0, "flash in progress" };
        }

        // Modify pattern_id in compiled_image
        img_.pattern_id[tile_id] = pattern_id;

        return status{};
    }

    u16 swarm::get_tile_pattern_id(std::size_t tile_id) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) return 0;
        return img_.pattern_id[tile_id];
    }
    
    i16 swarm::get_tile_thr_cur(std::size_t tile_id) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) return 0;
        return thr_cur_[tile_id];
    }
    
    status swarm::set_tile_routing_masks(std::size_t tile_id, u8 maskN, u8 maskE, u8 maskS, u8 maskW, u8 maskNE, u8 maskSE, u8 maskSW, u8 maskNW, u8 bus_w, u8 bus_r) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }
        if (flash_in_progress_) {
            return { status::code::BadPhase, 0, "flash in progress" };
        }

        // Modify routing masks in compiled_image
        img_.maskN[tile_id] = maskN;
        img_.maskE[tile_id] = maskE;
        img_.maskS[tile_id] = maskS;
        img_.maskW[tile_id] = maskW;
        img_.maskNE[tile_id] = maskNE;  // Diagonal: North-East
        img_.maskSE[tile_id] = maskSE;  // Diagonal: South-East
        img_.maskSW[tile_id] = maskSW;  // Diagonal: South-West
        img_.maskNW[tile_id] = maskNW;  // Diagonal: North-West
        img_.bus_w[tile_id] = (bus_w != 0) ? 1 : 0;  // BUS_W
        img_.bus_r[tile_id] = (bus_r != 0) ? 1 : 0;  // BUS_R

        // Recalculate parent topology after changing routing masks (including diagonals)
        compute_parent_topology_();
        compute_domain_connectivity_();
        
        return status{};
    }
    
    int swarm::get_tile_parent_id(std::size_t tile_id) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return -1;  // Invalid tile_id
        }
        if (!bake_applied_) {
            return -1;  // Not baked
        }
        return parent_[tile_id];
    }
    
    std::vector<std::size_t> swarm::get_tile_children(std::size_t tile_id) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_) || !bake_applied_) {
            return {};  // Invalid tile_id or not baked
        }
        return children_[tile_id];  // Return vector copy
    }

    status swarm::set_tile_locked(std::size_t tile_id, bool locked) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (!bake_applied_) {
            return { status::code::NotBaked, 0, "NotBaked" };
        }
        if (flash_in_progress_) {
            return { status::code::BadPhase, 0, "flash in progress" };
        }

        if (locked) {
            // Fuse tile: set locked = 1, clamp thr_cur to range
            locked_[tile_id] = 1;
            const i16 thr_lo = img_.thr_lo[tile_id];
            const i16 thr_hi = img_.thr_hi[tile_id];
            if (thr_cur_[tile_id] < thr_lo) {
                thr_cur_[tile_id] = thr_lo;
            } else if (thr_cur_[tile_id] > thr_hi) {
                thr_cur_[tile_id] = thr_hi;
            }
        } else {
            // Unfuse tile: set locked = 0, DO NOT reset accumulator
            locked_[tile_id] = 0;
        }

        return status{};
    }

    bool swarm::get_tile_locked(std::size_t tile_id) const noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_) || !bake_applied_) {
            return false;
        }
        // CRITICAL: If bake_id_active == 0, then bake is not applied (raw tiles)
        // For raw tiles locked must always be false, regardless of locked_[tile_id] value
        if (snapshot_.bake_id_active == 0) {
            return false;
        }
        return (locked_[tile_id] != 0);
    }
    
    status swarm::set_tile_reset_on_fire_mask16(std::size_t tile_id, u16 reset_on_fire_mask16) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }
        if (flash_in_progress_) {
            return { status::code::BadPhase, 0, "flash in progress" };
        }

        // Modify reset_on_fire_mask16 in compiled_image
        img_.reset_on_fire_mask16[tile_id] = reset_on_fire_mask16;

        return status{};
    }

    swarm::tile_routing_masks swarm::get_tile_routing_masks(std::size_t tile_id) const noexcept {
        tile_routing_masks masks{};
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) return masks;

        masks.maskN = img_.maskN[tile_id];
        masks.maskE = img_.maskE[tile_id];
        masks.maskS = img_.maskS[tile_id];
        masks.maskW = img_.maskW[tile_id];
        masks.maskNE = img_.maskNE[tile_id];  // Diagonal: North-East
        masks.maskSE = img_.maskSE[tile_id];  // Diagonal: South-East
        masks.maskSW = img_.maskSW[tile_id];  // Diagonal: South-West
        masks.maskNW = img_.maskNW[tile_id];  // Diagonal: North-West
        masks.bus_w = img_.bus_w[tile_id];  // BUS_W
        masks.bus_r = img_.bus_r[tile_id];  // BUS_R
        
        return masks;
    }
    
    status swarm::set_tile_weight_sign(std::size_t tile_id, std::size_t weight_idx, bool sign) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (weight_idx >= 64) {
            return { status::code::BakeReservedNonZero, u32(weight_idx), "weight_idx out of range (0..63)" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }

        // Modify weight sign in compiled_image
        const std::size_t idx = tile_id * 64 + weight_idx;
        img_.w_sign[idx] = sign ? 1 : 0;

        return status{};
    }

    status swarm::set_tile_weight_mag(std::size_t tile_id, std::size_t weight_idx, u8 mag) noexcept {
        if (!is_active_tile(tile_id, active_tile_w_, active_tile_h_)) {
            return { status::code::BakeReservedNonZero, u32(tile_id), "tile_id out of range" };
        }
        if (weight_idx >= 64) {
            return { status::code::BakeReservedNonZero, u32(weight_idx), "weight_idx out of range (0..63)" };
        }
        if (mag > 15) {
            return { status::code::BakeReservedNonZero, u32(mag), "mag out of range (0..15)" };
        }
        if (!bake_applied_) {
            return { status::code::BadPhase, 0, "bake not applied" };
        }
        // Removed flash_in_progress check - weights can be changed during Play

        // Modify weight magnitude in compiled_image
        const std::size_t idx = tile_id * 64 + weight_idx;
        img_.w_mag[idx] = mag;

        return status{};
    }

    std::vector<u8> swarm::serialize_current_bake() const noexcept {
        if (!bake_applied_) {
            return {};
        }


        // Create copy of compiled_image for serialization
        bake::compiled_image img_copy = img_;

        // Convert compiled_image to bake_view
        bake::bake_view view{};
        std::vector<u8> temp_buffers;
        auto st = bake::compiled_image_to_view(img_copy, view, temp_buffers);
        if (!st) {
            return {};
        }

        // Estimate output buffer size (approximately same as original or larger)
        const std::size_t estimated_size = 28 + // header
            (16 + 4) + // topology + padding
            (view.tile_params.size() + 4) + // tile_params + padding
            (view.tile_routing.size() + 4) + // tile_routing + padding
            (12 + 4) + // readout + padding
            (view.reset_on_fire.size() + 4) + // reset_on_fire + padding
            (view.weights_packed.size() + 4) + // weights + padding
            (4 + 4) + // CRC + padding
            100; // reserve
        
        std::vector<u8> out_buf(estimated_size);
        std::size_t out_len = 0;
        
        st = bake::serialize_canonical(view, out_buf, out_len);
        if (!st) {
            return {};
        }
        
        out_buf.resize(out_len);
        return out_buf;
    }

    void swarm::set_double_strait_bake(bool enabled) noexcept {
        if (!bake_applied_) {
            return;
        }
        if (enabled) {
            img_.bake_flags |= d8::BAKE_FLAG_DOUBLE_STRAIT;
        } else {
            img_.bake_flags &= ~d8::BAKE_FLAG_DOUBLE_STRAIT;
        }
        double_strait_bake_ = enabled;
    }

    status swarm::set_tile_field_limit(u32 limit) noexcept {
        if (limit != 0 && (limit < 1 || limit > kTileCount)) {
            return { status::code::BakeBadTLVLen, limit, "tile_field_limit out of range" };
        }
        if (limit == 128) {
            limit = 256;
        }
        img_.tile_field_limit = limit;
        return status{};
    }

} // namespace d8
