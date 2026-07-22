/*
 * Number-literal tokenization test for DB25 SQL Tokenizer
 *
 * Covers the lexeme boundaries of numeric literals, in particular:
 *   - hex (0x..) and binary (0b..) integer literals, which must be a single
 *     Number token rather than `0` + an identifier tail (the old behaviour
 *     silently read `0xFF` as the number 0 followed by an alias `xFF`);
 *   - leading-dot floats (`.5`), which must lex as a Number, not the `.`
 *     delimiter followed by `5`;
 *   - the existing decimal / exponent forms, as a regression guard;
 *   - shapes that must NOT be swallowed by the new rules: a bare `0`, the `.`
 *     member operator, and `t.*`.
 */

#include <iostream>
#include <string>
#include <vector>
#include "simd_tokenizer.hpp"

using namespace db25;

struct TokExp {
    TokenType type;
    std::string value;
};

struct TestCase {
    std::string sql;
    std::vector<TokExp> expected;  // non-whitespace, non-EOF tokens
    std::string description;
};

static const char* type_name(TokenType t) {
    switch (t) {
        case TokenType::Unknown: return "Unknown";
        case TokenType::Keyword: return "Keyword";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::Operator: return "Operator";
        case TokenType::Delimiter: return "Delimiter";
        case TokenType::Comment: return "Comment";
        case TokenType::Whitespace: return "Whitespace";
        case TokenType::EndOfFile: return "EOF";
        default: return "?";
    }
}

static bool run(const TestCase& test) {
    SimdTokenizer tokenizer(
        reinterpret_cast<const std::byte*>(test.sql.data()),
        test.sql.size());
    auto tokens = tokenizer.tokenize();

    std::vector<Token> actual;
    for (const auto& tok : tokens) {
        if (tok.type != TokenType::Whitespace && tok.type != TokenType::EndOfFile) {
            actual.push_back(tok);
        }
    }

    bool ok = actual.size() == test.expected.size();
    for (size_t i = 0; ok && i < actual.size(); ++i) {
        if (actual[i].type != test.expected[i].type ||
            std::string(actual[i].value) != test.expected[i].value) {
            ok = false;
        }
    }

    if (!ok) {
        std::cout << "✗ FAIL: " << test.description << "\n";
        std::cout << "  SQL: \"" << test.sql << "\"\n";
        std::cout << "  Expected: ";
        for (const auto& e : test.expected)
            std::cout << "[" << type_name(e.type) << ":" << e.value << "] ";
        std::cout << "\n  Got:      ";
        for (const auto& a : actual)
            std::cout << "[" << type_name(a.type) << ":" << std::string(a.value) << "] ";
        std::cout << "\n";
        return false;
    }

    std::cout << "✓ PASS: " << test.description << "\n";
    return true;
}

int main() {
    std::cout << "DB25 Tokenizer - Number Literal Test\n";
    std::cout << "====================================\n\n";

    const auto N = TokenType::Number;
    const auto I = TokenType::Identifier;
    const auto O = TokenType::Operator;
    const auto D = TokenType::Delimiter;

    std::vector<TestCase> tests = {
        // Hex literals: whole prefixed literal is one Number token.
        {"0xFF", {{N, "0xFF"}}, "Hex literal uppercase digits"},
        {"0xff", {{N, "0xff"}}, "Hex literal lowercase digits"},
        {"0X1a2B", {{N, "0X1a2B"}}, "Hex literal uppercase X, mixed case"},
        {"0x0", {{N, "0x0"}}, "Hex literal zero"},
        {"SELECT 0xFF", {{TokenType::Keyword, "SELECT"}, {N, "0xFF"}},
         "Hex literal after keyword"},
        {"0xFF+1", {{N, "0xFF"}, {O, "+"}, {N, "1"}}, "Hex literal then operator"},

        // Binary literals.
        {"0b1010", {{N, "0b1010"}}, "Binary literal"},
        {"0B1101", {{N, "0B1101"}}, "Binary literal uppercase B"},
        {"0b0", {{N, "0b0"}}, "Binary literal zero"},

        // Leading-dot floats.
        {".5", {{N, ".5"}}, "Leading-dot float"},
        {".25e3", {{N, ".25e3"}}, "Leading-dot float with exponent"},
        {"(.5)", {{D, "("}, {N, ".5"}, {D, ")"}}, "Leading-dot float in parens"},

        // Regression: existing decimal / exponent forms unchanged.
        {"0", {{N, "0"}}, "Bare zero stays a number"},
        {"42", {{N, "42"}}, "Plain integer"},
        {"3.14", {{N, "3.14"}}, "Decimal"},
        {"5.", {{N, "5."}}, "Trailing-dot float"},
        {"1e10", {{N, "1e10"}}, "Exponent"},
        {"1.5E-3", {{N, "1.5E-3"}}, "Signed exponent"},

        // Regression: the `.` member operator and `t.*` must NOT be eaten into a
        // number when no digit follows (the tokenizer emits `.` as an Operator).
        {"a.b", {{I, "a"}, {O, "."}, {I, "b"}}, "Member dot untouched"},
        {"t.*", {{I, "t"}, {O, "."}, {O, "*"}}, "t.* untouched"},
    };

    int passed = 0, failed = 0;
    for (const auto& t : tests) {
        if (run(t)) ++passed; else ++failed;
    }

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "Total: " << tests.size() << "  Passed: " << passed
              << "  Failed: " << failed << "\n";

    if (failed > 0) {
        std::cout << "\nSome tests failed! Please review the failures above.\n";
        return 1;
    }
    std::cout << "\nAll tests passed. No regressions detected.\n";
    return 0;
}
