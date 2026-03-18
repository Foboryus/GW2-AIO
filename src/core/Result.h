#pragma once

#include <QString>
#include <QVariant>
#include <optional>

/**
 * @brief Result type for error handling
 * 
 * Provides a clean way to return success/error without exceptions.
 * Similar to Rust's Result<T, E> pattern.
 */
template<typename T>
class Result
{
public:
    // Success constructors
    Result(const T& value) : m_value(value), m_success(true) {}
    Result(T&& value) : m_value(std::move(value)), m_success(true) {}
    
    // Error constructor
    static Result error(const QString& errorMessage) {
        Result r;
        r.m_success = false;
        r.m_error = errorMessage;
        return r;
    }
    
    // Static success factory
    static Result success(const T& value) {
        return Result(value);
    }
    
    // Check status
    bool isSuccess() const { return m_success; }
    bool isError() const { return !m_success; }
    explicit operator bool() const { return m_success; }
    
    // Access value (only valid if success)
    const T& value() const { return m_value.value(); }
    T& value() { return m_value.value(); }
    
    // Access with default
    T valueOr(const T& defaultValue) const {
        return m_success ? m_value.value() : defaultValue;
    }
    
    // Access error
    const QString& error() const { return m_error; }
    
    // Unwrap (throws if error)
    const T& unwrap() const {
        if (!m_success) {
            throw std::runtime_error(m_error.toStdString());
        }
        return m_value.value();
    }
    
private:
    Result() = default;
    
    std::optional<T> m_value;
    bool m_success = false;
    QString m_error;
};

/**
 * @brief Void result for operations without return value
 */
class VoidResult
{
public:
    VoidResult() : m_success(true) {}
    
    static VoidResult success() { return VoidResult(); }
    
    static VoidResult error(const QString& errorMessage) {
        VoidResult r;
        r.m_success = false;
        r.m_error = errorMessage;
        return r;
    }
    
    bool isSuccess() const { return m_success; }
    bool isError() const { return !m_success; }
    explicit operator bool() const { return m_success; }
    
    const QString& error() const { return m_error; }
    
private:
    bool m_success = false;
    QString m_error;
};

// Convenience macros
#define TRY(expr) \
    do { \
        auto _result = (expr); \
        if (_result.isError()) return _result; \
    } while(0)

#define TRY_OR_LOG(expr, fallback) \
    [&]() { \
        auto _result = (expr); \
        if (_result.isError()) { \
            qWarning() << _result.error(); \
            return fallback; \
        } \
        return _result.value(); \
    }()
