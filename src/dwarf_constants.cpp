// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/dwarf_constants.hpp"

// stdc++
#include <cassert>

/**************************************************************************************************/

namespace dw {

/**************************************************************************************************/

const char* to_string(at attr) {
    switch (attr) {
        case at::none: return "none";
        case at::sibling: return "sibling";
        case at::location: return "location";
        case at::name: return "name";
        case at::ordering: return "ordering";
        case at::subscr_data: return "subscr_data";
        case at::byte_size: return "byte_size";
        case at::bit_offset: return "bit_offset";
        case at::bit_size: return "bit_size";
        case at::element_list: return "element_list";
        case at::stmt_list: return "stmt_list";
        case at::low_pc: return "low_pc";
        case at::high_pc: return "high_pc";
        case at::language: return "language";
        case at::member: return "member";
        case at::discr: return "discr";
        case at::discr_value: return "discr_value";
        case at::visibility: return "visibility";
        case at::import: return "import";
        case at::string_length: return "string_length";
        case at::common_reference: return "common_reference";
        case at::comp_dir: return "comp_dir";
        case at::const_value: return "const_value";
        case at::containing_type: return "containing_type";
        case at::default_value: return "default_value";
        case at::inline_: return "inline_";
        case at::is_optional: return "is_optional";
        case at::lower_bound: return "lower_bound";
        case at::producer: return "producer";
        case at::prototyped: return "prototyped";
        case at::return_addr: return "return_addr";
        case at::start_scope: return "start_scope";
        case at::bit_stride: return "bit_stride";
     // case at::stride_size: return "stride_size";
        case at::upper_bound: return "upper_bound";
        case at::abstract_origin: return "abstract_origin";
        case at::accessibility: return "accessibility";
        case at::address_class: return "address_class";
        case at::artificial: return "artificial";
        case at::base_types: return "base_types";
        case at::calling_convention: return "calling_convention";
        case at::count: return "count";
        case at::data_member_location: return "data_member_location";
        case at::decl_column: return "decl_column";
        case at::decl_file: return "decl_file";
        case at::decl_line: return "decl_line";
        case at::declaration: return "declaration";
        case at::discr_list: return "discr_list";
        case at::encoding: return "encoding";
        case at::external: return "external";
        case at::frame_base: return "frame_base";
        case at::friend_: return "friend_";
        case at::identifier_case: return "identifier_case";
        case at::macro_info: return "macro_info";
        case at::namelist_item: return "namelist_item";
        case at::priority: return "priority";
        case at::segment: return "segment";
        case at::specification: return "specification";
        case at::static_link: return "static_link";
        case at::type: return "type";
        case at::use_location: return "use_location";
        case at::variable_parameter: return "variable_parameter";
        case at::virtuality: return "virtuality";
        case at::vtable_elem_location: return "vtable_elem_location";
        case at::allocated: return "allocated";
        case at::associated: return "associated";
        case at::data_location: return "data_location";
        case at::byte_stride: return "byte_stride";
     // case at::stride: return "stride";
        case at::entry_pc: return "entry_pc";
        case at::use_utf8: return "use_utf8";
        case at::extension: return "extension";
        case at::ranges: return "ranges";
        case at::trampoline: return "trampoline";
        case at::call_column: return "call_column";
        case at::call_file: return "call_file";
        case at::call_line: return "call_line";
        case at::description: return "description";
        case at::binary_scale: return "binary_scale";
        case at::decimal_scale: return "decimal_scale";
        case at::small: return "small";
        case at::decimal_sign: return "decimal_sign";
        case at::digit_count: return "digit_count";
        case at::picture_string: return "picture_string";
        case at::mutable_: return "mutable_";
        case at::threads_scaled: return "threads_scaled";
        case at::explicit_: return "explicit_";
        case at::object_pointer: return "object_pointer";
        case at::endianity: return "endianity";
        case at::elemental: return "elemental";
        case at::pure: return "pure";
        case at::recursive: return "recursive";
        case at::signature: return "signature";
        case at::main_subprogram: return "main_subprogram";
        case at::data_bit_offset: return "data_bit_offset";
        case at::const_expr: return "const_expr";
        case at::enum_class: return "enum_class";
        case at::linkage_name: return "linkage_name";
        case at::string_length_bit_size: return "string_length_bit_size";
        case at::string_length_byte_size: return "string_length_byte_size";
        case at::rank: return "rank";
        case at::str_offsets_base: return "str_offsets_base";
        case at::addr_base: return "addr_base";
        case at::rnglists_base: return "rnglists_base";
        case at::dwo_id: return "dwo_id";
        case at::dwo_name: return "dwo_name";
        case at::reference: return "reference";
        case at::rvalue_reference: return "rvalue_reference";
        case at::macros: return "macros";
        case at::call_all_calls: return "call_all_calls";
        case at::call_all_source_calls: return "call_all_source_calls";
        case at::call_all_tail_calls: return "call_all_tail_calls";
        case at::call_return_pc: return "call_return_pc";
        case at::call_value: return "call_value";
        case at::call_origin: return "call_origin";
        case at::call_parameter: return "call_parameter";
        case at::call_pc: return "call_pc";
        case at::call_tail_call: return "call_tail_call";
        case at::call_target: return "call_target";
        case at::call_target_clobbered: return "call_target_clobbered";
        case at::call_data_location: return "call_data_location";
        case at::call_data_value: return "call_data_value";
        case at::noreturn: return "noreturn";
        case at::alignment: return "alignment";
        case at::export_symbols: return "export_symbols";
        case at::deleted: return "deleted";
        case at::defaulted: return "defaulted";
        case at::loclists_base: return "loclists_base";
        case at::ghs_namespace_alias: return "ghs_namespace_alias";
        case at::ghs_using_namespace: return "ghs_using_namespace";
        case at::ghs_using_declaration: return "ghs_using_declaration";
        case at::hp_block_index: return "hp_block_index";
     // case at::lo_user: return "lo_user";
        case at::mips_fde: return "mips_fde";
        case at::mips_loop_begin: return "mips_loop_begin";
        case at::mips_tail_loop_begin: return "mips_tail_loop_begin";
        case at::mips_epilog_begin: return "mips_epilog_begin";
        case at::mips_loop_unroll_factor: return "mips_loop_unroll_factor";
        case at::mips_software_pipeline_depth: return "mips_software_pipeline_depth";
        case at::mips_linkage_name: return "mips_linkage_name";
        case at::mips_stride: return "mips_stride";
        case at::mips_abstract_name: return "mips_abstract_name";
        case at::mips_clone_origin: return "mips_clone_origin";
        case at::mips_has_inlines: return "mips_has_inlines";
        case at::mips_stride_byte: return "mips_stride_byte";
        case at::mips_stride_elem: return "mips_stride_elem";
        case at::mips_ptr_dopetype: return "mips_ptr_dopetype";
        case at::mips_allocatable_dopetype: return "mips_allocatable_dopetype";
        case at::mips_assumed_shape_dopetype: return "mips_assumed_shape_dopetype";
        case at::mips_assumed_size: return "mips_assumed_size";
     // case at::hp_unmodifiable: return "hp_unmodifiable";
     // case at::hp_prologue: return "hp_prologue";
     // case at::hp_epilogue: return "hp_epilogue";
     // case at::hp_actuals_stmt_list: return "hp_actuals_stmt_list";
     // case at::hp_proc_per_section: return "hp_proc_per_section";
        case at::hp_raw_data_ptr: return "hp_raw_data_ptr";
        case at::hp_pass_by_reference: return "hp_pass_by_reference";
        case at::hp_opt_level: return "hp_opt_level";
        case at::hp_prof_version_id: return "hp_prof_version_id";
        case at::hp_opt_flags: return "hp_opt_flags";
        case at::hp_cold_region_low_pc: return "hp_cold_region_low_pc";
        case at::hp_cold_region_high_pc: return "hp_cold_region_high_pc";
        case at::hp_all_variables_modifiable: return "hp_all_variables_modifiable";
        case at::hp_linkage_name: return "hp_linkage_name";
        case at::hp_prof_flags: return "hp_prof_flags";
        case at::hp_unit_name: return "hp_unit_name";
        case at::hp_unit_size: return "hp_unit_size";
        case at::hp_widened_byte_size: return "hp_widened_byte_size";
        case at::hp_definition_points: return "hp_definition_points";
        case at::hp_default_location: return "hp_default_location";
        case at::hp_is_result_param: return "hp_is_result_param";
     // case at::cpq_discontig_ranges: return "cpq_discontig_ranges";
     // case at::cpq_semantic_events: return "cpq_semantic_events";
     // case at::cpq_split_lifetimes_var: return "cpq_split_lifetimes_var";
     // case at::cpq_split_lifetimes_rtn: return "cpq_split_lifetimes_rtn";
     // case at::cpq_prologue_length: return "cpq_prologue_length";
     // case at::ghs_mangled: return "ghs_mangled";
        case at::ghs_rsm: return "ghs_rsm";
        case at::ghs_frsm: return "ghs_frsm";
        case at::ghs_frames: return "ghs_frames";
        case at::ghs_rso: return "ghs_rso";
        case at::ghs_subcpu: return "ghs_subcpu";
        case at::ghs_lbrace_line: return "ghs_lbrace_line";
        case at::intel_other_endian: return "intel_other_endian";
        case at::sf_names: return "sf_names";
        case at::src_info: return "src_info";
        case at::mac_info: return "mac_info";
        case at::src_coords: return "src_coords";
        case at::body_begin: return "body_begin";
        case at::body_end: return "body_end";
        case at::gnu_vector: return "gnu_vector";
        case at::gnu_guarded_by: return "gnu_guarded_by";
        case at::gnu_pt_guarded_by: return "gnu_pt_guarded_by";
        case at::gnu_guarded: return "gnu_guarded";
        case at::gnu_pt_guarded: return "gnu_pt_guarded";
        case at::gnu_locks_excluded: return "gnu_locks_excluded";
        case at::gnu_exclusive_locks_required: return "gnu_exclusive_locks_required";
        case at::gnu_shared_locks_required: return "gnu_shared_locks_required";
        case at::gnu_odr_signature: return "gnu_odr_signature";
        case at::gnu_template_name: return "gnu_template_name";
        case at::gnu_call_site_value: return "gnu_call_site_value";
        case at::gnu_call_site_data_value: return "gnu_call_site_data_value";
        case at::gnu_call_site_target: return "gnu_call_site_target";
        case at::gnu_call_site_target_clobbered: return "gnu_call_site_target_clobbered";
        case at::gnu_tail_call: return "gnu_tail_call";
        case at::gnu_all_tail_call_sites: return "gnu_all_tail_call_sites";
        case at::gnu_all_call_sites: return "gnu_all_call_sites";
        case at::gnu_all_source_call_sites: return "gnu_all_source_call_sites";
        case at::gnu_macros: return "gnu_macros";
        case at::gnu_deleted: return "gnu_deleted";
        case at::gnu_dwo_name: return "gnu_dwo_name";
        case at::gnu_dwo_id: return "gnu_dwo_id";
        case at::gnu_ranges_base: return "gnu_ranges_base";
        case at::gnu_addr_base: return "gnu_addr_base";
        case at::gnu_pubnames: return "gnu_pubnames";
        case at::gnu_pubtypes: return "gnu_pubtypes";
        case at::gnu_discriminator: return "gnu_discriminator";
        case at::gnu_locviews: return "gnu_locviews";
        case at::gnu_entry_view: return "gnu_entry_view";
        case at::gnu_bias: return "gnu_bias";
        case at::sun_template: return "sun_template";
     // case at::vms_rtnbeg_pd_address: return "vms_rtnbeg_pd_address";
        case at::sun_alignment: return "sun_alignment";
        case at::sun_vtable: return "sun_vtable";
        case at::sun_count_guarantee: return "sun_count_guarantee";
        case at::sun_command_line: return "sun_command_line";
        case at::sun_vbase: return "sun_vbase";
        case at::sun_compile_options: return "sun_compile_options";
        case at::sun_language: return "sun_language";
        case at::sun_browser_file: return "sun_browser_file";
        case at::sun_vtable_abi: return "sun_vtable_abi";
        case at::sun_func_offsets: return "sun_func_offsets";
        case at::sun_cf_kind: return "sun_cf_kind";
        case at::sun_vtable_index: return "sun_vtable_index";
        case at::sun_omp_tpriv_addr: return "sun_omp_tpriv_addr";
        case at::sun_omp_child_func: return "sun_omp_child_func";
        case at::sun_func_offset: return "sun_func_offset";
        case at::sun_memop_type_ref: return "sun_memop_type_ref";
        case at::sun_profile_id: return "sun_profile_id";
        case at::sun_memop_signature: return "sun_memop_signature";
        case at::sun_obj_dir: return "sun_obj_dir";
        case at::sun_obj_file: return "sun_obj_file";
        case at::sun_original_name: return "sun_original_name";
        case at::sun_hwcprof_signature: return "sun_hwcprof_signature";
        case at::sun_amd64_parmdump: return "sun_amd64_parmdump";
        case at::sun_part_link_name: return "sun_part_link_name";
        case at::sun_link_name: return "sun_link_name";
        case at::sun_pass_with_const: return "sun_pass_with_const";
        case at::sun_return_with_const: return "sun_return_with_const";
        case at::sun_import_by_name: return "sun_import_by_name";
        case at::sun_f90_pointer: return "sun_f90_pointer";
        case at::sun_pass_by_ref: return "sun_pass_by_ref";
        case at::sun_f90_allocatable: return "sun_f90_allocatable";
        case at::sun_f90_assumed_shape_array: return "sun_f90_assumed_shape_array";
        case at::sun_c_vla: return "sun_c_vla";
        case at::sun_return_value_ptr: return "sun_return_value_ptr";
        case at::sun_dtor_start: return "sun_dtor_start";
        case at::sun_dtor_length: return "sun_dtor_length";
        case at::sun_dtor_state_initial: return "sun_dtor_state_initial";
        case at::sun_dtor_state_final: return "sun_dtor_state_final";
        case at::sun_dtor_state_deltas: return "sun_dtor_state_deltas";
        case at::sun_import_by_lname: return "sun_import_by_lname";
        case at::sun_f90_use_only: return "sun_f90_use_only";
        case at::sun_namelist_spec: return "sun_namelist_spec";
        case at::sun_is_omp_child_func: return "sun_is_omp_child_func";
        case at::sun_fortran_main_alias: return "sun_fortran_main_alias";
        case at::sun_fortran_based: return "sun_fortran_based";
        case at::altium_loclist: return "altium_loclist";
        case at::use_gnat_descriptive_type: return "use_gnat_descriptive_type";
        case at::gnat_descriptive_type: return "gnat_descriptive_type";
        case at::gnu_numerator: return "gnu_numerator";
        case at::gnu_denominator: return "gnu_denominator";
        case at::go_kind: return "go_kind";
        case at::go_key: return "go_key";
        case at::go_elem: return "go_elem";
        case at::go_embedded_field: return "go_embedded_field";
        case at::go_runtime_type: return "go_runtime_type";
        case at::upc_threads_scaled: return "upc_threads_scaled";
        case at::ibm_wsa_addr: return "ibm_wsa_addr";
        case at::ibm_home_location: return "ibm_home_location";
        case at::ibm_alt_srcview: return "ibm_alt_srcview";
        case at::pgi_lbase: return "pgi_lbase";
        case at::pgi_soffset: return "pgi_soffset";
        case at::pgi_lstride: return "pgi_lstride";
        case at::borland_property_read: return "borland_property_read";
        case at::borland_property_write: return "borland_property_write";
        case at::borland_property_implements: return "borland_property_implements";
        case at::borland_property_index: return "borland_property_index";
        case at::borland_property_default: return "borland_property_default";
        case at::borland_delphi_unit: return "borland_delphi_unit";
        case at::borland_delphi_class: return "borland_delphi_class";
        case at::borland_delphi_record: return "borland_delphi_record";
        case at::borland_delphi_metaclass: return "borland_delphi_metaclass";
        case at::borland_delphi_constructor: return "borland_delphi_constructor";
        case at::borland_delphi_destructor: return "borland_delphi_destructor";
        case at::borland_delphi_anonymous_method: return "borland_delphi_anonymous_method";
        case at::borland_delphi_interface: return "borland_delphi_interface";
        case at::borland_delphi_abi: return "borland_delphi_abi";
        case at::borland_delphi_frameptr: return "borland_delphi_frameptr";
        case at::borland_closure: return "borland_closure";
        case at::llvm_include_path: return "llvm_include_path";
        case at::llvm_config_macros: return "llvm_config_macros";
        case at::llvm_sysroot: return "llvm_sysroot";
        case at::llvm_tag_offset: return "llvm_tag_offset";
     // case at::llvm_apinotes: return "llvm_apinotes";
        case at::apple_optimized: return "apple_optimized";
        case at::apple_flags: return "apple_flags";
        case at::apple_isa: return "apple_isa";
        case at::apple_block: return "apple_block";
        case at::apple_major_runtime_vers: return "apple_major_runtime_vers";
        case at::apple_runtime_class: return "apple_runtime_class";
        case at::apple_omit_frame_ptr: return "apple_omit_frame_ptr";
        case at::apple_property_name: return "apple_property_name";
        case at::apple_property_getter: return "apple_property_getter";
        case at::apple_property_setter: return "apple_property_setter";
        case at::apple_property_attribute: return "apple_property_attribute";
        case at::apple_objc_complete_type: return "apple_objc_complete_type";
        case at::apple_property: return "apple_property";
        case at::apple_objc_direct: return "apple_objc_direct";
        case at::apple_sdk: return "apple_sdk";
        case at::hi_user: return "hi_user";
    }
}

/**************************************************************************************************/

const char* to_string(tag t) {
    switch (t) {
        case tag::none: return "none";
        case tag::array_type: return "array";
        case tag::class_type: return "class";
        case tag::entry_point: return "entry point";
        case tag::enumeration_type: return "enumeration";
        case tag::formal_parameter: return "formal parameter";
        case tag::imported_declaration: return "imported declaration";
        case tag::label: return "label";
        case tag::lexical_block: return "lexical block";
        case tag::member: return "member";
        case tag::pointer_type: return "pointer";
        case tag::reference_type: return "reference";
        case tag::compile_unit: return "compile unit";
        case tag::string_type: return "string";
        case tag::structure_type: return "structure";
        case tag::subroutine_type: return "subroutine";
        case tag::typedef_: return "typedef";
        case tag::union_type: return "union";
        case tag::unspecified_parameters: return "unspecified parameters";
        case tag::variant: return "variant";
        case tag::common_block: return "common block";
        case tag::common_inclusion: return "common inclusion";
        case tag::inheritance: return "inheritance";
        case tag::inlined_subroutine: return "inlined subroutine";
        case tag::module: return "module";
        case tag::ptr_to_member_type: return "ptr to member";
        case tag::set_type: return "set";
        case tag::subrange_type: return "subrange";
        case tag::with_stmt: return "with stmt";
        case tag::access_declaration: return "access declaration";
        case tag::base_type: return "base";
        case tag::catch_block: return "catch block";
        case tag::const_type: return "const";
        case tag::constant: return "constant";
        case tag::enumerator: return "enumerator";
        case tag::file_type: return "file";
        case tag::friend_: return "friend";
        case tag::namelist: return "namelist";
        case tag::namelist_item: return "namelist item";
        // case tag::namelist_items: return "namelist items";
        case tag::packed_type: return "packed";
        case tag::subprogram: return "subprogram";
        case tag::template_type_parameter: return "template type parameter";
        // case tag::template_type_param: return "template type param";
        case tag::template_value_parameter: return "template value parameter";
        // case tag::template_value_param: return "template value param";
        case tag::thrown_type: return "thrown";
        case tag::try_block: return "try block";
        case tag::variant_part: return "variant part";
        case tag::variable: return "variable";
        case tag::volatile_type: return "volatile";
        case tag::dwarf_procedure: return "dwarf procedure";
        case tag::restrict_type: return "restrict";
        case tag::interface_type: return "interface";
        case tag::namespace_: return "namespace";
        case tag::imported_module: return "imported module";
        case tag::unspecified_type: return "unspecified";
        case tag::partial_unit: return "partial unit";
        case tag::imported_unit: return "imported unit";
        case tag::mutable_type: return "mutable";
        case tag::condition: return "condition";
        case tag::shared_type: return "shared";
        case tag::type_unit: return "type unit";
        case tag::rvalue_reference_type: return "rvalue reference";
        case tag::template_alias: return "template alias";
        case tag::coarray_type: return "coarray";
        case tag::generic_subrange: return "generic subrange";
        case tag::dynamic_type: return "dynamic";
        case tag::atomic_type: return "atomic";
        case tag::call_site: return "call site";
        case tag::call_site_parameter: return "call site parameter";
        case tag::skeleton_unit: return "skeleton unit";
        case tag::immutable_type: return "immutable";
        case tag::lo_user: return "lo user";
        case tag::mips_loop: return "mips loop";
        case tag::hp_array_descriptor: return "hp array descriptor";
        case tag::format_label: return "format label";
        case tag::function_template: return "function template";
        case tag::class_template: return "class template";
        case tag::gnu_bincl: return "gnu bincl";
        case tag::gnu_eincl: return "gnu eincl";
        case tag::gnu_template_template_parameter: return "gnu template template parameter";
        // case tag::gnu_template_template_param: return "gnu template template param";
        case tag::gnu_template_parameter_pack: return "gnu template parameter pack";
        case tag::gnu_formal_parameter_pack: return "gnu formal parameter pack";
        case tag::gnu_call_site: return "gnu call site";
        case tag::gnu_call_site_parameter: return "gnu call site parameter";
        case tag::altium_circ_type: return "altium circ";
        case tag::altium_mwa_circ_type: return "altium mwa circ";
        case tag::altium_rev_carry_type: return "altium rev carry";
        case tag::altium_rom: return "altium rom";
        case tag::upc_shared_type: return "upc shared";
        case tag::upc_strict_type: return "upc strict";
        case tag::upc_relaxed_type: return "upc relaxed";
        case tag::apple_property: return "apple property";
        case tag::sun_function_template: return "sun function template";
        case tag::sun_class_template: return "sun class template";
        case tag::sun_struct_template: return "sun struct template";
        case tag::sun_union_template: return "sun union template";
        case tag::sun_indirect_inheritance: return "sun indirect inheritance";
        case tag::sun_codeflags: return "sun codeflags";
        case tag::sun_memop_info: return "sun memop info";
        case tag::sun_omp_child_func: return "sun omp child func";
        case tag::sun_rtti_descriptor: return "sun rtti descriptor";
        case tag::sun_dtor_info: return "sun dtor info";
        case tag::sun_dtor: return "sun dtor";
        case tag::sun_f90_interface: return "sun f90 interface";
        case tag::sun_fortran_vax_structure: return "sun fortran vax structure";
        case tag::sun_hi: return "sun hi";
        case tag::ghs_namespace: return "ghs namespace";
        case tag::ghs_using_namespace: return "ghs using namespace";
        case tag::ghs_using_declaration: return "ghs using declaration";
        case tag::ghs_template_templ_param: return "ghs template templ param";
        case tag::pgi_kanji_type: return "pgi kanji";
        case tag::pgi_interface_block: return "pgi interface block";
        case tag::borland_property: return "borland property";
        case tag::borland_delphi_string: return "borland delphi string";
        case tag::borland_delphi_dynamic_array: return "borland delphi dynamic array";
        case tag::borland_delphi_set: return "borland delphi set";
        case tag::borland_delphi_variant: return "borland delphi variant";
        case tag::hi_user: return "hi user";
    }
}

bool is_type(tag t) {
    switch (t) {
        case dw::tag::array_type:
        case dw::tag::class_type:
        case dw::tag::interface_type:
        case dw::tag::enumeration_type:
        case dw::tag::pointer_type:
        case dw::tag::reference_type:
        case dw::tag::rvalue_reference_type:
        case dw::tag::string_type:
        case dw::tag::structure_type:
        case dw::tag::subroutine_type:
        case dw::tag::union_type:
        case dw::tag::ptr_to_member_type:
        case dw::tag::set_type:
        case dw::tag::subrange_type:
        case dw::tag::base_type:
        case dw::tag::const_type:
        case dw::tag::file_type:
        case dw::tag::packed_type:
        case dw::tag::volatile_type:
        case dw::tag::typedef_:
            return true;
        default:
            return false;
    }
}

/**************************************************************************************************/

} // namespace dw

/**************************************************************************************************/
