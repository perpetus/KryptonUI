#pragma once

#include <expected>
#include <string>

namespace kr {

enum class ErrorCode {
  kInvalidArgument,
  kNotFound,
  kUnsupported,
  kVersionMismatch,
  kPlatform,
};

struct Error {
  ErrorCode code;
  std::string message;
};

// commonMain doesn't throw: anything fallible returns a Result.
template <typename T = void>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> Fail(ErrorCode code,
                                                 std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

}  // namespace kr
