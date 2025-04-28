// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

//--------------------------------------------------------------------------------------------------

#define ORC_FEATURE(X) (ORC_PRIVATE_FEATURE_ ## X())

#ifndef NDEBUG
    #define ORC_PRIVATE_FEATURE_DEBUG() 1
    #define ORC_PRIVATE_FEATURE_RELEASE() 0
#else
    #define ORC_PRIVATE_FEATURE_DEBUG() 0
    #define ORC_PRIVATE_FEATURE_RELEASE() 1
#endif // !defined(NDEBUG)

#if defined(TRACY_ENABLE)
    #define ORC_PRIVATE_FEATURE_TRACY() 1
#else
    #define ORC_PRIVATE_FEATURE_TRACY() 0
#endif

//--------------------------------------------------------------------------------------------------
