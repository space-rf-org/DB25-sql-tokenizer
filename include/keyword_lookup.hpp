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

// Fast keyword classification for the tokenizer's hot path.
//
// keywords.hpp is auto-generated and protected; its find_keyword() does an
// ASCII-upcase + O(log N) binary search over the whole keyword table, and it is
// called for EVERY identifier and keyword the lexer produces. This header adds a
// drop-in O(1) alternative built at compile time from the same generated
// KEYWORDS table - a linear-probe hash - and returns exactly the same result.
// It consumes KEYWORDS rather than modifying the generated file. The equivalence
// is locked by a test (LexerInvariantsTest) that checks it against every keyword
// plus a large random-string fuzz.

#include "keywords.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace db25 {

// FNV-1a over raw bytes. Used identically at table-build time (over the
// already-uppercase KEYWORDS text) and at lookup time (over the upcased lexeme),
// so the two hashes always agree for equal byte sequences.
[[nodiscard]] inline constexpr uint32_t keyword_fnv1a(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

// Shortest / longest keyword, derived from the generated table so the
// fast-reject length bounds stay correct if the grammar (and table) change.
inline constexpr size_t kKeywordMinLen = [] {
    size_t m = 64;
    for (const auto& e : KEYWORDS) if (e.length < m) m = e.length;
    return m;
}();
inline constexpr size_t kKeywordMaxLen = [] {
    size_t m = 0;
    for (const auto& e : KEYWORDS) if (e.length > m) m = e.length;
    return m;
}();
static_assert(kKeywordMaxLen <= 32, "upcase buffer below is 32 bytes");

// Linear-probe slot table over KEYWORDS, built at compile time. Size is a power
// of two >= 2 * count, so the load factor stays < 0.5: a lookup miss always
// reaches an empty (-1) slot, guaranteeing the probe loop terminates.
inline constexpr size_t kKeywordSlots = 512;
static_assert(kKeywordSlots >= KEYWORDS.size() * 2, "raise kKeywordSlots for load factor < 0.5");
static_assert(KEYWORDS.size() < 32767, "slot indices are int16_t");

inline constexpr std::array<int16_t, kKeywordSlots> kKeywordSlotTable = [] {
    std::array<int16_t, kKeywordSlots> table{};
    for (auto& s : table) s = -1;
    for (size_t k = 0; k < KEYWORDS.size(); ++k) {
        size_t i = keyword_fnv1a(KEYWORDS[k].text) & (kKeywordSlots - 1);
        while (table[i] != -1) i = (i + 1) & (kKeywordSlots - 1);
        table[i] = static_cast<int16_t>(k);
    }
    return table;
}();

// O(1) equivalent of keywords.hpp's find_keyword(): ASCII case-insensitive,
// returns the keyword id or Keyword::UNKNOWN. Only bytes 'a'-'z' are folded
// (matching the generated function exactly), so a non-ASCII or non-letter byte
// never matches a keyword.
[[nodiscard]] inline Keyword find_keyword_hashed(std::string_view text) noexcept {
    const size_t n = text.length();
    if (n < kKeywordMinLen || n > kKeywordMaxLen) return Keyword::UNKNOWN;

    char upper[32];
    for (size_t i = 0; i < n; ++i) {
        const char c = text[i];
        upper[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    const std::string_view key(upper, n);

    for (size_t i = keyword_fnv1a(key) & (kKeywordSlots - 1); ;
         i = (i + 1) & (kKeywordSlots - 1)) {
        const int16_t s = kKeywordSlotTable[i];
        if (s < 0) return Keyword::UNKNOWN;
        if (KEYWORDS[static_cast<size_t>(s)].text == key) {
            return KEYWORDS[static_cast<size_t>(s)].id;
        }
    }
}

}  // namespace db25
