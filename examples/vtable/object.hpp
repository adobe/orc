// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>
#include <string>

namespace example_vtable {

struct object {
#if ADOBE_ORC_ENABLE_FLAG()
    virtual std::string optional() const;
#endif // ADOBE_ORC_ENABLE_FLAG()

    virtual std::string api() const;
};

} // namespace example_vtable {
