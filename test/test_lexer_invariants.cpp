/*
 * Lexer invariants - regression guards for two performance cleanups:
 *
 *   1. Source position tracking (line/column). update_position() advances the
 *      line/column over a whitespace run using a vectorized newline scan instead
 *      of a per-byte loop; these cases pin the exact (line, column) of tokens
 *      across leading whitespace, blank lines, tabs, and CRLF.
 *
 *   2. Keyword-table completeness. find_keyword() is the sole keyword classifier
 *      (the redundant is_keyword_simd fallback was removed). It must therefore
 *      resolve EVERY keyword in the table, in any ASCII case - otherwise a
 *      keyword would silently lex as an identifier.
 *
 *   3. Hashed-lookup equivalence. The tokenizer classifies keywords via the O(1)
 *      find_keyword_hashed() (keyword_lookup.hpp); it must return exactly what
 *      the generated binary-search find_keyword() returns, for every keyword and
 *      across a large random-string fuzz.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "simd_tokenizer.hpp"
#include "keywords.hpp"
#include "keyword_lookup.hpp"

using namespace db25;

static int g_fail = 0;

static void expect(bool cond, const std::string& what) {
    if (!cond) { std::printf("  FAIL: %s\n", what.c_str()); ++g_fail; }
}

// First non-whitespace token whose text equals `value`.
static const Token* find_tok(const std::vector<Token>& toks, std::string_view value) {
    for (const auto& t : toks) {
        if (t.type != TokenType::Whitespace && t.value == value) return &t;
    }
    return nullptr;
}

static std::vector<Token> lex(const std::string& sql) {
    SimdTokenizer t(reinterpret_cast<const std::byte*>(sql.data()), sql.size());
    return t.tokenize();
}

// Assert a token's 1-based (line, column).
static void at(const std::vector<Token>& toks, std::string_view value,
               size_t line, size_t col, const std::string& ctx) {
    const Token* t = find_tok(toks, value);
    expect(t != nullptr, ctx + ": token '" + std::string{value} + "' present");
    if (!t) return;
    expect(t->line == line && t->column == col,
           ctx + ": '" + std::string{value} + "' at (" + std::to_string(t->line) +
               "," + std::to_string(t->column) + "), expected (" +
               std::to_string(line) + "," + std::to_string(col) + ")");
}

static void test_positions() {
    std::printf("position tracking\n");

    // The input string MUST outlive the tokens - a Token's `value` is a
    // string_view into it - so each case keeps the SQL in a named local.
    { const std::string s = "SELECT x\nFROM t"; auto t = lex(s);
      at(t, "SELECT", 1, 1, "newline-sep"); at(t, "x", 1, 8, "newline-sep");
      at(t, "FROM", 2, 1, "newline-sep");   at(t, "t", 2, 6, "newline-sep"); }

    { const std::string s = "  SELECT"; auto t = lex(s);       // leading spaces
      at(t, "SELECT", 1, 3, "leading-spaces"); }

    { const std::string s = "a\n\n\nb"; auto t = lex(s);       // blank lines
      at(t, "a", 1, 1, "blank-lines"); at(t, "b", 4, 1, "blank-lines"); }

    { const std::string s = "a\r\nb"; auto t = lex(s);         // CRLF: \r bumps col, \n resets
      at(t, "a", 1, 1, "crlf"); at(t, "b", 2, 1, "crlf"); }

    { const std::string s = "x\t\ty"; auto t = lex(s);         // tabs count as one column each
      at(t, "x", 1, 1, "tabs"); at(t, "y", 1, 4, "tabs"); }

    { const std::string s = "\n\nSELECT"; auto t = lex(s);     // leading blank lines
      at(t, "SELECT", 3, 1, "leading-newlines"); }

    { const std::string s = "SELECT\n   FROM"; auto t = lex(s);  // newline then indentation
      at(t, "FROM", 2, 4, "indent-after-newline"); }

    // Long whitespace runs (>= 16 bytes) exercise update_position's memchr
    // branch; these must match the scalar branch exactly.
    { const std::string s = "a" + std::string(20, ' ') + "b"; auto t = lex(s);  // 20 spaces, no newline
      at(t, "a", 1, 1, "long-run-no-nl"); at(t, "b", 1, 22, "long-run-no-nl"); }

    { const std::string s = "a\n" + std::string(20, ' ') + "b"; auto t = lex(s);  // nl + 20 spaces
      at(t, "a", 1, 1, "long-run-nl"); at(t, "b", 2, 21, "long-run-nl"); }

    { const std::string s = "a" + std::string(10, '\n') + std::string(18, ' ') + "b";
      auto t = lex(s);  // 10 newlines + 18 spaces in one >=16-byte run
      at(t, "a", 1, 1, "long-run-multi-nl"); at(t, "b", 11, 19, "long-run-multi-nl"); }
}

static void test_keyword_completeness() {
    std::printf("keyword-table completeness (%zu keywords)\n", KEYWORDS.size());
    for (const auto& e : KEYWORDS) {
        std::string up(e.text);
        std::string lo(e.text); for (char& c : lo) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        std::string mx(e.text); for (size_t i = 0; i < mx.size(); i += 2)
            if (mx[i] >= 'A' && mx[i] <= 'Z') mx[i] = mx[i] - 'A' + 'a';
        expect(find_keyword(up) == e.id, "upper '" + up + "'");
        expect(find_keyword(lo) == e.id, "lower '" + lo + "'");
        expect(find_keyword(mx) == e.id, "mixed '" + mx + "'");
    }
}

// find_keyword_hashed (the tokenizer's O(1) classifier) must agree with the
// generated find_keyword everywhere: every keyword in each case, and a large
// random-string fuzz spanning lengths 0..40 (empty, over-length, digits,
// underscores, and high bytes), which exercises hits, misses, and the length
// guards.
static void test_hashed_lookup_equivalence() {
    std::printf("hashed-lookup equivalence (keywords + fuzz)\n");
    for (const auto& e : KEYWORDS) {
        std::string up(e.text);
        std::string lo(e.text); for (char& c : lo) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        expect(find_keyword_hashed(up) == find_keyword(up), "kw upper '" + up + "'");
        expect(find_keyword_hashed(lo) == find_keyword(lo), "kw lower '" + lo + "'");
    }
    std::uint32_t rng = 0x9e3779b9u;
    auto next = [&] { rng = rng * 1103515245u + 12345u; return rng; };
    int mismatches = 0;
    for (int i = 0; i < 500000; ++i) {
        const int len = static_cast<int>((next() >> 10) % 41);
        std::string s;
        for (int j = 0; j < len; ++j) {
            const unsigned m = (next() >> 8) % 40;
            unsigned char ch = m < 26 ? ('a' + m)
                             : m < 36 ? ('A' + (m - 26))
                             : m < 37 ? '_'
                             : m < 39 ? ('0' + (m - 37))
                                      : static_cast<unsigned char>(0x80 + m);
            s.push_back(static_cast<char>(ch));
        }
        if (find_keyword_hashed(s) != find_keyword(s)) ++mismatches;
    }
    expect(mismatches == 0, "500k random strings: hashed == generated find_keyword");
}

int main() {
    std::printf("DB25 Tokenizer - Lexer Invariants\n=================================\n\n");
    test_positions();
    test_keyword_completeness();
    test_hashed_lookup_equivalence();

    std::printf("\n%s\n", g_fail == 0
        ? "All tests passed. No regressions detected."
        : "FAILURES detected.");
    return g_fail == 0 ? 0 : 1;
}
