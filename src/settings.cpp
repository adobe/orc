// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// identity
#include "orc/settings.hpp"

/**************************************************************************************************/

settings& settings::instance() {
    static settings instance;
    return instance;
}

/**************************************************************************************************/

globals& globals::instance() {
    static globals instance;
    return instance;
}

globals::~globals() {
    if (_fp.is_open()) {
        _fp.close();
    }
}

/**************************************************************************************************/

bool log_level_at_least(settings::log_level level) {
    using value_type = std::underlying_type_t<settings::log_level>;
    return static_cast<value_type>(settings::instance()._log_level) >=
           static_cast<value_type>(level);
}

/**************************************************************************************************/
