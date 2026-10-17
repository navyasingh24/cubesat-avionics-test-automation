// Small success-or-error type used instead of exceptions for expected
// failures (bad input, bad configuration). C++17 has no std::expected, so
// this keeps the error-handling style explicit and dependency-free.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubesat {

struct Error {
    std::string code;     // Stable, machine-readable identifier, e.g. "INVALID_ARGUMENT".
    std::string message;  // Human-readable explanation.
};

template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result fail(std::string code, std::string message) {
        return Result(Error{std::move(code), std::move(message)});
    }
    static Result fail(Error error) { return Result(std::move(error)); }

    bool isOk() const { return value_.has_value(); }

    const T& value() const {
        if (!value_) {
            throw std::logic_error("Result::value() called on an error result: " + error_.code);
        }
        return *value_;
    }

    T& value() {
        if (!value_) {
            throw std::logic_error("Result::value() called on an error result: " + error_.code);
        }
        return *value_;
    }

    const Error& error() const {
        if (value_) {
            throw std::logic_error("Result::error() called on a success result");
        }
        return error_;
    }

private:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<T> value_;
    Error error_;
};

}  // namespace cubesat
