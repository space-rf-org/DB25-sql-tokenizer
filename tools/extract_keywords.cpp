/*
 * Copyright (c) 2024 Chiradip Mandal
 * Author: Chiradip Mandal
 * Organization: Space-RF.org
 *
 * This file is part of DB25 SQL Tokenizer.
 *
 * Licensed under the MIT License. See LICENSE file for details.
 */

// Extract SQL keywords from EBNF grammar and generate C++ header
// This ensures parser keywords always match the grammar specification

#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <string>
#include <cctype>
#include <map>


#ifdef __ARM_FEATURE_CRC32
#include <arm_acle.h>
#endif

struct KeywordInfo {
    std::string keyword;
    size_t length;
    uint32_t hash;
    bool is_reserved;

    bool operator<(const KeywordInfo& other) const {
        return keyword < other.keyword;
    }
};

class EBNFKeywordExtractor {
private:
    std::set<std::string> all_keywords;
    std::set<std::string> reserved_keywords;
    std::set<std::string> contextual_keywords;

    static constexpr uint32_t FNV1A_PRIME = 0x01000193;
    static constexpr uint32_t FNV1A_OFFSET = 0x811C9DC5;

    #ifdef __ARM_FEATURE_CRC32
        static uint32_t hash_keyword(const std::string& str) {
            uint32_t hash = 0;
            const char* data = str.c_str();
            size_t len = str.length();

            // Process 8 bytes at a time
            while (len >= 8) {
                uint32_t chunk;
                memcpy(&chunk, data, 4);
                hash = __crc32w(hash, chunk);
                data += 8;
                len -= 8;
            }

            // Handle remaining bytes
            while (len > 0) {
                // TEST and test should be the same
                hash = __crc32b(hash, std::toupper(*data++));
                len--;
            }

            return hash;
        }
    #else
        // FNV-1a hash for compile-time keyword hashing
        static uint32_t hash_keyword(const std::string& str) {
            uint32_t hash = FNV1A_OFFSET;
            for (char c : str) {
                hash ^= static_cast<uint8_t>(std::toupper(c));
                hash *= FNV1A_PRIME;
            }
            return hash;
        }
    #endif

public:
    bool extract_from_ebnf(const std::string& ebnf_file) {
        std::ifstream file(ebnf_file);
        if (!file) {
            std::cerr << "Cannot open EBNF file: " << ebnf_file << std::endl;
            return false;
        }

        std::string line;
        std::regex terminal_regex("\"([A-Z][A-Z_]*)\"");

        // Reserved keywords that cannot be used as identifiers
        reserved_keywords = {
            "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE",
            "CREATE", "DROP", "ALTER", "TABLE", "INDEX", "VIEW",
            "AND", "OR", "NOT", "NULL", "TRUE", "FALSE",
            "INNER", "LEFT", "RIGHT", "OUTER", "FULL", "CROSS", "JOIN",
            "GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC",
            "UNION", "INTERSECT", "EXCEPT", "ALL", "DISTINCT",
            "CASE", "WHEN", "THEN", "ELSE", "END",
            "PRIMARY", "FOREIGN", "UNIQUE", "KEY", "REFERENCES",
            "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION"
        };

        // Context-sensitive keywords (can sometimes be identifiers)
        contextual_keywords = {
            "AS", "ON", "IN", "IS", "TO", "SET", "WITH", "FOR",
            "BETWEEN", "LIKE", "EXISTS", "VALUES", "INTO", "DEFAULT",
            "CHECK", "CONSTRAINT", "CASCADE", "RESTRICT", "NO", "ACTION",
            "LIMIT", "OFFSET", "FETCH", "FIRST", "NEXT", "ONLY",
            "RECURSIVE", "SEARCH", "DEPTH", "BREADTH", "CYCLE",
            "PARTITION", "RANGE", "ROWS", "GROUPS", "WINDOW",
            "OVER", "FILTER", "WITHIN", "EXTRACT", "INTERVAL",
            "TEMPORARY", "TEMP", "IF", "REPLACE", "BEFORE", "AFTER",
            "INSTEAD", "OF", "TRIGGER", "EACH", "ROW", "STATEMENT",
            "AUTHORIZATION", "SCHEMA", "DATABASE", "SEQUENCE",
            "INCREMENT", "MINVALUE", "MAXVALUE", "START", "CACHE",
            "RETURNING", "CONFLICT", "DO", "NOTHING", "USING",
            "NATURAL", "CROSS", "ISOLATION", "LEVEL", "READ",
            "WRITE", "COMMITTED", "UNCOMMITTED", "REPEATABLE",
            "SERIALIZABLE", "DEFERRABLE", "LOCAL", "SESSION",
            "TIME", "ZONE", "PRAGMA", "VACUUM", "ANALYZE",
            "ATTACH", "DETACH", "REINDEX", "EXPLAIN", "QUERY", "PLAN",
            "VIRTUAL", "GENERATED", "ALWAYS", "STORED", "COLLATE",
            "CURRENT", "UNBOUNDED", "PRECEDING", "FOLLOWING",
            "EXCLUDE", "TIES", "OTHERS", "NULLS", "LAST",
            "SAVEPOINT", "RELEASE", "WORK", "CASCADED", "ESCAPE",
            "ILIKE", "UNKNOWN", "PIVOT", "UNPIVOT", "LATERAL"
        };

        while (std::getline(file, line)) {
            // Skip comments
            if (line.empty() || (line.size() > 1 && line[0] == '(' && line[1] == '*')) {
                continue;
            }

            // Extract terminals
            auto words_begin = std::sregex_iterator(line.begin(), line.end(), terminal_regex);
            auto words_end = std::sregex_iterator();

            for (auto it = words_begin; it != words_end; ++it) {
                std::string terminal = (*it)[1];

                // Check if it's a keyword (all uppercase with optional underscore)
                bool is_keyword = true;
                for (char c : terminal) {
                    if (!std::isupper(c) && c != '_') {
                        is_keyword = false;
                        break;
                    }
                }

                if (is_keyword && terminal.length() > 1 && terminal != "UNKNOWN") {
                    all_keywords.insert(terminal);
                }
            }
        }

        // Remove any duplicates (like UNKNOWN which might appear in EBNF)
        all_keywords.erase("UNKNOWN");

        // Merge all keyword sets
        all_keywords.insert(reserved_keywords.begin(), reserved_keywords.end());
        all_keywords.insert(contextual_keywords.begin(), contextual_keywords.end());

        std::cout << "Extracted " << all_keywords.size() << " keywords from EBNF" << std::endl;
        std::cout << "  Reserved: " << reserved_keywords.size() << std::endl;
        std::cout << "  Contextual: " << contextual_keywords.size() << std::endl;

        return true;
    }

    void generate_header(const std::string& output_file) {
        std::ofstream out(output_file);
        if (!out) {
            std::cerr << "Cannot create output file: " << output_file << std::endl;
            return;
        }

        // Prepare keyword data (excluding UNKNOWN which is predefined)
        std::vector<KeywordInfo> keywords;
        for (const auto& kw : all_keywords) {
            if (kw != "UNKNOWN") {  // Skip UNKNOWN - it's predefined as enum value 0
                keywords.push_back({
                    kw,
                    kw.length(),
                    hash_keyword(kw),
                    reserved_keywords.count(kw) > 0
                });
            }
        }

        // Sort by length first, then alphabetically for binary search optimization
        std::sort(keywords.begin(), keywords.end(), [](const auto& a, const auto& b) {
            if (a.length != b.length) return a.length < b.length;
            return a.keyword < b.keyword;
        });

        // Generate header file
        out << "#pragma once\n";
        out << "// Auto-generated from DB25_SQL_GRAMMAR.ebnf\n";
        out << "// DO NOT EDIT MANUALLY\n\n";
        out << "#include <string_view>\n";
        out << "#include <array>\n";
        out << "#include <cstdint>\n";
        out << "#include <algorithm>\n\n";
        out << "namespace db25 {\n\n";

        // Generate keyword enum
        out << "enum class Keyword : uint16_t {\n";
        out << "    UNKNOWN = 0,\n";
        for (size_t i = 0; i < keywords.size(); ++i) {
            // Handle C++ reserved words
            std::string enum_name = keywords[i].keyword;
            if (enum_name == "NULL") enum_name = "KW_NULL";
            if (enum_name == "TRUE") enum_name = "KW_TRUE";
            if (enum_name == "FALSE") enum_name = "KW_FALSE";
            if (enum_name == "DEFAULT") enum_name = "KW_DEFAULT";
            if (enum_name == "CASE") enum_name = "KW_CASE";

            out << "    " << enum_name << " = " << (i + 1);
            if (i < keywords.size() - 1) out << ",";
            out << "\n";
        }
        out << "};\n\n";

        // Generate keyword table
        out << "struct KeywordEntry {\n";
        out << "    std::string_view text;\n";
        out << "    uint8_t length;\n";
        out << "    uint32_t hash;\n";
        out << "    Keyword id;\n";
        out << "    bool is_reserved;\n";
        out << "};\n\n";

        out << "inline constexpr std::array<KeywordEntry, " << keywords.size() << "> KEYWORDS = {{\n";
        for (size_t i = 0; i < keywords.size(); ++i) {
            const auto& kw = keywords[i];

            // Handle C++ reserved words for enum reference
            std::string enum_name = kw.keyword;
            if (enum_name == "NULL") enum_name = "KW_NULL";
            if (enum_name == "TRUE") enum_name = "KW_TRUE";
            if (enum_name == "FALSE") enum_name = "KW_FALSE";
            if (enum_name == "DEFAULT") enum_name = "KW_DEFAULT";
            if (enum_name == "CASE") enum_name = "KW_CASE";

            out << "    {\"" << kw.keyword << "\", "
                << static_cast<int>(kw.length) << ", "
                << "0x" << std::hex << kw.hash << std::dec << ", "
                << "Keyword::" << enum_name << ", "
                << (kw.is_reserved ? "true" : "false") << "}";
            if (i < keywords.size() - 1) out << ",";
            out << "\n";
        }
        out << "}};\n\n";


        // Generate length buckets for optimization
        out << "// Length-based lookup tables for O(log n) search\n";
        std::map<size_t, std::vector<size_t>> length_buckets;
        for (size_t i = 0; i < keywords.size(); ++i) {
            length_buckets[keywords[i].length].push_back(i);
        }

        out << "struct LengthBucket {\n";
        out << "    size_t start;\n";
        out << "    size_t count;\n";
        out << "};\n\n";

        out << "inline constexpr std::array<LengthBucket, " << length_buckets.size() << "> LENGTH_BUCKETS = {{\n";
        size_t start = 0;
        for (const auto& [len, indices] : length_buckets) {
            out << "    {" << start << ", " << indices.size() << "},  // length " << len << "\n";
            start += indices.size();
        }
        out << "}};\n\n";

        #ifdef __ARM_FEATURE_CRC32
        // Calculate hash table size (next power of 2 after keywords size * 1.5)
        size_t table_size = 1;
        while (table_size < keywords.size() * 3 / 2) {
            table_size *= 2;
        }

        out << "// Hash table constants\n";
        out << "inline constexpr size_t TABLE_SIZE = " << table_size << ";\n";
        out << "inline constexpr size_t TABLE_MASK = TABLE_SIZE - 1;\n\n";

        // Generate hash table structure
        out << "struct HashEntry {\n";
        out << "    const char* text;\n";
        out << "    uint32_t hash;\n";
        out << "    Keyword id;\n";
        out << "};\n\n";

        // Create hash table with linear probing
        std::vector<const KeywordInfo*> hash_table(table_size, nullptr);

        for (const auto& keyword : keywords) {
            size_t bucket = keyword.hash & (table_size - 1);
            while (hash_table[bucket] != nullptr) {
                bucket = (bucket + 1) & (table_size - 1);
            }
            hash_table[bucket] = &keyword;
        }

        out << "// Hash table for keyword lookup\n";
        out << "inline constexpr std::array<HashEntry, TABLE_SIZE> HASH_TABLE = {{\n";
        for (size_t i = 0; i < table_size; ++i) {
            if (hash_table[i] != nullptr) {
                const auto& kw = *hash_table[i];
                out << "    {\"" << kw.keyword << "\", " << kw.hash << ", Keyword::" << kw.keyword << "},\n";
            } else {
                out << "    {nullptr, 0, Keyword::UNKNOWN},\n";
            }
        }
        out << "}};\n\n";

        // Generate hash table lookup function
        out << "[[nodiscard]] inline Keyword find_keyword(std::string_view text) noexcept {\n";
        out << "    if (text.empty() || text.length() > 32) return Keyword::UNKNOWN;\n";
        out << "    \n";
        out << "    // Convert to uppercase for hashing\n";
        out << "    char upper[32];\n";
        out << "    for (size_t i = 0; i < text.length(); ++i) {\n";
        out << "        upper[i] = (text[i] >= 'a' && text[i] <= 'z') ? \n";
        out << "                   (text[i] - 'a' + 'A') : text[i];\n";
        out << "    }\n";
        out << "    upper[text.length()] = '\\0';\n";
        out << "    \n";
        out << "    // Compute hash using CRC32\n";
        out << "    uint32_t hash = 0;\n";
        out << "    const char* data = upper;\n";
        out << "    size_t len = text.length();\n";
        out << "    \n";
        out << "    // Process 4 bytes at a time\n";
        out << "    while (len >= 4) {\n";
        out << "        uint32_t chunk;\n";
        out << "        memcpy(&chunk, data, 4);\n";
        out << "        hash = __crc32w(hash, chunk);\n";
        out << "        data += 4;\n";
        out << "        len -= 4;\n";
        out << "    }\n";
        out << "    \n";
        out << "    // Process remaining bytes\n";
        out << "    while (len > 0) {\n";
        out << "        hash = __crc32b(hash, *data);\n";
        out << "        data++;\n";
        out << "        len--;\n";
        out << "    }\n";
        out << "    \n";
        out << "    // Hash table lookup with linear probing\n";
        out << "    size_t bucket = hash & TABLE_MASK;\n";
        out << "    while (HASH_TABLE[bucket].text != nullptr) {\n";
        out << "        if (HASH_TABLE[bucket].hash == hash && \n";
        out << "            strcasecmp(upper, HASH_TABLE[bucket].text) == 0) {\n";
        out << "            return HASH_TABLE[bucket].id;\n";
        out << "        }\n";
        out << "        bucket = (bucket + 1) & TABLE_MASK;\n";
        out << "    }\n";
        out << "    \n";
        out << "    return Keyword::UNKNOWN;\n";
        out << "}\n\n";
        #else
        // Generate perfect hash table for common keywords
        out << "// Fast lookup function\n";
        out << "[[nodiscard]] inline Keyword find_keyword(std::string_view text) noexcept {\n";
        out << "    if (text.empty() || text.length() > 32) return Keyword::UNKNOWN;\n";
        out << "    \n";
        out << "    // Convert to uppercase for comparison\n";
        out << "    char upper[32];\n";
        out << "    for (size_t i = 0; i < text.length(); ++i) {\n";
        out << "        upper[i] = (text[i] >= 'a' && text[i] <= 'z') ? \n";
        out << "                   (text[i] - 'a' + 'A') : text[i];\n";
        out << "    }\n";
        out << "    std::string_view upper_text(upper, text.length());\n";
        out << "    \n";
        out << "    // Binary search in the keyword table\n";
        out << "    auto it = std::lower_bound(KEYWORDS.begin(), KEYWORDS.end(), upper_text,\n";
        out << "        [](const KeywordEntry& entry, std::string_view val) {\n";
        out << "            if (entry.length != val.length()) return entry.length < val.length();\n";
        out << "            return entry.text < val;\n";
        out << "        });\n";
        out << "    \n";
        out << "    if (it != KEYWORDS.end() && it->text == upper_text) {\n";
        out << "        return it->id;\n";
        out << "    }\n";
        out << "    \n";
        out << "    return Keyword::UNKNOWN;\n";
        out << "}\n\n";
        #endif

        // SIMD-optimized keyword matcher
        out << "// SIMD-optimized keyword matching\n";
        out << "template<typename Processor>\n";
        out << "[[nodiscard]] inline bool is_keyword_simd(Processor& proc,\n";
        out << "                                          const std::byte* data,\n";
        out << "                                          size_t size,\n";
        out << "                                          Keyword& out_keyword) noexcept {\n";
        out << "    if (size == 0 || size > 32) {\n";
        out << "        out_keyword = Keyword::UNKNOWN;\n";
        out << "        return false;\n";
        out << "    }\n";
        out << "    \n";
        out << "    // Find the length bucket\n";
        out << "    for (const auto& bucket : LENGTH_BUCKETS) {\n";
        out << "        if (KEYWORDS[bucket.start].length == size) {\n";
        out << "            // Check keywords in this bucket\n";
        out << "            for (size_t i = 0; i < bucket.count; ++i) {\n";
        out << "                const auto& entry = KEYWORDS[bucket.start + i];\n";
        out << "                if (proc.matches_keyword(data, size, entry.text.data(), entry.length)) {\n";
        out << "                    out_keyword = entry.id;\n";
        out << "                    return true;\n";
        out << "                }\n";
        out << "            }\n";
        out << "            break;\n";
        out << "        }\n";
        out << "        if (KEYWORDS[bucket.start].length > size) break;\n";
        out << "    }\n";
        out << "    \n";
        out << "    out_keyword = Keyword::UNKNOWN;\n";
        out << "    return false;\n";
        out << "}\n\n";

        out << "// Keyword name lookup\n";
        out << "[[nodiscard]] inline std::string_view keyword_name(Keyword kw) noexcept {\n";
        out << "    if (kw == Keyword::UNKNOWN) return \"UNKNOWN\";\n";
        out << "    size_t idx = static_cast<size_t>(kw) - 1;\n";
        out << "    if (idx < KEYWORDS.size()) {\n";
        out << "        return KEYWORDS[idx].text;\n";
        out << "    }\n";
        out << "    return \"INVALID\";\n";
        out << "}\n\n";

        out << "}  // namespace db25\n";

        std::cout << "Generated " << output_file << " with " << keywords.size() << " keywords" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <grammar.ebnf> <output.hpp>" << std::endl;
        return 1;
    }

    std::string ebnf_file = argv[1];
    std::string output_file = argv[2];

    EBNFKeywordExtractor extractor;

    if (!extractor.extract_from_ebnf(ebnf_file)) {
        return 1;
    }

    extractor.generate_header(output_file);

    return 0;
}
