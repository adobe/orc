/*
    Copyright 2024 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE file or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef ADOBE_CONTRACT_CHECKS_HPP
#define ADOBE_CONTRACT_CHECKS_HPP

#define INTERNAL_ADOBE_CONTRACT_VIOLATION_verbose 1
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_lightweight 2
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_unsafe 3
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_custom_verbose 4
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_custom_lightweight 5

// INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR is not a function-style
// macro because it lets us give a better diagnostic on
// misconfiguration.
#ifndef ADOBE_CONTRACT_VIOLATION// Default is verbose
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR INTERNAL_ADOBE_CONTRACT_VIOLATION_verbose
#else
#define INTERNAL_ADOBE_CONCATENATE1(x, y) x##y
#define INTERNAL_ADOBE_CONCATENATE(x, y) INTERNAL_ADOBE_CONCATENATE1(x, y)
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR \
  INTERNAL_ADOBE_CONCATENATE(INTERNAL_ADOBE_CONTRACT_VIOLATION_, ADOBE_CONTRACT_VIOLATION)
#endif

#if INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR == INTERNAL_ADOBE_CONTRACT_VIOLATION_unsafe

#define INTERNAL_ADOBE_CONTRACT_CHECK_1(kind, condition) while (false)
#define INTERNAL_ADOBE_CONTRACT_CHECK_2(kind, condition, message) while (false)

#else

// Recent compilers will support [[unlikely]] even in C++17 mode, but
// they also will warn if you use this C++20 feature in C++17 mode, so
// we cannot use it unless we have C++20.
#if __cplusplus >= 2020002 && __has_cpp_attribute(unlikely)
// The attribute (if any) that marks the cold path in a contract check.
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_LIKELIHOOD [[unlikely]]
#else
// The attribute (if any) that marks the cold path in a contract check.
#define INTERNAL_ADOBE_CONTRACT_VIOLATION_LIKELIHOOD
#endif

namespace adobe {
enum class contract_violation_kind { precondition, invariant };
}// namespace adobe

#endif

#if INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR == INTERNAL_ADOBE_CONTRACT_VIOLATION_verbose

#define INTERNAL_ADOBE_CONTRACT_VIOLATED(condition, kind, file, line, message) \
  ::adobe::detail::contract_violated(condition, kind, file, line, message)

#include <cstdint>
#include <cstdio>
#include <exception>

namespace adobe {
namespace detail {
  [[noreturn]] inline void contract_violated(const char *const condition,
    contract_violation_kind kind,
    const char *const file,
    std::uint32_t const line,
    const char *const message) noexcept
  {
    const char *const description = kind == contract_violation_kind::precondition
                                      ? "Precondition violated"
                                      : "Invariant not upheld";
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ::std::fprintf(stderr,
      "%s:%d: %s (%s). %s\n",
      file,
      static_cast<int>(line),
      description,
      condition,
      message);
    (void)std::fflush(stderr);
    ::std::terminate();
  }
}// namespace detail
}// namespace adobe

#elif INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR == INTERNAL_ADOBE_CONTRACT_VIOLATION_lightweight

#include <exception>

#define INTERNAL_ADOBE_CONTRACT_VIOLATED(condition, kind, file, line, message) ::std::terminate()

#elif INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR == INTERNAL_ADOBE_CONTRACT_VIOLATION_custom_verbose

#define INTERNAL_ADOBE_CONTRACT_VIOLATED(condition, kind, file, line, message) \
  ::adobe::contract_violated_verbose(condition, kind, file, line, message)

#include <cstdint>

namespace adobe {
[[noreturn]] extern void contract_violated_verbose(const char *condition,
  adobe::contract_violation_kind kind,
  const char *file,
  std::uint32_t line,
  const char *message);
}

#elif INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR \
  == INTERNAL_ADOBE_CONTRACT_VIOLATION_custom_lightweight

#define INTERNAL_ADOBE_CONTRACT_VIOLATED(condition, kind, file, line, message) \
  ::adobe::contract_violated_lightweight()

namespace adobe {
[[noreturn]] extern void contract_violated_lightweight();
}

#elif INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR == INTERNAL_ADOBE_CONTRACT_VIOLATION_unsafe

#else
#define ADOBE_INTERNAL_STRINGIFY1(x) #x
#define ADOBE_INTERNAL_STRINGIFY(x) ADOBE_INTERNAL_STRINGIFY1(x)
static_assert(false,
  "Unknown configuration ADOBE_CONTRACT_VIOLATION=" ADOBE_INTERNAL_STRINGIFY(
    ADOBE_CONTRACT_VIOLATION) ".  Valid values are \"verbose\", \"lightweight\", or \"unsafe\"");
#endif

#if INTERNAL_ADOBE_CONTRACT_VIOLATION_BEHAVIOR != INTERNAL_ADOBE_CONTRACT_VIOLATION_unsafe

// Expands to a statement that reports a contract violation of the
// given kind when condition is false.
#define INTERNAL_ADOBE_CONTRACT_CHECK_1(kind, condition) \
  if (condition)                                         \
    ;                                                    \
  else                                                   \
    INTERNAL_ADOBE_CONTRACT_VIOLATION_LIKELIHOOD         \
  INTERNAL_ADOBE_CONTRACT_VIOLATED(#condition, kind, __FILE__, __LINE__, "")

// Expands to a statement that reports a contract violation of the
// given kind, with <message: const char*> when condition is false.
#define INTERNAL_ADOBE_CONTRACT_CHECK_2(kind, condition, message) \
  if (condition)                                                  \
    ;                                                             \
  else                                                            \
    INTERNAL_ADOBE_CONTRACT_VIOLATION_LIKELIHOOD                  \
  INTERNAL_ADOBE_CONTRACT_VIOLATED(#condition, kind, __FILE__, __LINE__, message)
#endif


// Part of a workaround for an MSVC preprocessor bug. See
// https://stackoverflow.com/a/5134656.
#define INTERNAL_ADOBE_MSVC_EXPAND(x) x

// Contract checking macros take a condition and an optional second argument.
//
// Information on how to simulate optional arguments is here:
// https://stackoverflow.com/questions/3046889/optional-parameters-with-c-macros.
//
// The user experience when zero or three arguments are passed could
// be improved; portably detecting empty macro arguments could be used
// to help.

// Expands to its third argument
#define INTERNAL_ADOBE_THIRD_ARGUMENT(arg0, arg1, invocation, ...) invocation

// ADOBE_PRECONDITION(<condition>);
// ADOBE_PRECONDITION(<condition>, <message: const char*>);
//
// Expands to a statement that reports a precondition violation (with
// <message> if supplied) when <condition> is false.
#define ADOBE_PRECONDITION(...) \
  ADOBE_CONTRACT_CHECK(::adobe::contract_violation_kind::precondition, __VA_ARGS__)

// ADOBE_INVARIANT(<condition>);
// ADOBE_INVARIANT(<condition>, <message: const char*>);
//
// Expands to a statement that reports an invariant violation (with
// <message> if supplied) when <condition> is false.
#define ADOBE_INVARIANT(...) \
  ADOBE_CONTRACT_CHECK(::adobe::contract_violation_kind::invariant, __VA_ARGS__)

// ADOBE_CONTRACT_CHECK(<integer kind>, <condition>);
// ADOBE_CONTRACT_CHECK(<integer kind>, <condition>, <message: const char*>);
//
// Expands to a statement that reports a contract violation of the
// given kind (with <message>, if supplied) when <condition> is false.
#define ADOBE_CONTRACT_CHECK(kind, ...)                                                      \
  INTERNAL_ADOBE_MSVC_EXPAND(INTERNAL_ADOBE_THIRD_ARGUMENT(                                  \
    __VA_ARGS__, INTERNAL_ADOBE_CONTRACT_CHECK_2, INTERNAL_ADOBE_CONTRACT_CHECK_1, ignored)( \
    kind, __VA_ARGS__))

#endif
