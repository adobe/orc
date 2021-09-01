// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/ar.hpp"

// application
#include "orc/str.hpp"

/**************************************************************************************************/

template <std::size_t N>
std::string read_fixed_string(freader& s) {
    char result[N];
    s.read(&result[0], N);
    return std::string(&result[0], &result[N]);
}

/**************************************************************************************************/

std::string read_string(freader& s, std::size_t n) {
    std::string result(n, 0);
    s.read(&result[0], n);
    return result;
}

/**************************************************************************************************/

void read_ar(const std::string&,
             freader& s,
             std::istream::pos_type end_pos,
             file_details details,
             callbacks callbacks) {
    std::string magic = read_fixed_string<8>(s);
    assert(magic == "!<arch>\n");

    // REVISIT: (fbrereto) opportunity to parallelize here.
    while (s.tellg() < end_pos) {
        std::string identifier = rstrip(read_fixed_string<16>(s));
        std::string timestamp = rstrip(read_fixed_string<12>(s));
        std::string owner_id = rstrip(read_fixed_string<6>(s));
        std::string group_id = rstrip(read_fixed_string<6>(s));
        std::string file_mode = rstrip(read_fixed_string<8>(s));
        std::size_t file_size = std::atoi(rstrip(read_fixed_string<10>(s)).c_str());
        std::string end_token = read_fixed_string<2>(s);

        // extended naming mode
        if (identifier.find("#1/") == 0) {
            auto extended_name_sz = std::atoi(&identifier[3]);
            identifier = rstrip(read_string(s, extended_name_sz));
            file_size -= extended_name_sz;
        }

        if (identifier.rfind(".o") == identifier.size() - 2) {
            auto end_pos = s.tellg() + static_cast<std::streamoff>(file_size);
            parse_file(identifier, s, end_pos, callbacks);
            s.seekg(end_pos); // parse_file could leave the read head anywhere.
        } else {
            // skip to next file in the archive.
            s.seekg(file_size, std::ios::cur);
        }
    }
}

/**************************************************************************************************/
