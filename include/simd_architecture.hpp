/*
 * Copyright (c) 2024 Chiradip Mandal
 * Author: Chiradip Mandal
 * Organization: Space-RF.org
 * 
 * This file is part of DB25 SQL Tokenizer.
 * 
 * Licensed under the MIT License. See LICENSE file for details.
 */

#pragma once

// ============================================================================
// PROTECTED FILE - DO NOT MODIFY
// ============================================================================
// SIMD architecture abstraction layer - Core component of DB25 SQL Parser.
// Provides optimized SIMD operations for tokenization across x86 and ARM.
// 
// MODIFICATION RESTRICTION: This file is frozen for parser development.
// The SIMD implementations have been tested and optimized.
// ============================================================================

#include "cpu_detection.hpp"
#include "char_classifier.hpp"
#include <cstring>
#include <cstddef>
#include <array>
#include <span>
#include <concepts>
#include <bit>

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
#endif

// ============================================================================
// Function-level SIMD target attribute helper
// ============================================================================
// The SSE4.2 / AVX2 / AVX-512 processors below are chosen at runtime by
// CpuDetection::detect() (see SimdDispatcher). To let those functions be
// compiled even when the translation unit is built for a lower, portable
// baseline (i.e. WITHOUT a global -march flag), each function that uses higher
// ISA intrinsics is annotated with a GCC/Clang "target" attribute naming the
// exact feature set its intrinsics require. The compiler then emits those
// instructions per-function while the rest of the TU stays at the baseline,
// so the resulting binaries remain portable and only execute the wider paths
// on CPUs that actually support them (as gated by the runtime dispatch).
//
// The attribute is a no-op on MSVC (which permits these intrinsics without a
// target attribute) and on non-x86 targets (the NEON paths are built for the
// ARMv8 baseline directly and must stay untouched).
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    #define DB25_TARGET(str) __attribute__((target(str)))
#else
    #define DB25_TARGET(str)
#endif

namespace db25 {

template<typename T>
concept SimdProcessor = requires(T t, const std::byte* data, size_t size) {
    { t.process(data, size) } -> std::same_as<size_t>;
    { T::vector_size() } -> std::convertible_to<size_t>;
};

class ScalarProcessor {
public:
    static constexpr size_t vector_size() noexcept { return 1; }
    
    template<typename Predicate>
    [[nodiscard]] size_t find_delimiter(const std::byte* data, size_t size, Predicate&& pred) const noexcept {
        for (size_t i = 0; i < size; ++i) {
            if (pred(data[i])) {
                return i;
            }
        }
        return size;
    }
    
    [[nodiscard]] size_t find_whitespace(const std::byte* data, size_t size) const noexcept {
        return find_delimiter(data, size, [](std::byte b) {
            return is_whitespace(static_cast<uint8_t>(b));
        });
    }

    [[nodiscard]] size_t skip_whitespace(const std::byte* data, size_t size) const noexcept {
        size_t i = 0;
        while (i < size) {
            if (!is_whitespace(static_cast<uint8_t>(data[i]))) {
                break;
            }
            ++i;
        }
        return i;
    }
    
    [[nodiscard]] bool matches_keyword(const std::byte* data, size_t size, 
                                      const char* keyword, size_t kw_len) const noexcept {
        if (size < kw_len) return false;
        
        for (size_t i = 0; i < kw_len; ++i) {
            uint8_t data_char = static_cast<uint8_t>(data[i]);
            uint8_t kw_char = static_cast<uint8_t>(keyword[i]);
            
            if ((data_char & 0xDF) != (kw_char & 0xDF)) {
                return false;
            }
        }
        
        if (size > kw_len) {
            uint8_t next_char = static_cast<uint8_t>(data[kw_len]);
            return !is_identifier_cont(next_char);
        }
        
        return true;
    }
};

#if defined(__x86_64__) || defined(_M_X64)

class SSE42Processor {
public:
    static constexpr size_t vector_size() noexcept { return 16; }
    
    [[nodiscard]] DB25_TARGET("sse4.2") size_t find_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m128i whitespace = _mm_set_epi8(
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            '\r', '\n', '\t', ' '
        );
        
        size_t i = 0;
        
        for (; i + 16 <= size; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            
            int result = _mm_cmpestri(whitespace, 4, chunk, 16,
                _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_POSITIVE_POLARITY);
            
            if (result < 16) {
                return i + result;
            }
        }
        
        ScalarProcessor scalar;
        return i + scalar.find_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] DB25_TARGET("sse4.2") size_t skip_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m128i whitespace = _mm_set_epi8(
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            '\r', '\n', '\t', ' '
        );
        
        size_t i = 0;
        
        for (; i + 16 <= size; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            
            int result = _mm_cmpestri(whitespace, 4, chunk, 16,
                _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_NEGATIVE_POLARITY);
            
            if (result < 16) {
                return i + result;
            }
        }
        
        ScalarProcessor scalar;
        return i + scalar.skip_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] bool matches_keyword(const std::byte* data, size_t size,
                                      const char* keyword, size_t kw_len) const noexcept {
        ScalarProcessor scalar;
        return scalar.matches_keyword(data, size, keyword, kw_len);
    }
};

class AVX2Processor {
public:
    static constexpr size_t vector_size() noexcept { return 32; }
    
    [[nodiscard]] DB25_TARGET("avx2") size_t find_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m256i space = _mm256_set1_epi8(' ');
        const __m256i tab = _mm256_set1_epi8('\t');
        const __m256i newline = _mm256_set1_epi8('\n');
        const __m256i carriage = _mm256_set1_epi8('\r');
        
        size_t i = 0;
        
        for (; i + 32 <= size; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
            
            __m256i cmp_space = _mm256_cmpeq_epi8(chunk, space);
            __m256i cmp_tab = _mm256_cmpeq_epi8(chunk, tab);
            __m256i cmp_newline = _mm256_cmpeq_epi8(chunk, newline);
            __m256i cmp_carriage = _mm256_cmpeq_epi8(chunk, carriage);
            
            __m256i whitespace = _mm256_or_si256(
                _mm256_or_si256(cmp_space, cmp_tab),
                _mm256_or_si256(cmp_newline, cmp_carriage)
            );
            
            uint32_t mask = _mm256_movemask_epi8(whitespace);
            if (mask != 0) {
                return i + std::countr_zero(mask);
            }
        }
        
        SSE42Processor sse42;
        return i + sse42.find_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] DB25_TARGET("avx2") size_t skip_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m256i space = _mm256_set1_epi8(' ');
        const __m256i tab = _mm256_set1_epi8('\t');
        const __m256i newline = _mm256_set1_epi8('\n');
        const __m256i carriage = _mm256_set1_epi8('\r');
        
        size_t i = 0;
        
        for (; i + 32 <= size; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
            
            __m256i cmp_space = _mm256_cmpeq_epi8(chunk, space);
            __m256i cmp_tab = _mm256_cmpeq_epi8(chunk, tab);
            __m256i cmp_newline = _mm256_cmpeq_epi8(chunk, newline);
            __m256i cmp_carriage = _mm256_cmpeq_epi8(chunk, carriage);
            
            __m256i whitespace = _mm256_or_si256(
                _mm256_or_si256(cmp_space, cmp_tab),
                _mm256_or_si256(cmp_newline, cmp_carriage)
            );
            
            uint32_t mask = ~_mm256_movemask_epi8(whitespace);
            if (mask != 0) {
                return i + std::countr_zero(mask);
            }
        }
        
        SSE42Processor sse42;
        return i + sse42.skip_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] DB25_TARGET("avx2") bool matches_keyword(const std::byte* data, size_t size,
                                      const char* keyword, size_t kw_len) const noexcept {
        if (size < kw_len || kw_len > 32) {
            ScalarProcessor scalar;
            return scalar.matches_keyword(data, size, keyword, kw_len);
        }
        
        alignas(32) char upper_data[32] = {};
        alignas(32) char upper_kw[32] = {};
        
        std::memcpy(upper_data, data, std::min(size, size_t(32)));
        std::memcpy(upper_kw, keyword, kw_len);
        
        __m256i data_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(upper_data));
        __m256i kw_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(upper_kw));
        
        const __m256i lower_mask = _mm256_set1_epi8(static_cast<char>(0xDF));
        data_vec = _mm256_and_si256(data_vec, lower_mask);
        kw_vec = _mm256_and_si256(kw_vec, lower_mask);
        
        __m256i cmp = _mm256_cmpeq_epi8(data_vec, kw_vec);
        uint32_t mask = _mm256_movemask_epi8(cmp);
        
        uint32_t expected_mask = (1U << kw_len) - 1;
        return (mask & expected_mask) == expected_mask;
    }
};

class AVX512Processor {
public:
    static constexpr size_t vector_size() noexcept { return 64; }
    
    [[nodiscard]] DB25_TARGET("avx512f,avx512bw") size_t find_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m512i space = _mm512_set1_epi8(' ');
        const __m512i tab = _mm512_set1_epi8('\t');
        const __m512i newline = _mm512_set1_epi8('\n');
        const __m512i carriage = _mm512_set1_epi8('\r');
        
        size_t i = 0;
        
        for (; i + 64 <= size; i += 64) {
            __m512i chunk = _mm512_loadu_si512(data + i);
            
            __mmask64 cmp_space = _mm512_cmpeq_epi8_mask(chunk, space);
            __mmask64 cmp_tab = _mm512_cmpeq_epi8_mask(chunk, tab);
            __mmask64 cmp_newline = _mm512_cmpeq_epi8_mask(chunk, newline);
            __mmask64 cmp_carriage = _mm512_cmpeq_epi8_mask(chunk, carriage);
            
            __mmask64 whitespace = cmp_space | cmp_tab | cmp_newline | cmp_carriage;
            
            if (whitespace != 0) {
                return i + std::countr_zero(whitespace);
            }
        }
        
        AVX2Processor avx2;
        return i + avx2.find_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] DB25_TARGET("avx512f,avx512bw") size_t skip_whitespace(const std::byte* data, size_t size) const noexcept {
        const __m512i space = _mm512_set1_epi8(' ');
        const __m512i tab = _mm512_set1_epi8('\t');
        const __m512i newline = _mm512_set1_epi8('\n');
        const __m512i carriage = _mm512_set1_epi8('\r');
        
        size_t i = 0;
        
        for (; i + 64 <= size; i += 64) {
            __m512i chunk = _mm512_loadu_si512(data + i);
            
            __mmask64 cmp_space = _mm512_cmpeq_epi8_mask(chunk, space);
            __mmask64 cmp_tab = _mm512_cmpeq_epi8_mask(chunk, tab);
            __mmask64 cmp_newline = _mm512_cmpeq_epi8_mask(chunk, newline);
            __mmask64 cmp_carriage = _mm512_cmpeq_epi8_mask(chunk, carriage);
            
            __mmask64 whitespace = cmp_space | cmp_tab | cmp_newline | cmp_carriage;
            __mmask64 non_whitespace = ~whitespace;
            
            if (non_whitespace != 0) {
                return i + std::countr_zero(non_whitespace);
            }
        }
        
        AVX2Processor avx2;
        return i + avx2.skip_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] bool matches_keyword(const std::byte* data, size_t size,
                                      const char* keyword, size_t kw_len) const noexcept {
        AVX2Processor avx2;
        return avx2.matches_keyword(data, size, keyword, kw_len);
    }
};

#elif defined(__aarch64__) || defined(_M_ARM64)

class NeonProcessor {
public:
    static constexpr size_t vector_size() noexcept { return 16; }
    
    [[nodiscard]] size_t find_whitespace(const std::byte* data, size_t size) const noexcept {
        const uint8x16_t space = vdupq_n_u8(' ');
        const uint8x16_t tab = vdupq_n_u8('\t');
        const uint8x16_t newline = vdupq_n_u8('\n');
        const uint8x16_t carriage = vdupq_n_u8('\r');
        
        size_t i = 0;
        
        for (; i + 16 <= size; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
            
            uint8x16_t cmp_space = vceqq_u8(chunk, space);
            uint8x16_t cmp_tab = vceqq_u8(chunk, tab);
            uint8x16_t cmp_newline = vceqq_u8(chunk, newline);
            uint8x16_t cmp_carriage = vceqq_u8(chunk, carriage);
            
            uint8x16_t whitespace = vorrq_u8(
                vorrq_u8(cmp_space, cmp_tab),
                vorrq_u8(cmp_newline, cmp_carriage)
            );
            
            uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(whitespace), 4);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
            
            if (mask != 0) {
                for (size_t j = 0; j < 16; ++j) {
                    uint8_t lanes[16];
                    vst1q_u8(lanes, whitespace);
                    if (lanes[j]) {
                        return i + j;
                    }
                }
            }
        }
        
        ScalarProcessor scalar;
        return i + scalar.find_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] size_t skip_whitespace(const std::byte* data, size_t size) const noexcept {
        const uint8x16_t space = vdupq_n_u8(' ');
        const uint8x16_t tab = vdupq_n_u8('\t');
        const uint8x16_t newline = vdupq_n_u8('\n');
        const uint8x16_t carriage = vdupq_n_u8('\r');
        
        size_t i = 0;
        
        for (; i + 16 <= size; i += 16) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
            
            uint8x16_t cmp_space = vceqq_u8(chunk, space);
            uint8x16_t cmp_tab = vceqq_u8(chunk, tab);
            uint8x16_t cmp_newline = vceqq_u8(chunk, newline);
            uint8x16_t cmp_carriage = vceqq_u8(chunk, carriage);
            
            uint8x16_t whitespace = vorrq_u8(
                vorrq_u8(cmp_space, cmp_tab),
                vorrq_u8(cmp_newline, cmp_carriage)
            );
            
            uint8x16_t non_whitespace = vmvnq_u8(whitespace);
            
            for (size_t j = 0; j < 16; ++j) {
                uint8_t lanes[16];
                vst1q_u8(lanes, non_whitespace);
                if (lanes[j]) {
                    return i + j;
                }
            }
        }
        
        ScalarProcessor scalar;
        return i + scalar.skip_whitespace(data + i, size - i);
    }
    
    [[nodiscard]] bool matches_keyword(const std::byte* data, size_t size,
                                      const char* keyword, size_t kw_len) const noexcept {
        if (size < kw_len || kw_len > 16) {
            ScalarProcessor scalar;
            return scalar.matches_keyword(data, size, keyword, kw_len);
        }
        
        uint8x16_t data_vec = vld1q_u8(reinterpret_cast<const uint8_t*>(data));
        
        alignas(16) uint8_t kw_buffer[16] = {};
        std::memcpy(kw_buffer, keyword, kw_len);
        uint8x16_t kw_vec = vld1q_u8(kw_buffer);
        
        const uint8x16_t lower_mask = vdupq_n_u8(0xDF);
        data_vec = vandq_u8(data_vec, lower_mask);
        kw_vec = vandq_u8(kw_vec, lower_mask);
        
        uint8x16_t cmp = vceqq_u8(data_vec, kw_vec);
        
        uint8_t lanes[16];
        vst1q_u8(lanes, cmp);
        
        for (size_t i = 0; i < kw_len; ++i) {
            if (!lanes[i]) {
                return false;
            }
        }
        
        // Check word boundary
        if (size > kw_len) {
            uint8_t next_char = static_cast<uint8_t>(data[kw_len]);
            return !is_identifier_cont(next_char);
        }
        
        return true;
    }
};

#endif

class SimdDispatcher {
private:
    SimdLevel level_;
    
public:
    SimdDispatcher() : level_(CpuDetection::detect()) {}
    
    template<typename Func>
    auto dispatch(Func&& func) const {
        #if defined(__x86_64__) || defined(_M_X64)
        switch (level_) {
            case SimdLevel::AVX512:
                return func(AVX512Processor{});
            case SimdLevel::AVX2:
                return func(AVX2Processor{});
            case SimdLevel::SSE42:
                return func(SSE42Processor{});
            default:
                return func(ScalarProcessor{});
        }
        #elif defined(__aarch64__) || defined(_M_ARM64)
        if (level_ == SimdLevel::NEON) {
            return func(NeonProcessor{});
        }
        return func(ScalarProcessor{});
        #else
        return func(ScalarProcessor{});
        #endif
    }
    
    [[nodiscard]] SimdLevel level() const noexcept { return level_; }
    [[nodiscard]] const char* level_name() const noexcept { return CpuDetection::level_name(); }
};

}  // namespace db25