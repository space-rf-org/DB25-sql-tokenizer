/*
 * Delimited-identifier tokenization test for DB25 SQL Tokenizer
 *
 * A double-quoted lexeme is a delimited identifier (standard SQL / PostgreSQL /
 * DuckDB), not a string literal. It must:
 *   - emit an Identifier token whose value is the INNER text (quotes stripped);
 *   - never be keyword-matched, so "select" is an identifier, not the keyword;
 *   - preserve spaces, punctuation, and case inside the quotes;
 *   - treat a doubled "" as an escaped quote that does not terminate it.
 * Single-quoted lexemes must still tokenize as String literals (unchanged).
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
        std::cout << "FAIL: " << test.description << "\n";
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

    std::cout << "PASS: " << test.description << "\n";
    return true;
}

int main() {
    std::cout << "DB25 Tokenizer - Delimited Identifier Test\n";
    std::cout << "==========================================\n\n";

    const auto ID = TokenType::Identifier;
    const auto KW = TokenType::Keyword;
    const auto STR = TokenType::String;
    const auto OP = TokenType::Operator;
    const auto DL = TokenType::Delimiter;

    std::vector<TestCase> tests = {
        // Value is the inner text, quotes stripped.
        {"\"col\"", {{ID, "col"}}, "Simple delimited identifier"},
        {"\"User Name\"", {{ID, "User Name"}}, "Identifier with a space"},
        {"\"column-with-dashes\"", {{ID, "column-with-dashes"}}, "Identifier with dashes"},
        {"\"MixedCase\"", {{ID, "MixedCase"}}, "Case is preserved"},

        // A keyword inside quotes is an identifier, not a keyword.
        {"\"select\"", {{ID, "select"}}, "Keyword as delimited identifier"},
        {"\"FROM\"", {{ID, "FROM"}}, "Uppercase keyword as identifier"},

        // Doubled "" is an escaped quote, not a terminator (one token).
        {"\"a\"\"b\"", {{ID, "a\"\"b"}}, "Escaped doubled quote stays one token"},

        // In context.
        {"SELECT \"id\" FROM t",
         {{KW, "SELECT"}, {ID, "id"}, {KW, "FROM"}, {ID, "t"}},
         "Delimited identifier in a SELECT list"},
        {"\"t\".\"c\"",
         {{ID, "t"}, {OP, "."}, {ID, "c"}},
         "Qualified delimited identifier"},

        // Single quotes are STILL string literals (regression guard).
        {"'hello'", {{STR, "'hello'"}}, "Single-quoted stays a String"},
        {"WHERE x = 'active'",
         {{KW, "WHERE"}, {ID, "x"}, {OP, "="}, {STR, "'active'"}},
         "Single-quoted string in a predicate"},

        // A double quote inside a single-quoted string is just string content.
        {"'{\"active\": true}'",
         {{STR, "'{\"active\": true}'"}},
         "Double quote inside a single-quoted string is untouched"},

        // Alias form.
        {"SELECT x AS \"my col\" FROM t",
         {{KW, "SELECT"}, {ID, "x"}, {KW, "AS"}, {ID, "my col"}, {KW, "FROM"}, {ID, "t"}},
         "Delimited alias"},
    };
    (void)DL;

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
