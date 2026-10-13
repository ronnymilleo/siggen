/**
 * @file    error_codes.h
 * @brief   Status codes returned by initialization and run functions across the project.
 * @details Core::ExitSuccess means the operation completed; Core::ExitFailure means it failed and the
 *          caller reports the details.
 */

#ifndef SIGGEN_ERROR_CODES_H
#define SIGGEN_ERROR_CODES_H

#include <cstdlib>

namespace Core {

constexpr int ExitFailure = EXIT_FAILURE;
constexpr int ExitSuccess = EXIT_SUCCESS;

} // namespace Core

#endif // SIGGEN_ERROR_CODES_H
