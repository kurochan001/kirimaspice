// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

// Efficient string utility functions using string_view
// to reduce unnecessary string copies in parsing operations.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace kirimaspice::util {

/// Trim leading whitespace from a string_view (no allocation)
/// @param sv Input string view
/// @return Trimmed string view (references same memory)
inline std::string_view ltrim_view(std::string_view sv) noexcept {
    auto it = std::find_if(sv.begin(), sv.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    return sv.substr(static_cast<size_t>(it - sv.begin()));
}

/// Trim trailing whitespace from a string_view (no allocation)
/// @param sv Input string view
/// @return Trimmed string view (references same memory)
inline std::string_view rtrim_view(std::string_view sv) noexcept {
    auto it = std::find_if(sv.rbegin(), sv.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    return sv.substr(0, static_cast<size_t>(sv.rend() - it));
}

/// Trim both leading and trailing whitespace from a string_view (no allocation)
/// @param sv Input string view
/// @return Trimmed string view (references same memory)
inline std::string_view trim_view(std::string_view sv) noexcept {
    return ltrim_view(rtrim_view(sv));
}

/// Trim both leading and trailing whitespace, returns a new string
/// Use when you need to own the result
/// @param sv Input string view
/// @return Trimmed string (new allocation)
inline std::string trim_copy(std::string_view sv) {
    return std::string(trim_view(sv));
}

/// Trim a string in-place (modifies the string)
/// @param s String to trim
inline void trim_inplace(std::string& s) {
    // Trim trailing first to avoid moving characters unnecessarily
    auto r_it = std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    s.erase(r_it.base(), s.end());

    // Trim leading
    auto l_it = std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    s.erase(s.begin(), l_it);
}

/// Check if a string starts with a prefix (case-insensitive)
/// @param str String to check
/// @param prefix Prefix to look for
/// @return true if str starts with prefix (case-insensitive)
inline bool starts_with_icase(std::string_view str, std::string_view prefix) noexcept {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(str[i])) !=
            std::toupper(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

/// Convert string to uppercase in-place
/// @param s String to convert
inline void to_upper_inplace(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
}

/// Convert string to uppercase, returns a new string
/// @param sv Input string view
/// @return Uppercase string (new allocation)
inline std::string to_upper_copy(std::string_view sv) {
    std::string result(sv);
    to_upper_inplace(result);
    return result;
}

}  // namespace kirimaspice::util
