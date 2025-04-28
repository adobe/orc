// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

// stdc++
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>
#include <fstream>

// application
#include "orc/features.hpp"

//--------------------------------------------------------------------------------------------------

struct settings {
    enum class log_level {
        silent, // emit nothing but ODRVs
        warning, // emit issues that need to be fixed
        info, // emit brief, informative status
        verbose, // emit as much as possible
    };

    enum class output_file_mode {
        text,
        json,
    };

    static settings& instance();

    bool _graceful_exit{false};
    std::size_t _max_violation_count{0};
    bool _forward_to_linker{true};
    log_level _log_level{log_level::silent};
    bool _standalone_mode{false};
    bool _dylib_scan_mode{false};
    bool _print_object_file_list{false};
    std::vector<std::string> _symbol_ignore;
    std::vector<std::string> _violation_report;
    std::vector<std::string> _violation_ignore;
    bool _parallel_processing{true};
    bool _filter_redundant{true};
    std::string _relative_output_file;
    output_file_mode _output_file_mode{output_file_mode::text};
};

//--------------------------------------------------------------------------------------------------

struct globals {
    static globals& instance();
    ~globals();

    // Don't call this in the middle of processing, or you're going to get inconsistent numbers.
    void reset() {
        _object_file_count = 0;
        _odrv_count = 0;
        _unique_symbol_count = 0;
        _die_processed_count = 0;
        _die_skipped_count = 0;
    }

    std::atomic_size_t _object_file_count{0};
    std::atomic_size_t _odrv_count{0};
    std::atomic_size_t _unique_symbol_count{0};
    std::atomic_size_t _die_processed_count{0};
    std::atomic_size_t _die_skipped_count{0};
    std::ofstream _fp;

private:
    globals() = default;
};

//--------------------------------------------------------------------------------------------------
// returns true iff the current log level is at least as noisy as the passed-in value.
bool log_level_at_least(settings::log_level);

//--------------------------------------------------------------------------------------------------
