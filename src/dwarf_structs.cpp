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
#include "orc/object_file_registry.hpp"
#include "orc/parse_file.hpp"

//--------------------------------------------------------------------------------------------------

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

//--------------------------------------------------------------------------------------------------

void attribute::read(freader& s) {
    _name = static_cast<dw::at>(uleb128(s));
    _form = static_cast<dw::form>(uleb128(s));

    // SPECREF DWARF5 225 (207) lines 11-14 --
    // `implicit_const` is a special case where the value of the attribute we be an sleb immediately
    // after the form. There is no value in the `debug_info` in this case. When we process this
    // attribute in `process_form`, we'll source its value from here into the result.
    if (_form == dw::form::implicit_const) {
        _value.sint(sleb128(s));
    }
}

//--------------------------------------------------------------------------------------------------

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

//--------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& s, const attribute_value& x) {
    if (x.type() == attribute_value::type::none) return s << "<none>";
    if (x.type() == attribute_value::type::passover) return s << "<unhandled>";

    auto first_space = [_first = true, &_s = s]() mutable {
        if (!_first) _s << "; ";
        _first = false;
    };

    if (x.has(attribute_value::type::string)) {
        first_space();

        if (x.has(attribute_value::type::reference)) {
            s << '`';
        }

        s << x.string();

        if (x.has(attribute_value::type::reference)) {
            s << '`';
        }
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

//--------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& s, const attribute& x) {
    return s << "    " << to_string(x._name) << ": " << x._value;
}

//--------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& s, const location& x) {
    return s << "    " << x.file << ":" << x.loc;
}

std::optional<location> derive_definition_location(const attribute_sequence& x) {
    if (!x.has_string(dw::at::decl_file)) {
        return std::nullopt;
    }

    location result;

    result.file = x.string(dw::at::decl_file);

    if (x.has_uint(dw::at::decl_line)) {
        result.loc = x.uint(dw::at::decl_line);
    }

    return result;
}

//--------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& s, const attribute_sequence& x) {
    // file and line are covered by the `odrv_report`, so should be skipped here.
    for (const auto& attr : x) {
        if (attr._name == dw::at::decl_file) continue;
        if (attr._name == dw::at::decl_line) continue;
        s << attr << '\n';
    }

    return s;
}

//--------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& s, const object_ancestry& x) {
    bool first = true;
    for (const auto& ancestor : x) {
        if (first) {
            first = false;
        } else {
            s << " -> ";
        }

        s << ancestor.allocate_path().filename().string();
    }
    return s;
}

//--------------------------------------------------------------------------------------------------

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
            dw::at::call_value,
            dw::at::containing_type,
            // Item 10 of section 4.1 talks about the `const_value` attribute, saying the
            // entry describes a constant parameter value that can take a number of different
            // forms. Since ORC does not concern itself with parameter values, these should
            // be safe to skip. (Unless it's talking about _template_ parameters? But I don't
            // get that from the interpretation of the spec. I would expect the signature of
            // the template to contain the constant value, and it not be something required
            // of the target architecture, as is the case with `const_value`.)
            dw::at::const_value,
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
            // ODRV. Thus, dw::at::high_pc is a fatal attribute.
            // dw::at::high_pc,
            dw::at::location,
            dw::at::low_pc,
            dw::at::name,
            dw::at::prototyped,

            // Added 2025-03-28 with Xcode 16.1. It's been a while so I'm not sure exactly when
            // these were introduced, or if they are truly nonfatal. They have been known for
            // quite a while, but only now are starting to appear in generated DWARF data. I
            // think it's because Xcode 16.x has started producing DWARF v5 data. Huzzah.
            // (Perhaps it would be better to have an allowlist of fatal attributes instead of
            // a disallowlist of nonfatal ones?)
            dw::at::producer,
            dw::at::llvm_sysroot,
            dw::at::comp_dir,
            dw::at::ranges,
        };

        std::sort(nonfatal_attributes.begin(), nonfatal_attributes.end());

        return nonfatal_attributes;
    }();

    return sorted_has(attributes, at);
}

//--------------------------------------------------------------------------------------------------
