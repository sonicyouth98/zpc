#pragma once

#include <functional>
#include <type_traits>
#include <utility>

#include "zensim/math/bit/Bits.h"
#include "zensim/meta/Meta.h"
#include "zensim/meta/Relationship.h"
#include "zensim/meta/Sequence.h"

namespace zs {

  namespace mathutil_impl {
    // constexpr scan only available in c++20:
    // https://en.cppreference.com/w/cpp/algorithm/exclusive_scan
    template <typename... Args, std::size_t... Is>
    constexpr auto incl_prefix_sum_impl(std::make_signed_t<std::size_t> I,
                                        std::index_sequence<Is...>, Args &&...args) noexcept {
      return (((std::make_signed_t<std::size_t>)Is <= I ? std::forward<Args>(args) : 0) + ...);
    }
    template <typename... Args, std::size_t... Is>
    constexpr auto excl_prefix_sum_impl(std::size_t I, std::index_sequence<Is...>,
                                        Args &&...args) noexcept {
      return (((std::make_signed_t<std::size_t>)Is < I ? std::forward<Args>(args) : 0) + ...);
    }
    template <typename... Args, std::size_t... Is>
    constexpr auto excl_suffix_mul_impl(std::make_signed_t<std::size_t> I,
                                        std::index_sequence<Is...>, Args &&...args) noexcept {
      return (((std::make_signed_t<std::size_t>)Is > I ? std::forward<Args>(args) : 1) * ...);
    }
  }  // namespace mathutil_impl

  namespace math {
    /// ref: http://www.lomont.org/papers/2003/InvSqrt.pdf
    /// ref: https://cs.uwaterloo.ca/~m32rober/rsqrt.pdf
    /// ref: https://community.wolfram.com/groups/-/m/t/1108896
    /// ref:
    /// https://www.codeproject.com/Articles/69941/Best-Square-Root-Method-Algorithm-Function-Precisi
    constexpr float q_rsqrt(float number) noexcept {
      uint32_t i{};
      float x2 = number * 0.5f, y = number;
      i = *(uint32_t *)&y;
      i = 0x5f375a86 - (i >> 1);
      y = *(float *)&i;
      y = y * (1.5f - (x2 * y * y));
      y = y * (1.5f - (x2 * y * y));
      return y;
    }
    constexpr double q_rsqrt(double number) noexcept {
      uint64_t i{};
      double x2 = number * 0.5, y = number;
      i = *(uint64_t *)&y;
      i = 0x5fe6eb50c7b537a9 - (i >> 1);
      y = *(double *)&i;
      y = y * (1.5 - (x2 * y * y));
      y = y * (1.5 - (x2 * y * y));
      return y;
    }
    constexpr float q_sqrt(float x) noexcept {
#if 1
      return 1.f / q_rsqrt(x);
#else
      uint32_t i = *(uint32_t *)&x;
      // adjust bias
      i += 127 << 23;
      // approximation of square root
      i >>= 1;
      return *(float *)&i;
#endif
    }
    // best guess starting square
    constexpr double q_sqrt(double fp) noexcept { return 1.0 / q_rsqrt(fp); }
  }  // namespace math

  template <typename... Args>
  constexpr auto incl_prefix_sum(std::size_t I, Args &&...args) noexcept {
    return mathutil_impl::incl_prefix_sum_impl(I, std::index_sequence_for<Args...>{},
                                               std::forward<Args>(args)...);
  }
  template <typename... Args>
  constexpr auto excl_prefix_sum(std::size_t I, Args &&...args) noexcept {
    return mathutil_impl::excl_prefix_sum_impl(I, std::index_sequence_for<Args...>{},
                                               std::forward<Args>(args)...);
  }
  template <typename... Args>
  constexpr auto excl_suffix_mul(std::size_t I, Args &&...args) noexcept {
    return mathutil_impl::excl_suffix_mul_impl(I, std::index_sequence_for<Args...>{},
                                               std::forward<Args>(args)...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto incl_prefix_sum(std::size_t I, std::integer_sequence<Tn, Ns...>) noexcept {
    return incl_prefix_sum(I, Ns...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto excl_prefix_sum(std::size_t I, std::integer_sequence<Tn, Ns...>) noexcept {
    return excl_prefix_sum(I, Ns...);
  }
  template <typename Tn, Tn... Ns>
  constexpr auto excl_suffix_mul(std::size_t I, std::integer_sequence<Tn, Ns...>) noexcept {
    return excl_suffix_mul(I, Ns...);
  }

  template <typename T, typename Data>
  constexpr auto linear_interop(T &&alpha, Data &&a, Data &&b) noexcept {
    return a + (b - a) * alpha;
  }

  template <typename T, enable_if_t<is_same_v<T, double>> = 0>
  constexpr auto lower_trunc(T v) noexcept {
    return v > 0 ? (i64)v : ((i64)v) - 1;
  }
  template <typename T, enable_if_t<is_same_v<T, float>> = 0>
  constexpr auto lower_trunc(T v) noexcept {
    return v > 0 ? (i32)v : ((i32)v) - 1;
  }

}  // namespace zs
