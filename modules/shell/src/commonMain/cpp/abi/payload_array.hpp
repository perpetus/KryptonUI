#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

#include "krypton/ui/core/result.hpp"

namespace kr::abi {

// Array stride and each record's size are independent. Read through bytes so
// extension records need not retain the current type's size or alignment.
template <typename T>
  requires std::is_trivially_copyable_v<T>
class PayloadArray {
 public:
  [[nodiscard]] static Result<PayloadArray> Read(const T* data,
                                                 std::size_t count,
                                                 std::size_t stride,
                                                 std::size_t minimum) {
    if (count == 0)
      return PayloadArray{};
    if (!data || stride < minimum ||
        count > static_cast<std::size_t>(
                    std::numeric_limits<std::ptrdiff_t>::max()) /
                    stride)
      return Fail(ErrorCode::kInvalidArgument, "invalid payload array extent");
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    for (std::size_t index = 0; index < count; ++index) {
      std::size_t size = 0;
      std::memcpy(&size, bytes + index * stride, sizeof(size));
      if (size < minimum || size > stride)
        return Fail(ErrorCode::kInvalidArgument, "incomplete array record");
    }
    return PayloadArray(bytes, count, stride);
  }

  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  [[nodiscard]] T operator[](std::size_t index) const noexcept {
    const auto* bytes = data_ + index * stride_;
    std::size_t size = 0;
    std::memcpy(&size, bytes, sizeof(size));
    T record{};
    std::memcpy(&record, bytes, std::min(size, sizeof(record)));
    return record;
  }

 private:
  PayloadArray() = default;
  PayloadArray(const std::byte* data, std::size_t count, std::size_t stride)
      : data_(data), count_(count), stride_(stride) {}

  const std::byte* data_ = nullptr;
  std::size_t count_ = 0;
  std::size_t stride_ = 0;
};

}  // namespace kr::abi
