// Copyright 2024 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#pragma once

#include <functional>

//======================================================================================================================

namespace orc {

//======================================================================================================================

// Enqueue a task for (possibly asynchronous) execution. If the `parallel_processing` setting in the
// ORC config file is true, the task will be enqueued for processing on a background thread pool.
// Otherwise, the task will be executed immediately in the current thread.
void do_work(std::function<void()>);

// blocks the calling thread until all enqueued work items have completed. If the
// `parallel_processing` setting in the ORC config file is `false`, this will return immediately.
void block_on_work();

//======================================================================================================================

} // namespace orc

//======================================================================================================================
