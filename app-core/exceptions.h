/***********************************************************************************************************************
 *
 * @file Exceptions.h
 * @brief Custom exception classes for RF Signal Generator
 *
 **********************************************************************************************************************/

#pragma once

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#include <exception>
#include <string>

/***********************************************************************************************************************
 * EXCEPTION CLASSES
 **********************************************************************************************************************/

/**
 * @brief Base exception class for RF Signal Generator
 */
class RFSignalException : public std::exception
{
public:
    explicit RFSignalException(const std::string& message) : m_message(message)
    {
    }
    const char* what() const noexcept override
    {
        return m_message.c_str();
    }

protected:
    std::string m_message;
};

/**
 * @brief Exception for signal generation errors
 */
class SignalGenerationException : public RFSignalException
{
public:
    explicit SignalGenerationException(const std::string& message)
        : RFSignalException("Signal Generation Error: " + message)
    {
    }
};

/**
 * @brief Exception for parameter validation errors
 */
class ParameterValidationException : public RFSignalException
{
public:
    explicit ParameterValidationException(const std::string& message)
        : RFSignalException("Parameter Validation Error: " + message)
    {
    }
};

/**
 * @brief Exception for OpenGL/Graphics errors
 */
class GraphicsException : public RFSignalException
{
public:
    explicit GraphicsException(const std::string& message) : RFSignalException("Graphics Error: " + message)
    {
    }
};

/**
 * @brief Exception for memory allocation errors
 */
class MemoryException : public RFSignalException
{
public:
    explicit MemoryException(const std::string& message) : RFSignalException("Memory Error: " + message)
    {
    }
};

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
