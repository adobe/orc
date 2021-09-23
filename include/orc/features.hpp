// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

/**************************************************************************************************/

#define ORC_FEATURE(X) (ORC_PRIVATE_FEATURE_ ## X())

// This tool generates GB worth of in-memory data, and keeps it resident for the entire lifetime of
// the application. It does this to maximize performance while doing symbol collision analysis. It
// has been observed in Instruments that the cleanup of these large types during `std::atexit`
// contributes to *at least* half the total running time of the application. This feature is an
// experimental switch that, when enabled, will allocate these large containers via `new`, and let
// them leak intentionally. Since the destruction of these types is otherwise just to clean up
// memory that we're about to let go of anyways with the termination of the application, we let
// them leak on purpose, cutting our toMACH_Otal execution time in half (or better.)

#define ORC_PRIVATE_FEATURE_LEAKY_MEMORY() (1)

/**************************************************************************************************/
