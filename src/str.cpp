// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/str.hpp"

// stdc++
#include <cmath>
#include <sstream>

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

std::string format_size(std::size_t x, format_mode mode) {
    double v(x);
    std::size_t exponent{0};
    const std::size_t factor{mode == format_mode::binary ? 1024ul : 1000ul};

    while (v >= factor && exponent < 4) {
        v /= factor;
        ++exponent;
    }

    const char* label = [&]{
        switch (exponent) {
            case 0: return "bytes";
            case 1: return mode == format_mode::binary ? "KiB" : "KB";
            case 2: return mode == format_mode::binary ? "MiB" : "MB";
            case 3: return mode == format_mode::binary ? "GiB" : "GB";
            default: return mode == format_mode::binary ? "TiB" : "TB";
        }
    }();

    float dummy(0);
    const bool with_precision = std::modf(v, &dummy) != 0;
    std::stringstream result;
    result << std::fixed << std::setprecision(with_precision ? 2 : 0) << v << ' ' << label;
    return result.str();
}

/**************************************************************************************************/

std::string format_pct(float x) {
    x *= 100.;

    float dummy(0);
    const bool with_precision = std::modf(x, &dummy) != 0;
    std::stringstream result;
    result << std::fixed << std::setprecision(with_precision ? 2 : 0) << x << '%';
    return result.str();
}

/**************************************************************************************************/

std::string toupper(std::string&& s) {
    std::transform(s.begin(), s.end(), s.begin(), [](auto c){
        return std::toupper(c);
    });
    return s;
}

/**************************************************************************************************/
