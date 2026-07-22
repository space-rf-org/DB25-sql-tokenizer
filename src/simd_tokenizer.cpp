/*
 * Copyright (c) 2024 Chiradip Mandal
 * Author: Chiradip Mandal
 * Organization: Space-RF.org
 * 
 * This file is part of DB25 SQL Tokenizer.
 * 
 * Licensed under the MIT License. See LICENSE file for details.
 */

#include "simd_tokenizer.hpp"
#include "char_classifier.hpp"

#include <cstring>  // std::memchr (vectorized newline scan in update_position)

namespace db25 {

SimdTokenizer::SimdTokenizer(const std::byte* input, size_t size)
        : input_(input)
        , input_size_(size)
        , position_(0)
        , line_(1)
        , column_(1) {}
    
[[nodiscard]] std::vector<Token> SimdTokenizer::tokenize() {
        std::vector<Token> tokens;
        tokens.reserve(input_size_ / 8);
        
        while (position_ < input_size_) {
            size_t skip = dispatcher_.dispatch([this](auto processor) {
                return processor.skip_whitespace(
                    input_ + position_, 
                    input_size_ - position_
                );
            });
            
            if (skip > 0) {
                update_position(skip);
            }
            
            if (position_ >= input_size_) {
                break;
            }
            
            Token token = next_token();
            if (token.type != TokenType::Whitespace) {
                tokens.push_back(token);
            }
            
            if (token.type == TokenType::EndOfFile) {
                break;
            }
        }
        
        return tokens;
    }
    
[[nodiscard]] const char* SimdTokenizer::simd_level() const noexcept {
    return dispatcher_.level_name();
}

Token SimdTokenizer::next_token() {
        if (position_ >= input_size_) {
            return {TokenType::EndOfFile, "", Keyword::UNKNOWN, line_, column_};
        }
        
        size_t start = position_;
        size_t start_line = line_;
        size_t start_column = column_;
        
        uint8_t first_char = static_cast<uint8_t>(input_[position_]);

        // Use lookup table for character classification
        if (is_identifier_start(first_char)) {
            return scan_identifier_or_keyword(start, start_line, start_column);
        }

        if (is_digit(first_char)) {
            return scan_number(start, start_line, start_column);
        }

        // Leading-dot float: `.5` is a number, not the `.` delimiter. Only when a
        // digit follows the dot (so member/qualifier `.` and `t.*` are untouched).
        if (first_char == '.' && position_ + 1 < input_size_ &&
            is_digit(static_cast<uint8_t>(input_[position_ + 1]))) {
            return scan_number(start, start_line, start_column);
        }

        // Double quote opens a delimited identifier (standard SQL / PostgreSQL /
        // DuckDB), NOT a string literal: "select" is a column named select, and
        // "user name" a column whose name has a space. It must be intercepted
        // before the generic quote path, which would otherwise lex it as a
        // String. Single quotes still make a string literal below.
        if (first_char == '"') {
            return scan_delimited_identifier(start, start_line, start_column);
        }

        if (is_quote(first_char)) {
            return scan_string(start, start_line, start_column, first_char);
        }

        if (first_char == '-' && position_ + 1 < input_size_ &&
            static_cast<uint8_t>(input_[position_ + 1]) == '-') {
            return scan_comment(start, start_line, start_column);
        }
        
        if (first_char == '/' && position_ + 1 < input_size_ &&
            static_cast<uint8_t>(input_[position_ + 1]) == '*') {
            return scan_block_comment(start, start_line, start_column);
        }
        
        return scan_operator_or_delimiter(start, start_line, start_column);
    }

Token SimdTokenizer::scan_identifier_or_keyword(size_t start, size_t start_line, size_t start_column) {
        while (position_ < input_size_) {
            uint8_t ch = static_cast<uint8_t>(input_[position_]);
            if (!is_identifier_cont(ch)) {
                break;
            }
            ++position_;
            ++column_;
        }
        
        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );
        
        // find_keyword is a complete, case-insensitive lookup over the whole
        // keyword table (binary search after ASCII-upcasing), so it is
        // authoritative: if it returns UNKNOWN the lexeme is not a keyword in any
        // case. The previous SIMD fallback (is_keyword_simd) only ran after this
        // returned UNKNOWN and checked the SAME table with the SAME case-folding,
        // so it could never turn an identifier into a keyword - it was dead work
        // on every identifier. Removed.
        Keyword kw = find_keyword(value);
        TokenType type = (kw != Keyword::UNKNOWN) ? TokenType::Keyword : TokenType::Identifier;

        return {type, value, kw, start_line, start_column};
    }

Token SimdTokenizer::scan_number(size_t start, size_t start_line, size_t start_column) {
        bool has_dot = false;
        bool has_exp = false;

        // Hex (0x..) / binary (0b..) integer literals: a leading '0' followed by
        // x/X (base 16) or b/B (base 2) scans the whole prefixed literal as a
        // single Number token, instead of stopping at the letter and leaving the
        // rest to be mis-read as an identifier (which turned `0xFF` into `0` + an
        // alias `xFF`, i.e. a silent value of 0). The value is parsed downstream.
        if (static_cast<uint8_t>(input_[position_]) == '0' &&
            position_ + 1 < input_size_) {
            const uint8_t radix = static_cast<uint8_t>(input_[position_ + 1]);
            const auto is_hex = [](uint8_t c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
            };
            if (radix == 'x' || radix == 'X') {
                position_ += 2;
                column_ += 2;
                while (position_ < input_size_ &&
                       is_hex(static_cast<uint8_t>(input_[position_]))) {
                    ++position_;
                    ++column_;
                }
                return {TokenType::Number,
                        std::string_view(reinterpret_cast<const char*>(input_ + start),
                                         position_ - start),
                        Keyword::UNKNOWN, start_line, start_column};
            }
            if (radix == 'b' || radix == 'B') {
                position_ += 2;
                column_ += 2;
                while (position_ < input_size_ &&
                       (static_cast<uint8_t>(input_[position_]) == '0' ||
                        static_cast<uint8_t>(input_[position_]) == '1')) {
                    ++position_;
                    ++column_;
                }
                return {TokenType::Number,
                        std::string_view(reinterpret_cast<const char*>(input_ + start),
                                         position_ - start),
                        Keyword::UNKNOWN, start_line, start_column};
            }
        }

        while (position_ < input_size_) {
            uint8_t ch = static_cast<uint8_t>(input_[position_]);

            if (is_digit(ch)) {
                ++position_;
                ++column_;
            } else if (ch == '.' && !has_dot && !has_exp) {
                has_dot = true;
                ++position_;
                ++column_;
            } else if ((ch == 'e' || ch == 'E') && !has_exp) {
                has_exp = true;
                ++position_;
                ++column_;
                
                if (position_ < input_size_) {
                    ch = static_cast<uint8_t>(input_[position_]);
                    if (ch == '+' || ch == '-') {
                        ++position_;
                        ++column_;
                    }
                }
            } else {
                break;
            }
        }
        
        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );
        
        return {TokenType::Number, value, Keyword::UNKNOWN, start_line, start_column};
    }

Token SimdTokenizer::scan_string(size_t start, size_t start_line, size_t start_column, uint8_t quote) {
        ++position_;
        ++column_;
        
        while (position_ < input_size_) {
            uint8_t ch = static_cast<uint8_t>(input_[position_]);
            
            if (ch == quote) {
                if (position_ + 1 < input_size_ &&
                    static_cast<uint8_t>(input_[position_ + 1]) == quote) {
                    position_ += 2;
                    column_ += 2;
                } else {
                    ++position_;
                    ++column_;
                    break;
                }
            } else if (ch == '\n') {
                ++position_;
                ++line_;
                column_ = 1;
            } else {
                ++position_;
                ++column_;
            }
        }
        
        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );
        
        return {TokenType::String, value, Keyword::UNKNOWN, start_line, start_column};
    }

Token SimdTokenizer::scan_delimited_identifier(size_t start, size_t start_line, size_t start_column) {
        // start points at the opening '"'. The token value is the INNER text
        // (the surrounding quotes stripped) so downstream code sees a plain
        // identifier and never keyword-matches it. A doubled quote ("") inside
        // is the SQL escape for one '"' and does not terminate the identifier;
        // the escape is left un-collapsed in the value (a rare shape) so the
        // lexer stays allocation-free - only the token boundary matters here.
        (void)start;
        ++position_;  // consume opening quote
        ++column_;
        const size_t inner_start = position_;

        while (position_ < input_size_) {
            uint8_t ch = static_cast<uint8_t>(input_[position_]);
            if (ch == '"') {
                if (position_ + 1 < input_size_ &&
                    static_cast<uint8_t>(input_[position_ + 1]) == '"') {
                    position_ += 2;  // escaped "" - part of the identifier
                    column_ += 2;
                    continue;
                }
                // Closing quote: value spans the inner text, quote excluded.
                std::string_view value(
                    reinterpret_cast<const char*>(input_ + inner_start),
                    position_ - inner_start);
                ++position_;  // consume closing quote
                ++column_;
                return {TokenType::Identifier, value, Keyword::UNKNOWN,
                        start_line, start_column};
            }
            if (ch == '\n') {
                ++position_;
                ++line_;
                column_ = 1;
            } else {
                ++position_;
                ++column_;
            }
        }

        // Unterminated: take the inner text to end of input (mirrors the string
        // scanner's lenient EOF handling) rather than dropping the token.
        std::string_view value(
            reinterpret_cast<const char*>(input_ + inner_start),
            position_ - inner_start);
        return {TokenType::Identifier, value, Keyword::UNKNOWN,
                start_line, start_column};
    }

Token SimdTokenizer::scan_comment(size_t start, size_t start_line, size_t start_column) {
        position_ += 2;
        column_ += 2;
        
        while (position_ < input_size_) {
            if (static_cast<uint8_t>(input_[position_]) == '\n') {
                ++position_;
                ++line_;
                column_ = 1;
                break;
            }
            ++position_;
            ++column_;
        }
        
        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );
        
        return {TokenType::Comment, value, Keyword::UNKNOWN, start_line, start_column};
    }

Token SimdTokenizer::scan_block_comment(size_t start, size_t start_line, size_t start_column) {
        position_ += 2;
        column_ += 2;

        bool terminated = false;
        while (position_ + 1 < input_size_) {
            if (static_cast<uint8_t>(input_[position_]) == '*' &&
                static_cast<uint8_t>(input_[position_ + 1]) == '/') {
                position_ += 2;
                column_ += 2;
                terminated = true;
                break;
            } else if (static_cast<uint8_t>(input_[position_]) == '\n') {
                ++position_;
                ++line_;
                column_ = 1;
            } else {
                ++position_;
                ++column_;
            }
        }

        // Unterminated block comment: the loop above stops one byte early
        // (position_ + 1 < input_size_), which would leave a trailing byte to be
        // re-tokenized as a spurious token. Consume the remainder so the whole
        // input becomes a single Comment token, mirroring scan_comment's EOF
        // handling.
        if (!terminated) {
            position_ = input_size_;
        }

        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );

        return {TokenType::Comment, value, Keyword::UNKNOWN, start_line, start_column};
    }

Token SimdTokenizer::scan_operator_or_delimiter(size_t start, size_t start_line, size_t start_column) {
        uint8_t ch = static_cast<uint8_t>(input_[position_]);
        ++position_;
        ++column_;

        // Use lookup table to determine token type
        TokenType type = is_delimiter(ch) ? TokenType::Delimiter : TokenType::Operator;
        
        if (position_ < input_size_) {
            uint8_t next = static_cast<uint8_t>(input_[position_]);
            
            if ((ch == '<' && (next == '=' || next == '>')) ||
                (ch == '>' && next == '=') ||
                (ch == '!' && next == '=') ||
                (ch == '=' && next == '=') ||  // Add support for ==
                (ch == '|' && next == '|') ||
                (ch == '&' && next == '&') ||
                (ch == ':' && next == ':') ||
                (ch == '<' && next == '<') ||
                (ch == '>' && next == '>')) {
                ++position_;
                ++column_;
            }
        }
        
        std::string_view value(
            reinterpret_cast<const char*>(input_ + start),
            position_ - start
        );
        
        return {type, value, Keyword::UNKNOWN, start_line, start_column};
    }

void SimdTokenizer::update_position(size_t count) {
        // Advance the source line/column over a run of `count` bytes (the
        // whitespace skip_whitespace just measured).
        //
        // Short runs - the overwhelmingly common case, a single space or a few
        // spaces between tokens - use the tight scalar loop: for so few bytes it
        // beats std::memchr's call overhead (measured: memchr loses below ~16
        // bytes, wins well above it). Long runs - deep indentation, blank-line
        // gaps in formatted SQL - use memchr's vectorized newline scan, which is
        // ~1.5x faster there. Both branches produce identical line/column, so the
        // threshold is a pure performance knob, never a behavior change.
        constexpr size_t kMemchrThreshold = 16;
        if (count < kMemchrThreshold) {
            for (size_t i = 0; i < count; ++i) {
                if (static_cast<uint8_t>(input_[position_]) == '\n') {
                    ++line_;
                    column_ = 1;
                } else {
                    ++column_;
                }
                ++position_;
            }
            return;
        }

        // Long run: scan for newlines with memchr instead of walking every byte.
        // The no-newline case reduces to a single column add; with newlines the
        // final column is the distance past the last '\n' (1-based).
        const char* run = reinterpret_cast<const char*>(input_ + position_);
        const void* first_nl = std::memchr(run, '\n', count);
        if (first_nl == nullptr) {
            column_ += count;
        } else {
            size_t nlines = 0;
            size_t last = 0;  // offset of the last '\n' within the run
            const char* p = run;
            size_t remaining = count;
            for (const void* hit = first_nl; hit != nullptr;
                 hit = std::memchr(p, '\n', remaining)) {
                ++nlines;
                const size_t off = static_cast<size_t>(static_cast<const char*>(hit) - run);
                last = off;
                p = run + off + 1;
                remaining = count - (off + 1);
                if (remaining == 0) {
                    break;
                }
            }
            line_ += nlines;
            column_ = count - last;  // 1-based column after the final newline
        }
        position_ += count;
}

}  // namespace db25