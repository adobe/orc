// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <string_view>

// application
#include "orc/parse_file.hpp"

/**************************************************************************************************/

std::string rstrip(std::string s);

std::vector<std::string> split(const std::string& src, const std::string& delimiter);

std::string join(std::vector<std::string> src, const std::string& delimiter);

/**************************************************************************************************/
