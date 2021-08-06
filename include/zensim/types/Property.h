#pragma once
#include <type_traits>

#include "zensim/meta/Meta.h"

namespace zs {

  enum struct attrib_e : unsigned char { scalar = 0, vector, matrix, affine };
  using attrib_scalar_tag = wrapv<attrib_e::scalar>;
  using attrib_vector_tag = wrapv<attrib_e::vector>;
  using attrib_matrix_tag = wrapv<attrib_e::matrix>;
  using attrib_affine_matrix_tag = wrapv<attrib_e::affine>;
  constexpr auto scalar_v = attrib_scalar_tag{};
  constexpr auto vector_v = attrib_vector_tag{};
  constexpr auto matrix_v = attrib_matrix_tag{};
  constexpr auto affine_matrix_v = attrib_affine_matrix_tag{};

  enum struct layout_e : int { aos = 0, soa, aosoa };
  using layout_aos_tag = wrapv<layout_e::aos>;
  using layout_soa_tag = wrapv<layout_e::soa>;
  using layout_aosoa_tag = wrapv<layout_e::aosoa>;
  constexpr auto aos_v = layout_aos_tag{};
  constexpr auto soa_v = layout_soa_tag{};
  constexpr auto aosoa_v = layout_aosoa_tag{};

  enum struct kernel_e { linear = 2, quadratic = 3, cubic = 4 };
  using kernel_linear_tag = wrapv<kernel_e::linear>;
  using kernel_quadratic_tag = wrapv<kernel_e::quadratic>;
  using kernel_cubic_tag = wrapv<kernel_e::cubic>;
  constexpr auto kernel_linear = kernel_linear_tag{};
  constexpr auto kernel_quad = kernel_quadratic_tag{};
  constexpr auto kernel_cubic = kernel_cubic_tag{};

  enum struct execspace_e : unsigned char { host = 0, openmp, cuda, hip };
  using host_exec_tag = wrapv<execspace_e::host>;
  using omp_exec_tag = wrapv<execspace_e::openmp>;
  using cuda_exec_tag = wrapv<execspace_e::cuda>;
  using hip_exec_tag = wrapv<execspace_e::hip>;
  constexpr auto exec_seq = host_exec_tag{};
  constexpr auto exec_omp = omp_exec_tag{};
  constexpr auto exec_cuda = cuda_exec_tag{};
  constexpr auto exec_hip = hip_exec_tag{};

  // HOST, DEVICE, UM
  enum struct memsrc_e : unsigned char { host = 0, device, um };
  using host_mem_tag = wrapv<memsrc_e::host>;
  using device_mem_tag = wrapv<memsrc_e::device>;
  using um_mem_tag = wrapv<memsrc_e::um>;
  constexpr auto mem_host = host_mem_tag{};
  constexpr auto mem_device = device_mem_tag{};
  constexpr auto mem_um = um_mem_tag{};

  /// comparable
  template <typename T> struct is_equality_comparable {
  private:
    static void *conv(bool);
    template <typename U> static std::true_type test(
        decltype(conv(std::declval<U const &>() == std::declval<U const &>())),
        decltype(conv(!std::declval<U const &>() == std::declval<U const &>())));
    template <typename U> static std::false_type test(...);

  public:
    static constexpr bool value = decltype(test<T>(nullptr, nullptr))::value;
  };

}  // namespace zs
