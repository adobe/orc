// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

/**************************************************************************************************/

#define ORC_FEATURE(X) (ORC_PRIVATE_FEATURE_ ## X())

#define ORC_PRIVATE_FEATURE_RELEASE() (defined(NDEBUG))
#define ORC_PRIVATE_FEATURE_DEBUG() (!ORC_PRIVATE_FEATURE_RELEASE())

/**************************************************************************************************/
