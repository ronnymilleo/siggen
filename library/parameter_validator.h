/***********************************************************************************************************************
 *
 * @file ParameterValidator.h
 * @brief Parameter validation utilities for RF Signal Generator
 *
 **********************************************************************************************************************/

#pragma once

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#include <algorithm>
#include <cmath>
#include <exceptions.h>

/***********************************************************************************************************************
 * PARAMETER VALIDATION UTILITIES
 **********************************************************************************************************************/

class ParameterValidator
{
public:
    /**
     * @brief Validate integer parameter within range
     */
    static void ValidateIntRange(int value, int min, int max, const std::string& paramName)
    {
        if (value < min || value > max)
        {
            throw ParameterValidationException(paramName + " must be between " + std::to_string(min) + " and " +
                                               std::to_string(max) + ". Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Validate float parameter within range
     */
    static void ValidateFloatRange(float value, float min, float max, const std::string& paramName)
    {
        if (value < min || value > max || std::isnan(value) || std::isinf(value))
        {
            throw ParameterValidationException(paramName + " must be between " + std::to_string(min) + " and " +
                                               std::to_string(max) + ". Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Validate double parameter within range
     */
    static void ValidateDoubleRange(double value, double min, double max, const std::string& paramName)
    {
        if (value < min || value > max || std::isnan(value) || std::isinf(value))
        {
            throw ParameterValidationException(paramName + " must be between " + std::to_string(min) + " and " +
                                               std::to_string(max) + ". Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Validate positive integer
     */
    static void ValidatePositiveInt(int value, const std::string& paramName)
    {
        if (value <= 0)
        {
            throw ParameterValidationException(paramName + " must be positive. Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Validate positive float
     */
    static void ValidatePositiveFloat(float value, const std::string& paramName)
    {
        if (value <= 0.0f || std::isnan(value) || std::isinf(value))
        {
            throw ParameterValidationException(paramName + " must be positive. Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Validate power of 2
     */
    static void ValidatePowerOfTwo(int value, const std::string& paramName)
    {
        if (value <= 0 || (value & (value - 1)) != 0)
        {
            throw ParameterValidationException(paramName + " must be a power of 2. Got: " + std::to_string(value));
        }
    }

    /**
     * @brief Clamp integer value to range
     */
    static int ClampInt(int value, int min, int max)
    {
        return std::max(min, std::min(max, value));
    }

    /**
     * @brief Clamp float value to range
     */
    static float ClampFloat(float value, float min, float max)
    {
        if (std::isnan(value))
            return min;
        if (std::isinf(value))
            return value > 0 ? max : min;
        return std::max(min, std::min(max, value));
    }

    /**
     * @brief Clamp double value to range
     */
    static double ClampDouble(double value, double min, double max)
    {
        if (std::isnan(value))
            return min;
        if (std::isinf(value))
            return value > 0 ? max : min;
        return std::max(min, std::min(max, value));
    }
};

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
