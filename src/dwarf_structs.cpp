// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/dwarf_structs.hpp"

// stdc++
#include <filesystem>

// application
#include "orc/parse_file.hpp"

/**************************************************************************************************/

const char* to_string(arch arch) {
    switch (arch) {
        case arch::unknown:
            return "unknown";
        case arch::x86:
            return "x86";
        case arch::x86_64:
            return "x86_64";
        case arch::arm:
            return "arm";
        case arch::arm64:
            return "arm64";
        case arch::arm64_32:
            return "arm64_32";
    }
}

/**************************************************************************************************/

void attribute::read(freader& s) {
    _name = static_cast<dw::at>(uleb128(s));
    _form = static_cast<dw::form>(uleb128(s));
}

/**************************************************************************************************/

std::size_t attribute_value::hash() const {
    // order here matches operator==
    if (has_string()) return hash_combine(0, string().hash());
    else if (has_uint()) return hash_combine(0, uint());
    else if (has_sint()) return hash_combine(0, sint());
    return hash_combine(0, type());
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const attribute_value& x) {
    if (x.type() == attribute_value::type::none) return s << "<none>";
    if (x.type() == attribute_value::type::passover) return s << "<unhandled>";

    auto first_space = [_first = true, &_s = s]() mutable {
        if (!_first) _s << "; ";
        _first = false;
    };

    if (x.has(attribute_value::type::string)) {
        first_space();
        s << x.string();
    }

    if (x.has(attribute_value::type::uint)) {
        first_space();
        s << x.uint() << " (0x" << std::hex << x.uint() << std::dec << ")";
    }

    if (x.has(attribute_value::type::sint)) {
        first_space();
        s << x.sint() << " (0x" << std::hex << x.sint() << std::dec << ")";
    }

    return s;
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const attribute& x) {
    return s << "        " << to_string(x._name) << ": " << x._value;
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const die& x) {
    std::string def_loc;
    std::vector<attribute> attributes(x._attributes, x._attributes + x._attributes_size);

    for (const auto& ancestor: x._ancestry) {
        s << "    within: " << ancestor.allocate_path().filename().string() << ":\n";
    }

    auto erase_attr = [](auto& attributes, auto key){
        auto found = std::find_if(attributes.begin(), attributes.end(), [&](auto& x){
            return x._name == key;
        });

        if (found != attributes.end())
            attributes.erase(found);
    };

    bool first = true;

    if (x.attribute_has_string(dw::at::decl_file)) {
        s << "        definition location: " << x.attribute_string(dw::at::decl_file);
        erase_attr(attributes, dw::at::decl_file);

        if (x.attribute_has_uint(dw::at::decl_line)) {
            s << ":" + std::to_string(x.attribute_uint(dw::at::decl_line));
            erase_attr(attributes, dw::at::decl_line);
        }

        first = false;
    }


    for (const auto& attr : attributes) {
        if (!first) s << '\n';
        s << attr;
        first = false;
    }

    return s;
}

/**************************************************************************************************/
