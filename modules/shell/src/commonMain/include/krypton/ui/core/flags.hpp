#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace kr {

// Opt an enum into bit-flag operators by specialising this to true.
template <typename E>
inline constexpr bool kIsFlagEnum = false;

template <typename E>
concept FlagEnum = std::is_enum_v<E> && kIsFlagEnum<E>;

// A set of flags of one enum type. Keeps bit twiddling out of call sites.
template <FlagEnum E>
class Flags {
 public:
  using Bits = std::underlying_type_t<E>;

  constexpr Flags() = default;
  constexpr Flags(E flag) : bits_(std::to_underlying(flag)) {}  // NOLINT

  [[nodiscard]] static constexpr Flags FromBits(Bits bits) {
    Flags flags;
    flags.bits_ = bits;
    return flags;
  }

  [[nodiscard]] constexpr Bits bits() const { return bits_; }
  [[nodiscard]] constexpr bool none() const { return bits_ == 0; }
  [[nodiscard]] constexpr bool Has(E flag) const {
    return (bits_ & std::to_underlying(flag)) != 0;
  }
  [[nodiscard]] constexpr bool HasAny(Flags other) const {
    return (bits_ & other.bits_) != 0;
  }

  constexpr Flags& Set(E flag, bool on = true) {
    if (on) {
      bits_ |= std::to_underlying(flag);
    } else {
      bits_ &= static_cast<Bits>(~std::to_underlying(flag));
    }
    return *this;
  }

  constexpr Flags& operator|=(Flags other) {
    bits_ |= other.bits_;
    return *this;
  }

  friend constexpr Flags operator|(Flags a, Flags b) {
    return FromBits(a.bits_ | b.bits_);
  }
  friend constexpr Flags operator&(Flags a, Flags b) {
    return FromBits(a.bits_ & b.bits_);
  }
  friend constexpr bool operator==(Flags, Flags) = default;

 private:
  Bits bits_ = 0;
};

template <FlagEnum E>
constexpr Flags<E> operator|(E a, E b) {
  return Flags<E>(a) | Flags<E>(b);
}

}  // namespace kr
