// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/str.hpp"

/**************************************************************************************************/

std::string rstrip(std::string s) {
    auto found =
        std::find_if_not(s.rbegin(), s.rend(), [](char x) { return std::isspace(x) || x == 0; });
    s.erase(s.size() - std::distance(found.base(), s.end()));
    return s;
}

/**************************************************************************************************/

std::vector<std::string> split(const std::string& src, const std::string& delimiter) {
    std::size_t p = 0;
    std::vector<std::string> result;
    while (true) {
        auto next = src.find(delimiter, p);
        if (next == std::string::npos) {
            result.push_back(src.substr(p, std::string::npos));
            break;
        }
        result.push_back(src.substr(p, next - p));
        p = next + delimiter.size();
        if (p > src.size()) break;
    }
    return result;
}

/**************************************************************************************************/

std::string join(std::vector<std::string> src, const std::string& delimiter) {
    if (src.empty()) return std::string();
    std::string result = src[0];
    for (std::size_t i = 1; i < src.size(); ++i)
        result += delimiter + src[i];
    return result;
}

/**************************************************************************************************/
