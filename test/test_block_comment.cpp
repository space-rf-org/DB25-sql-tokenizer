/*
 * Block comment test for DB25 SQL Tokenizer
 * Regression test for unterminated block comment handling.
 *
 * Bug: scan_block_comment looped `while (position_ + 1 < input_size_)`, so for
 * an unterminated block comment (a slash-star with no closing star-slash) the
 * loop exited one byte early. The final byte was dropped from the Comment token
 * and re-tokenized as a spurious trailing token (Comment losing its last byte,
 * plus a stray Identifier). The fix consumes to end of input when no closing
 * star-slash is found, mirroring scan_comment's EOF handling.
 */

#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include "simd_tokenizer.hpp"

using namespace db25;

struct ExpectedToken {
    TokenType type;
    std::string value;
};

struct BlockCommentTest {
    std::string sql;
    std::vector<ExpectedToken> expected_tokens;
    std::string description;
};

const char* token_type_to_string(TokenType type) {
    switch (type) {
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

bool test_block_comment(const BlockCommentTest& test) {
    SimdTokenizer tokenizer(
        reinterpret_cast<const std::byte*>(test.sql.data()),
        test.sql.size()
    );

    auto tokens = tokenizer.tokenize();

    // Extract non-whitespace, non-EOF tokens
    std::vector<Token> actual_tokens;
    for (const auto& token : tokens) {
        if (token.type != TokenType::Whitespace &&
            token.type != TokenType::EndOfFile) {
            actual_tokens.push_back(token);
        }
    }

    // Strict comparison of both count and (type, value) of every token
    if (actual_tokens.size() != test.expected_tokens.size()) {
        std::cerr << "✗ FAIL: " << test.description << "\n";
        std::cerr << "  SQL: \"" << test.sql << "\"\n";
        std::cerr << "  Expected " << test.expected_tokens.size()
                  << " tokens, got " << actual_tokens.size() << "\n";
        std::cerr << "  Expected: ";
        for (const auto& t : test.expected_tokens)
            std::cerr << "[" << token_type_to_string(t.type) << ":" << t.value << "] ";
        std::cerr << "\n  Got:      ";
        for (const auto& t : actual_tokens)
            std::cerr << "[" << token_type_to_string(t.type) << ":" << std::string(t.value) << "] ";
        std::cerr << "\n";
        return false;
    }

    for (size_t i = 0; i < actual_tokens.size(); ++i) {
        if (actual_tokens[i].type != test.expected_tokens[i].type ||
            std::string(actual_tokens[i].value) != test.expected_tokens[i].value) {
            std::cerr << "✗ FAIL: " << test.description << "\n";
            std::cerr << "  SQL: \"" << test.sql << "\"\n";
            std::cerr << "  Token " << i << " mismatch: expected ["
                      << token_type_to_string(test.expected_tokens[i].type) << ":"
                      << test.expected_tokens[i].value << "], got ["
                      << token_type_to_string(actual_tokens[i].type) << ":"
                      << std::string(actual_tokens[i].value) << "]\n";
            return false;
        }
    }

    std::cout << "✓ PASS: " << test.description << "\n";
    return true;
}

int main() {
    std::cout << "DB25 Tokenizer - Block Comment Test\n";
    std::cout << "===================================\n\n";

    std::vector<BlockCommentTest> tests = {
        // Regression: unterminated block comment must be ONE Comment token that
        // spans to EOF, with no spurious trailing Identifier.
        {"/* unterminated",
         {{TokenType::Comment, "/* unterminated"}},
         "Unterminated block comment is a single Comment to EOF"},

        // Regression: the originally reported reproducer "/* bad".
        {"/* bad",
         {{TokenType::Comment, "/* bad"}},
         "Unterminated '/* bad' keeps its final byte (no trailing Identifier)"},

        // Control: terminated block comment followed by an identifier.
        {"/* ok */x",
         {{TokenType::Comment, "/* ok */"}, {TokenType::Identifier, "x"}},
         "Terminated block comment then identifier"},

        // Edge: bare "/*" at exact EOF is a single (empty-body) Comment token.
        {"/*",
         {{TokenType::Comment, "/*"}},
         "Bare '/*' at EOF is a single Comment"},

        // Edge: empty terminated comment.
        {"/**/",
         {{TokenType::Comment, "/**/"}},
         "Empty terminated block comment '/**/'"},

        // Edge: unterminated comment ending right after a newline.
        {"/* multi\nline",
         {{TokenType::Comment, "/* multi\nline"}},
         "Unterminated multi-line block comment spans to EOF"},
    };

    int passed = 0;
    int failed = 0;

    for (const auto& test : tests) {
        if (test_block_comment(test)) {
            passed++;
        } else {
            failed++;
            assert(false && "Block comment test failed - tokenizer not handling block comments correctly!");
        }
    }

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "Test Summary\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Total Tests: " << tests.size() << "\n";
    std::cout << "Passed:      " << passed << "\n";
    std::cout << "Failed:      " << failed << "\n";

    if (failed > 0) {
        std::cerr << "\n⚠️  Some block comment tests failed! Please review the failures above.\n";
        return 1;
    } else {
        std::cout << "\n✅ All tests passed. No regressions detected.\n";
        return 0;
    }
}
