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
    if (has_string())
        return string().hash();
    else if (has_uint())
        return uint();
    else if (has_sint())
        return sint();
    return static_cast<std::size_t>(type());
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

std::ostream& operator<<(std::ostream& s, const attribute_sequence& x) {
    if (x.has_string(dw::at::decl_file)) {
        s << "        definition location: " << x.string(dw::at::decl_file);

        if (x.has_uint(dw::at::decl_line)) {
            s << ":" + std::to_string(x.uint(dw::at::decl_line));
        }

        s << '\n';
    }

    for (const auto& attr : x) {
        if (attr._name == dw::at::decl_file) continue;
        if (attr._name == dw::at::decl_line) continue;
        s << attr << '\n';
    }

    return s;
}

/**************************************************************************************************/

std::ostream& operator<<(std::ostream& s, const die& x) {
    for (const auto& ancestor : x._ancestry) {
        s << "    within: " << ancestor.allocate_path().filename().string() << ":\n";
    }

    // Save for debugging so we can map what we find with dwarfdump output
#if 0
    s << "        debug info offset: 0x" << std::hex << x._debug_info_offset << std::dec << '\n';
#endif

    return s;
}

/**************************************************************************************************/

bool nonfatal_attribute(dw::at at) {
    static const auto attributes = [] {
        std::vector<dw::at> nonfatal_attributes = {
            dw::at::apple_block,
            dw::at::apple_flags,
            dw::at::apple_isa,
            dw::at::apple_major_runtime_vers,
            dw::at::apple_objc_complete_type,
            dw::at::apple_objc_direct,
            dw::at::apple_omit_frame_ptr,
            dw::at::apple_optimized,
            dw::at::apple_property,
            dw::at::apple_property_attribute,
            dw::at::apple_property_getter,
            dw::at::apple_property_name,
            dw::at::apple_property_setter,
            dw::at::apple_runtime_class,
            dw::at::apple_sdk,
            dw::at::call_column,
            dw::at::call_file,
            dw::at::call_line,
            dw::at::call_origin,
            dw::at::call_return_pc,
            dw::at::containing_type,
            dw::at::decl_column,
            dw::at::decl_file,
            dw::at::decl_line,
            dw::at::frame_base,
            // According to section 2.17 of the DWARF spec, if high_pc is a constant (e.g., form
            // data4) then its value is the size of the function. Likewise, its existence implies
            // the function it describes is a contiguous block of code in the object file. Since we
            // assume this attribute is of constant form, this is the size of the function. If two
            // or more functions with the same name have different high_pc values, their sizes are
            // different, which means their definitions are going to be different, and that's an
            // ODRV.
            // dw::at::high_pc,
            dw::at::location,
            dw::at::low_pc,
            dw::at::name,
            dw::at::prototyped,
        };

        std::sort(nonfatal_attributes.begin(), nonfatal_attributes.end());

        return nonfatal_attributes;
    }();

    return sorted_has(attributes, at);
}

/**************************************************************************************************/
