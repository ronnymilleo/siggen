/**
 * @file    test_support.h
 * @brief   Shared test helper that unwraps std::expected results with a readable failure.
 */

#ifndef SIGGEN_TEST_SUPPORT_H
#define SIGGEN_TEST_SUPPORT_H

#include <expected>
#include <gtest/gtest.h>
#include <string>
#include <utility>

/**
 * @brief   Returns the value of a result the test expects to be valid.
 * @param[in] result  The result to unwrap.
 * @return  The contained value; on an error, records a test failure with the message and returns a
 *          default-constructed value.
 */
template <class T> T Unwrap(std::expected<T, std::string> result) {
    if (!result.has_value()) {
        ADD_FAILURE() << "Unexpected error: " << result.error();
        return T{};
    }
    return std::move(*result);
}

#endif // SIGGEN_TEST_SUPPORT_H
