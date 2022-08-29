// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/dwarf.hpp"

// stdc++
#include <list>
#include <unordered_map>
#include <vector>

// application
#include "orc/dwarf_structs.hpp"
#include "orc/features.hpp"
#include "orc/settings.hpp"

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

std::uint32_t form_length(dw::form f, freader& s) {
    static constexpr std::uint32_t length_size_k{4}; // REVISIT: (fosterbrereton) 8 on 64bit DWARF

    auto leb_block = [&] {
        return temp_seek(s, [&] {
            auto start = s.tellg();
            auto length = uleb128(s);
            auto leb_length = s.tellg() - start;
            return length - leb_length;
        });
    };

    switch (f) {
        case dw::form::addr:
            return 8;
        case dw::form::data2:
            return 2;
        case dw::form::data4:
            return 4;
        case dw::form::data8:
            return 8;
        case dw::form::string:
            assert(false);
            return 0; // TODO: read NTSB from s
        case dw::form::block:
            return leb_block();
        case dw::form::block1:
            // for block1, block2, and block4, we read the N-byte prefix, and then send back
            // its value as the length of the form. This causes the passover to skip over the
            // value, putting the read head at the correct place. So the returned length isn't
            // technically correct, but the net result is what we want.
            return read_pod<std::uint8_t>(s);
        case dw::form::block2:
            return read_pod<std::uint16_t>(s);
        case dw::form::block4:
            return read_pod<std::uint32_t>(s);
        case dw::form::data1:
            return 1;
        case dw::form::flag:
            return 1;
        case dw::form::sdata:
            return sleb128(s); // length of LEB _not_ included
        case dw::form::strp:
            return length_size_k;
        case dw::form::udata:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_addr:
            return length_size_k;
        case dw::form::ref1:
            return 1;
        case dw::form::ref2:
            return 2;
        case dw::form::ref4:
            return 4;
        case dw::form::ref8:
            return 8;
        case dw::form::ref_udata:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::indirect:
            // For attributes with this form, the attribute value itself in the .debug_info
            // section begins with an unsigned LEB128 number that represents its form.
            assert(false);
            return 0; // TODO: (fbrereto)
        case dw::form::sec_offset:
            return length_size_k;
        case dw::form::exprloc:
            return leb_block();
        case dw::form::flag_present:
            return 0;
        case dw::form::strx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::addrx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_sup4:
            return 4;
        case dw::form::strp_sup:
            return length_size_k;
        case dw::form::data16:
            return 16;
        case dw::form::line_strp:
            return length_size_k;
        case dw::form::ref_sig8:
            return 8;
        case dw::form::implicit_const:
            return 0;
        case dw::form::loclistx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::rnglistx:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::ref_sup8:
            return 8;
        case dw::form::strx1:
            return 1;
        case dw::form::strx2:
            return 2;
        case dw::form::strx3:
            return 4;
        case dw::form::strx4:
            return 4;
        case dw::form::addrx1:
            return 1;
        case dw::form::addrx2:
            return 2;
        case dw::form::addrx3:
            return 4;
        case dw::form::addrx4:
            return 4;
        case dw::form::gnu_addr_index:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::gnu_str_index:
            return uleb128(s); // length of LEB _not_ included
        case dw::form::gnu_ref_alt:
            return length_size_k;
        case dw::form::gnu_strp_alt:
            return length_size_k;
    }
}

/**************************************************************************************************/

struct section {
    std::size_t _offset{0};
    std::size_t _size{0};

    bool valid() const { return _offset != 0 && _size != 0; }
};

/**************************************************************************************************/
// An abbreviation (abbreviated 'abbrev' througout a lot of the DWARF spec) is a template of sorts.
// Think of it like a cookie cutter that needs to get stamped on some dough to make an actual
// cookie. Only in this case instead of a cookie, it'll make a DIE (DWARF Information Entry.)
struct abbrev {
    std::size_t _g{0};
    std::uint32_t _code{0};
    dw::tag _tag{0};
    bool _has_children{false};
    std::vector<attribute> _attributes;

    void read(freader& s);
};

void abbrev::read(freader& s) {
    _g = s.tellg();
    _code = uleb128(s);
    _tag = static_cast<dw::tag>(uleb128(s));
    _has_children = read_pod<bool>(s);
    while (true) {
        attribute entry;
        entry.read(s);
        if (entry._name == dw::at::none) break;
        _attributes.push_back(std::move(entry));
    }
}

/**************************************************************************************************/

struct file_name {
    std::string_view _name;
    std::uint32_t _directory_index{0};
    std::uint32_t _mod_time{0};
    std::uint32_t _file_length{0};
};

/**************************************************************************************************/


/**************************************************************************************************/

struct cu_header {
    std::uint64_t _length{0}; // 4 bytes (or 12 if extended length is used.)
    bool _is_64_bit{false};
    std::uint16_t _version{0};
    std::uint64_t _debug_abbrev_offset{0}; // 4 (!_is_64_bit) or 8 (_is_64_bit) bytes
    std::uint32_t _address_size{0};

    void read(freader& s, const file_details& details);
};

void cu_header::read(freader& s, const file_details& details) {
    _length = read_pod<std::uint32_t>(s);
    if (details._needs_byteswap) {
        endian_swap(_length);
    }
    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        throw std::runtime_error("unsupported length");
    }

    _version = read_pod<std::uint16_t>(s);

    // note the read_pod types differ.
    if (_is_64_bit) {
        _debug_abbrev_offset = read_pod<std::uint64_t>(s);
    } else {
        _debug_abbrev_offset = read_pod<std::uint32_t>(s);
    }

    if (details._needs_byteswap) {
        endian_swap(_version);
        endian_swap(_debug_abbrev_offset);
    }

    _address_size = read_pod<std::uint8_t>(s);
}

/**************************************************************************************************/

struct line_header {
    std::uint64_t _length{0}; // 4 (DWARF) or 8 (DWARF64) bytes
    std::uint16_t _version{0};
    std::uint32_t _header_length{0}; // 4 (DWARF) or 8 (DWARF64) bytes
    std::uint32_t _min_instruction_length{0};
    std::uint32_t _max_ops_per_instruction{0}; // DWARF4 or greater
    std::uint32_t _default_is_statement{0};
    std::int32_t _line_base{0};
    std::uint32_t _line_range{0};
    std::uint32_t _opcode_base{0};
    std::vector<std::uint32_t> _standard_opcode_lengths;
    std::vector<std::string_view> _include_directories;
    std::vector<file_name> _file_names;

    void read(freader& s, const file_details& details);
};

void line_header::read(freader& s, const file_details& details) {
    _length = read_pod<std::uint32_t>(s);
    if (details._needs_byteswap) {
        endian_swap(_length);
    }
    if (_length >= 0xfffffff0) {
        // REVISIT: (fbrereto) handle extended length / DWARF64
        throw std::runtime_error("unsupported length");
    }
    _version = read_pod<std::uint16_t>(s);
    _header_length = read_pod<std::uint32_t>(s);
    _min_instruction_length = read_pod<std::uint8_t>(s);
    if (_version >= 4) {
        _max_ops_per_instruction = read_pod<std::uint8_t>(s);
    }
    _default_is_statement = read_pod<std::uint8_t>(s);
    _line_base = read_pod<std::int8_t>(s);
    _line_range = read_pod<std::uint8_t>(s);
    _opcode_base = read_pod<std::uint8_t>(s);
    if (details._needs_byteswap) {
        endian_swap(_version);
        endian_swap(_header_length);
        // endian_swap(_min_instruction_length);
        // endian_swap(_max_ops_per_instruction);
        // endian_swap(_default_is_statement);
        // endian_swap(_line_base);
        // endian_swap(_line_range);
        // endian_swap(_opcode_base);
    }
    for (std::size_t i{0}; i < (_opcode_base - 1); ++i) {
        _standard_opcode_lengths.push_back(read_pod<std::int8_t>(s));
    }

    while (true) {
        auto cur_directory = s.read_c_string_view();
        if (cur_directory.empty()) break;
        _include_directories.push_back(cur_directory);
    }

    while (true) {
        file_name cur_file_name;
        cur_file_name._name = s.read_c_string_view();
        if (cur_file_name._name.empty()) break;
        cur_file_name._directory_index = uleb128(s);
        cur_file_name._mod_time = uleb128(s);
        cur_file_name._file_length = uleb128(s);
        _file_names.push_back(std::move(cur_file_name));
    }
}

/**************************************************************************************************/

attribute* alloc_attributes(std::size_t n) {
#if ORC_FEATURE(LEAKY_MEMORY)
    thread_local auto& pool_s = *(new std::list<std::unique_ptr<attribute[]>>);
#else
    thread_local std::list<std::unique_ptr<attribute[]>> pool_s;
#endif // ORC_FEATURE(LEAKY_MEMORY)
    constexpr auto block_size_k{32 * 1024 * 1024};
    constexpr auto max_attributes_k{block_size_k / sizeof(attribute)};
    thread_local std::size_t n_s{max_attributes_k};
    if ((n_s + n) >= max_attributes_k) {
        pool_s.push_back(std::make_unique<attribute[]>(max_attributes_k));
        n_s = 0;
    }
    attribute* result = &pool_s.back().get()[n_s];
    n_s += n;
    return result;
}

/**************************************************************************************************/

const die& lookup_die(const dies& dies, std::uint32_t offset) {
    auto found = std::lower_bound(dies.begin(), dies.end(), offset,
                                  [](const auto& x, const auto& offset){
        return x._debug_info_offset < offset;
    });
    bool match = found != dies.end() && found->_debug_info_offset == offset;
    assert(match);
    if (!match) throw std::runtime_error("die not found");
    return *found;
}

/**************************************************************************************************/

void resolve_reference_attributes(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    for (auto& attr : d) {
        if (attr._name == dw::at::type) continue;
        if (!attr.has(attribute_value::type::reference)) continue;
        const die& resolved = lookup_die(dies, attr.reference());
        attr._value.die(resolved);
        attr._value.string(resolved._path);
    }
}

/**************************************************************************************************/

const die& find_base_reference(const dies& dies, const die& d, dw::at attribute) {
    if (!d.has_attribute(attribute)) return d;
    return find_base_reference(dies, lookup_die(dies, d.attribute_reference(attribute)), attribute);
}

/**************************************************************************************************/

void resolve_type_attribute(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    constexpr auto type_k = dw::at::type;

    if (!d.has_attribute(type_k)) return; // nothing to resolve
    if (d._type_resolved) return; // already resolved

    const die& base_type_die = find_base_reference(dies, d, type_k);

    // Now that we have the resolved type die, overwrite this die's type to reflect
    // the resolved type.
    attribute& type_attr = d.attribute(type_k);
    type_attr._value.die(base_type_die);
    if (base_type_die.has_attribute(dw::at::name)) {
        type_attr._value.string(base_type_die.attribute_string(dw::at::name));
    }

    d._type_resolved = true;
}

/**************************************************************************************************/
#if 0
bool resolve_specification_attribute(const dies& dies, die& d, const empool_callback& empool) { // REVISIT (fbrereto): d is an out-arg
    // Section 2.13.2 (Declarations Completing Non-Defining Declarations) of the DWARF spec
    // indicate that DIEs with a specification attribute do not need to duplicate the
    // information present in the specification die. However, it _also_ says that not all
    // attributes in the specification die will apply to _this_ die. So we have to pick
    // and choose them. Nice. Here we make the expensive choice to copy over the attributes
    // we care about from the specification die to this one. REVISIT (fosterbrereton) this
    // is also sub-optimal, as it throws away the previous attribute memory block that gets
    // allocated to make room for these other attributes.
    if (!d.attribute_has_die(dw::at::specification)) return false;

    const auto& spec_die = d.attribute_die(dw::at::specification);
    const dw::at attributes_to_copy_over[] = {
        dw::at::accessibility,
        dw::at::linkage_name,
    };

    // replace with a std::accumulate when my head stops spinning from the problem at hand.
    std::size_t additional{0};
    for (const auto& attribute : attributes_to_copy_over) {
        additional += spec_die.has_attribute(attribute);
    }

    if (additional == 0) return false;

    std::size_t old_attr_count = d._attributes_size;
    attribute* old_attr = d._attributes;

    d._attributes_size += additional;
    d._attributes = alloc_attributes(d._attributes_size);

    attribute* next_attr = std::copy(old_attr, old_attr + old_attr_count, d._attributes);

    for (const auto& attribute : attributes_to_copy_over) {
        if (!spec_die.has_attribute(attribute)) continue;
        *next_attr++ = spec_die.attribute(attribute);
    }

    return true;
}
#endif
/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

struct dwarf::implementation {
    implementation(object_ancestry&& ancestry,
                   freader& s,
                   const file_details& details,
                   callbacks callbacks)
        : _ancestry(std::move(ancestry)), _s(s), _details(details),
          _callbacks(std::move(callbacks)) {}

    void register_section(const std::string& name, std::size_t offset, std::size_t size);

    void process();

    template <class T>
    T read();

    std::uint64_t read64();
    std::uint32_t read32();
    std::uint32_t read16();
    std::uint32_t read8();
    std::uint32_t read_uleb();
    std::int32_t read_sleb();

    void read_abbreviations();
    void read_lines();
    const abbrev& find_abbreviation(std::uint32_t code) const;

    pool_string read_debug_str(std::size_t offset);

    void path_identifier_push();
    void path_identifier_set(pool_string name);
    void path_identifier_pop();
    std::string qualified_symbol_name(const die& d) const;

    attribute process_attribute(const attribute& attr, std::size_t cur_die_offset);
    attribute_value process_form(const attribute& attr, std::size_t cur_die_offset);
    attribute_value evaluate_exprloc(std::uint32_t expression_size);

    pool_string die_identifier(const die& a) const;

    die abbreviation_to_die(std::size_t die_address, std::uint32_t abbrev_code);

    object_ancestry _ancestry;
    freader& _s;
    const file_details _details;
    callbacks _callbacks;
    std::vector<abbrev> _abbreviations;
    std::vector<pool_string> _path;
    std::size_t _cu_address{0};
    std::vector<pool_string> _decl_files;
    section _debug_abbrev;
    section _debug_info;
    section _debug_line;
    section _debug_str;
};

/**************************************************************************************************/

template <class T>
T dwarf::implementation::read() {
    T result = read_pod<T>(_s);
    if (_details._needs_byteswap) endian_swap(result);
    return result;
}

std::uint64_t dwarf::implementation::read64() { return read<std::uint64_t>(); }

std::uint32_t dwarf::implementation::read32() { return read<std::uint32_t>(); }

std::uint32_t dwarf::implementation::read16() { return read<std::uint16_t>(); }

std::uint32_t dwarf::implementation::read8() { return read<std::uint8_t>(); }

std::uint32_t dwarf::implementation::read_uleb() { return uleb128(_s); }

std::int32_t dwarf::implementation::read_sleb() { return sleb128(_s); }

/**************************************************************************************************/

void dwarf::implementation::register_section(const std::string& name,
                                             std::size_t offset,
                                             std::size_t size) {
    if (name == "__debug_str") {
        _debug_str = section{ offset, size };
    } else if (name == "__debug_info") {
        _debug_info = section{ offset, size };
    } else if (name == "__debug_abbrev") {
        _debug_abbrev = section{ offset, size };
    } else if (name == "__debug_line") {
        _debug_line = section{ offset, size };
    }
}

/**************************************************************************************************/

void dwarf::implementation::read_abbreviations() {
    auto section_begin = _debug_abbrev._offset;
    auto section_end = section_begin + _debug_abbrev._size;
    temp_seek(_s, section_begin, [&] {
        while (_s.tellg() < section_end) {
            abbrev a;
            a.read(_s);
            if (a._code == 0 || a._tag == dw::tag::none) break;
            _abbreviations.push_back(std::move(a));
        }
    });
}

/**************************************************************************************************/

void dwarf::implementation::read_lines() {
    temp_seek(_s, _debug_line._offset, [&] {
        line_header header;
        header.read(_s, _details);

        for (const auto& name : header._file_names) {
            if (name._directory_index > 0) {
                assert(name._directory_index - 1 < header._include_directories.size());
                std::string path(header._include_directories[name._directory_index - 1]);
                path += '/';
                path += name._name;
                _decl_files.push_back(_callbacks._empool(path));
            } else {
                _decl_files.push_back(_callbacks._empool(name._name));
            }
        }

        // We don't need to process the rest of __debug__line. We're only here for the file table.
    });
}

/**************************************************************************************************/

const abbrev& dwarf::implementation::find_abbreviation(std::uint32_t code) const {
    auto found = std::lower_bound(_abbreviations.begin(), _abbreviations.end(), code,
                                  [](const auto& x, const auto& code) { return x._code < code; });
    if (found == _abbreviations.end() || found->_code != code) {
        throw std::runtime_error("abbrev not found: " + std::to_string(code));
    }
    return *found;
}

/**************************************************************************************************/

pool_string dwarf::implementation::read_debug_str(std::size_t offset) {
    // We should be pre-loading all the strings into the string pool, saving their offsets off
    // into a map<offset, pool_string>, and looking the offsets up as needed. Re-reading these over
    // and over is a waste of time.
    return temp_seek(_s, _debug_str._offset + offset, [&] {
        return _callbacks._empool(_s.read_c_string_view());
    });
}

/**************************************************************************************************/

void dwarf::implementation::path_identifier_push() { _path.push_back(pool_string()); }

/**************************************************************************************************/

void dwarf::implementation::path_identifier_set(pool_string name) {
    assert(!_path.empty());
    _path.back() = name;
}

/**************************************************************************************************/

void dwarf::implementation::path_identifier_pop() { _path.pop_back(); }

/**************************************************************************************************/

std::string dwarf::implementation::qualified_symbol_name(const die& d) const {
    // There are some attributes that contain the mangled name of the symbol.
    // This is a much better representation of the symbol than the derived path
    // we are using, so let's use that instead here.
    // (We may want to store the path off in the die elsewhere anyhow, as it
    // could aid in ODRV analysis by the user.)

    const dw::at qualified_attributes[] = {
        dw::at::linkage_name,
        dw::at::specification,
    };

    for (const auto& at : qualified_attributes) {
        if (d.attribute_has_string(at)) {
            return "::[u]::" + d.attribute_string(at).allocate_string();
        }
    }

    // preprocess the path. If any identifier in the path is the empty string,
    // then it's talking about an anonymous/unnamed symbol, which at this time
    // we do not register. In such a case, if we find one, return an empty string
    // for the whole path so we can skip over this die at registration time.
    for (const auto& identifier : _path) {
        if (identifier.empty()) {
            return std::string();
        }
    }

    std::string result;
    for (const auto& identifier : _path) {
        result += "::" + identifier.allocate_string();
    }
    return result;
}

/**************************************************************************************************/
// This "flattens" the template into an evaluated value, based on both the attribute and the
// current read position in debug_info.
attribute dwarf::implementation::process_attribute(const attribute& attr,
                                                   std::size_t cur_die_offset) {
    attribute result = attr;

    result._value = process_form(attr, cur_die_offset);

    // some attributes need further processing. Once we have the initial value from process_form,
    // we can continue the work here. (Some forms, like ref_addr, have already been processed
    // completely- these are the cases when a value needs contextual interpretation. For example,
    // decl_file comes back as a uint, but that's a debug_str offset that needs to be resolved.
    if (result._name == dw::at::decl_file) {
        auto decl_file_index = result._value.uint();
        assert(decl_file_index < _decl_files.size());
        result._value.string(_decl_files[decl_file_index]);
    } else if (result._name == dw::at::calling_convention) {
        auto convention = result._value.uint();
        assert(convention > 0 && convention <= 0xff);
        switch (convention) {
            case 0x01: result._value.string(_callbacks._empool("normal")); break;
            case 0x02: result._value.string(_callbacks._empool("program")); break;
            case 0x03: result._value.string(_callbacks._empool("nocall")); break;
            case 0x04: result._value.string(_callbacks._empool("pass by reference")); break;
            case 0x05: result._value.string(_callbacks._empool("pass by value")); break;
            case 0x40: result._value.string(_callbacks._empool("lo user")); break;
            case 0xff: result._value.string(_callbacks._empool("hi user")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::virtuality) {
        auto virtuality = result._value.uint();
        assert(virtuality >= 0 && virtuality <= 2);
        switch (virtuality) {
            case 0: result._value.string(_callbacks._empool("none")); break;
            case 1: result._value.string(_callbacks._empool("virtual")); break;
            case 2: result._value.string(_callbacks._empool("pure virtual")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::visibility) {
        auto visibility = result._value.uint();
        assert(visibility > 0 && visibility <= 3);
        switch (visibility) {
            case 1: result._value.string(_callbacks._empool("local")); break;
            case 2: result._value.string(_callbacks._empool("exported")); break;
            case 3: result._value.string(_callbacks._empool("qualified")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._name == dw::at::apple_property) {
        auto property = result._value.uint();
        // this looks like a bitfield; a switch may not suffice.
        switch (property) {
            case 0x01: result._value.string(_callbacks._empool("readonly")); break;
            case 0x02: result._value.string(_callbacks._empool("getter")); break;
            case 0x04: result._value.string(_callbacks._empool("assign")); break;
            case 0x08: result._value.string(_callbacks._empool("readwrite")); break;
            case 0x10: result._value.string(_callbacks._empool("retain")); break;
            case 0x20: result._value.string(_callbacks._empool("copy")); break;
            case 0x40: result._value.string(_callbacks._empool("nonatomic")); break;
            case 0x80: result._value.string(_callbacks._empool("setter")); break;
            case 0x100: result._value.string(_callbacks._empool("atomic")); break;
            case 0x200: result._value.string(_callbacks._empool("weak")); break;
            case 0x400: result._value.string(_callbacks._empool("strong")); break;
            case 0x800: result._value.string(_callbacks._empool("unsafe_unretained")); break;
            case 0x1000: result._value.string(_callbacks._empool("nullability")); break;
            case 0x2000: result._value.string(_callbacks._empool("null_resettable")); break;
            case 0x4000: result._value.string(_callbacks._empool("class")); break;
            // otherwise, leave the value unchanged.
        }
    } else if (result._form == dw::form::flag || result._form == dw::form::flag_present) {
        static const auto true_ = _callbacks._empool("true");
        static const auto false_ = _callbacks._empool("false");
        result._value.string(result._value.uint() ? true_ : false_);
    }

    return result;
}

/**************************************************************************************************/

pool_string dwarf::implementation::die_identifier(const die& d) const {
    // To get a good name for this die, first we look at the tag types
    // to see if there's something worth calling it. These tags may not
    // be the best high-level names to give to some of these types, but
    // I don't have all the attributes fully fleshed out yet. For example,
    // inlined_subroutine or subprogram might have an attribute with a
    // name that would be better than the tag name itself. When the
    // attributes are all fleshed out, we should revisit this one.
    switch (d._tag) {
        case dw::tag::compile_unit:
        case dw::tag::partial_unit:
            return _callbacks._empool("[u]"); // u for "unit"
        default:
            // do nothing; we need the name not from the tag, but from one of its attributes
            break;
    }

    if (d._attributes_size == 0) {
        return pool_string();
    }

    // If the tag type doesn't give us a good name, then we scan various
    // attributes.
    const dw::at string_attributes[] = {
        dw::at::linkage_name,    dw::at::name,          dw::at::type, dw::at::import,
        dw::at::abstract_origin, dw::at::specification,
    };
    for (const auto& at : string_attributes)
        if (d.attribute_has_string(at)) return d.attribute_string(at);

    // If it's not named, it's an anonymous/unnamed die. So far we're skipping over these (and
    // all their descendants, if they have any) for registration purposes.
    return pool_string();
}

/**************************************************************************************************/

attribute_value dwarf::implementation::evaluate_exprloc(std::uint32_t expression_size) {
    std::vector<std::int32_t> stack;
    const auto end = _s.tellg() + expression_size;

    // There are some exprlocs that cannot be deciphered, probably because we don't have as much
    // information in our state machine as a debugger does when they're doing these evaluations.
    // In the event that happens, we set this passover flag, and avoid the evaluation entirely.
    bool passover = false;

    // The use of the range-based switch statements below are an extension to the C++ standard,
    // but is supported by both GCC and clang. It makes the below *far* more readable, but if
    // we need to pull it out, we can.

    while (_s.tellg() < end && !passover) {
        auto op = read_pod<dw::op>(_s);
        switch (op) {
            case dw::op::lit0 ... dw::op::lit31: {
                stack.push_back(static_cast<int>(op) - static_cast<int>(dw::op::reg0));
            } break;
            case dw::op::reg0 ... dw::op::reg31: {
                stack.push_back(static_cast<int>(op) - static_cast<int>(dw::op::reg0));
            } break;
            case dw::op::const1u: {
                stack.push_back(read8());
            } break;
            case dw::op::const2u: {
                stack.push_back(read16());
            } break;
            case dw::op::const4u: {
                stack.push_back(read32());
            } break;
            case dw::op::const8u: {
                stack.push_back(read64());
            } break;
            case dw::op::const1s: {
                stack.push_back(read_pod<std::int8_t>(_s));
            } break;
            case dw::op::const2s: {
                stack.push_back(read_pod<std::int16_t>(_s));
            } break;
            case dw::op::const4s: {
                stack.push_back(read_pod<std::int32_t>(_s));
            } break;
            case dw::op::const8s: {
                stack.push_back(read_pod<std::int64_t>(_s));
            } break;
            case dw::op::constu: {
                stack.push_back(read_uleb());
            } break;
            case dw::op::consts: {
                stack.push_back(read_sleb());
            } break;
            case dw::op::regx: {
                stack.push_back(read_uleb());
            } break;
            case dw::op::dup: {
                if (stack.empty()) {
                    passover = true;
                } else {
                    stack.push_back(stack.back());
                }
            } break;
            default: {
                passover = true;
            } break;
        }
    }

    attribute_value result;

    if (passover) {
        _s.seekg(end);
        result.passover();
    } else {
        assert(!stack.empty());
        result.sint(stack.back());
    }

    return result;
}

/**************************************************************************************************/

attribute_value dwarf::implementation::process_form(const attribute& attr,
                                                    std::size_t cur_die_offset) {
    auto form = attr._form;
    const auto debug_info_offset = _debug_info._offset;
    const auto cu_offset = _cu_address - debug_info_offset;
    attribute_value result;

    /*
        Notes worth remembering:

        The values for ref1, ref2, ref4, and ref8 are offsets from the first byte of the current
        compilation unit header, not the top of __debug_info.

        ref_addr could be 4 (DWARF) or 8 (DWARF64) bytes. We assume the former at present. We
        should save the cu_header somewhere so we can do the right thing here.
     */

    switch (form) {
        case dw::form::udata:
        case dw::form::implicit_const: {
            result.uint(read_uleb());
        } break;
        case dw::form::sdata: {
            result.uint(read_uleb()); // sdata is expecting unsigned values?
        } break;
        case dw::form::strp: {
            result.string(read_debug_str(read32()));
        } break;
        case dw::form::exprloc: {
            read_exactly(_s, read_uleb(), [&](auto expr_size){
                result = evaluate_exprloc(expr_size);
            });
        } break;
        case dw::form::addr: {
            result.uint(read64());
        } break;
        case dw::form::ref_addr: {
            result.reference(read32());
        } break;
        case dw::form::ref1: {
            result.reference(cu_offset + read8());
        } break;
        case dw::form::ref2: {
            result.reference(cu_offset + read16());
        } break;
        case dw::form::ref4: {
            result.reference(cu_offset + read32());
        } break;
        case dw::form::ref8: {
            result.reference(cu_offset + read64());
        } break;
        case dw::form::data1: {
            result.uint(read8());
        } break;
        case dw::form::data2: {
            result.uint(read16());
        } break;
        case dw::form::data4: {
            result.uint(read32());
        } break;
        case dw::form::data8: {
            result.uint(read64());
        } break;
        case dw::form::string: {
            result.string(_callbacks._empool(_s.read_c_string_view()));
        } break;
        case dw::form::flag: {
            result.uint(read8());
        } break;
        case dw::form::flag_present: {
            result.uint(1);
        } break;
        case dw::form::sec_offset: {
            result.uint(read32());
        } break;
        default: {
            result.passover();
            auto size = form_length(form, _s);
            _s.seekg(size, std::ios::cur);
        } break;
    }

    return result;
}

/**************************************************************************************************/

die dwarf::implementation::abbreviation_to_die(std::size_t die_address, std::uint32_t abbrev_code) {
    die result;

    result._debug_info_offset = die_address - _debug_info._offset;
    result._arch = _details._arch;

#if 0 // save for debugging. Useful to grok why a specific die isn't getting processed correctly.
    if (result._debug_info_offset == 0x0000e993) {
        int x;
        (void)x;
    }
#endif

    if (abbrev_code == 0) return result;

    auto& a = find_abbreviation(abbrev_code);

    result._tag = a._tag;
    result._has_children = a._has_children;
    assert(a._attributes.size() < 256); // we've never seen anything even close to 255, but check to be sure
    result._attributes_size = a._attributes.size();
    result._attributes = alloc_attributes(result._attributes_size);

    std::transform(a._attributes.begin(), a._attributes.end(),
                   result.begin(),
                   [&](const auto& x) { return process_attribute(x, result._debug_info_offset); });

    path_identifier_set(die_identifier(result));

    result._path = empool(std::string_view(qualified_symbol_name(result)));

    return result;
}

/**************************************************************************************************/

void dwarf::implementation::process() {
    if (!(_debug_info.valid() && _debug_abbrev.valid() && _debug_line.valid())) return;

#if 0 // save for debugging, waiting to catch a particular file.
    if (_ancestry.back().allocate_string() == "ImathBox.cpp.o") {
        int x;
        (void)x;
    }
#endif

    // Once we've loaded all the necessary DWARF sections, now we start piecing the details
    // together.

    read_abbreviations();

    read_lines();

    auto section_begin = _debug_info._offset;
    auto section_end = section_begin + _debug_info._size;

    _s.seekg(section_begin);

    // Have a nonempty stack in the path
    path_identifier_push();

    dies dies;

    while (_s.tellg() < section_end) {
        cu_header header;

        _cu_address = _s.tellg();

        header.read(_s, _details);

        // process dies one at a time, recording things like addresses along the way.
        while (true) {
            // we may not need to save these as a vector - registering them below should
            // suffice.

            auto die = abbreviation_to_die(_s.tellg(), read_uleb());

            // code 0 is reserved; it's a null entry, and signifies the end of siblings.
            if (die._tag == dw::tag::none) {
                path_identifier_pop();

                if (_path.size() == 1) {
                    break; // end of the compilation unit
                }

                continue;
            } else if (die._tag == dw::tag::compile_unit || die._tag == dw::tag::partial_unit) {
                _decl_files.insert(_decl_files.begin(), die.attribute_string(dw::at::name));

                // We've seen cases in the wild where compilation units are empty, have no children,
                // but do not have a null abbreviation code signalling their "end". In this case,
                // then, if we find a unit with no children, we're done. (No need to pop from the
                // path, either, because we never pushed anything.)

                // REVISIT: (fosterbrereton) This code path does not register the unit's die.
                if (!die._has_children) {
                    break; // end of the compilation unit
                }
            }

            if (die._has_children) {
                path_identifier_push();
            }

            die._ancestry = _ancestry;
            dies.push_back(std::move(die));
        }
    }

    // Now that we have all the dies for this translation unit, do some post-processing to get more
    // details out and reported to the surface.

    dies.shrink_to_fit();

    for (auto& d : dies) {
        resolve_reference_attributes(dies, d);

        resolve_type_attribute(dies, d);
#if 0
        bool modified = resolve_specification_attribute(dies, d, _callbacks._empool);

        if (modified) {
            //
            // Redo some finalization of the DIE
            //
            path_identifier_set(die_identifier(d));

            d._path = empool(std::string_view(qualified_symbol_name(d)));

            // recompute the die hash
            // REVISIT (fosterbrereton) : There are a number of values that,
            // when changed, require a re-hash of the die (arch, tag, and path). The setting of those values
            // so either be tucked within an API of the die (so it can rehash the value
            // on its own) or done at the end of these resolve_*
            d._hash = die_hash(d); // precompute the hash we'll use for the die map.
        }
#endif
    }

    _callbacks._register_die(std::move(dies));
}

/**************************************************************************************************/

dwarf::dwarf(object_ancestry&& ancestry,
             freader& s,
             const file_details& details,
             callbacks callbacks)
    : _impl(new implementation(std::move(ancestry), s, details, std::move(callbacks)),
            [](auto x) { delete x; }) {}

void dwarf::register_section(std::string name, std::size_t offset, std::size_t size) {
    _impl->register_section(std::move(name), offset, size);
}

void dwarf::process() { _impl->process(); }

/**************************************************************************************************/
