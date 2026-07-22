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
// SIMD-optimized SQL tokenizer - Foundation of the DB25 SQL Parser.
// This tokenizer has been thoroughly tested against complex SQL queries.
// 
// MODIFICATION RESTRICTION: This file is frozen for parser development.
// The parser must work with tokens as produced by this tokenizer.
// Any changes here would require revalidating all parser logic.
// ============================================================================

#include "simd_architecture.hpp"
#include "keywords.hpp"
#include <string_view>
#include <vector>

namespace db25 {

enum class TokenType : uint8_t {
    Unknown,
    Keyword,
    Identifier,
    Number,
    String,
    Operator,
    Delimiter,
    Whitespace,
    Comment,
    EndOfFile
};

struct Token {
    TokenType type;
    std::string_view value;
    Keyword keyword_id;  // If type == Keyword, this contains the keyword ID
    size_t line;
    size_t column;
};

class SimdTokenizer {
private:
    SimdDispatcher dispatcher_;
    const std::byte* input_;
    size_t input_size_;
    size_t position_;
    size_t line_;
    size_t column_;
    
public:
    SimdTokenizer(const std::byte* input, size_t size);
    [[nodiscard]] std::vector<Token> tokenize();
    [[nodiscard]] const char* simd_level() const noexcept;
    
private:
    Token next_token();
    Token scan_identifier_or_keyword(size_t start, size_t start_line, size_t start_column);
    Token scan_number(size_t start, size_t start_line, size_t start_column);
    Token scan_string(size_t start, size_t start_line, size_t start_column, uint8_t quote);
    Token scan_delimited_identifier(size_t start, size_t start_line, size_t start_column);
    Token scan_comment(size_t start, size_t start_line, size_t start_column);
    Token scan_block_comment(size_t start, size_t start_line, size_t start_column);
    Token scan_operator_or_delimiter(size_t start, size_t start_line, size_t start_column);
    void update_position(size_t count);
};

}  // namespace db25