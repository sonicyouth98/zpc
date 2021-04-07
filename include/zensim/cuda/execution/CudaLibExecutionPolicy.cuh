#pragma once
#include <cublas_v2.h>
#include <cusolverSp.h>
#include <cusparse_v2.h>

#include "ExecutionPolicy.cuh"
#include "zensim/Logger.hpp"
#include "zensim/Reflection.h"
#include "zensim/types/Function.h"

namespace zs {

  enum CudaLibraryComponentFlagBit : u64 {
    culib_none = 0,
    culib_cusparse = 1,
    culib_cublas = 0x2,
    culib_cusolversp = 0x4,
    culib_cusolverdn = 0x8
  };

  template <CudaLibraryComponentFlagBit flagbit> struct CudaLibHandle {};

  namespace details {
    template <typename T> void check_culib_error(T result) {
      if (static_cast<int>(result) != 0) {
        fmt::print("culib execution of {} error: code [{}]\n", demangle<T>(),
                   static_cast<int>(result));
      }
    }
    template <CudaLibraryComponentFlagBit flagbit> struct CudaLibStatusType { using type = void; };
    template <> struct CudaLibStatusType<culib_cusparse> { using type = cusparseStatus_t; };
    template <> struct CudaLibStatusType<culib_cublas> { using type = cublasStatus_t; };
    template <> struct CudaLibStatusType<culib_cusolversp> { using type = cusolverStatus_t; };
    template <> struct CudaLibStatusType<culib_cusolverdn> { using type = cusolverStatus_t; };
  }  // namespace details
  template <CudaLibraryComponentFlagBit flagbit> using cudaLibStatus_t =
      typename details::CudaLibStatusType<flagbit>::type;

  template <> struct CudaLibHandle<culib_cusparse> {
    cusparseHandle_t handle{nullptr};
    CudaLibHandle(CudaExecutionPolicy& cupol) {
      details::check_culib_error(cusparseCreate(&handle));
      details::check_culib_error(
          cusparseSetStream(handle, (cudaStream_t)Cuda::ref_cuda_context(cupol.getProcid())
                                        .stream_spare(cupol.getStreamid())));
    }
    ~CudaLibHandle() { details::check_culib_error(cusparseDestroy(handle)); }
  };
  template <> struct CudaLibHandle<culib_cublas> {
    cublasHandle_t handle{nullptr};
    CudaLibHandle(CudaExecutionPolicy& cupol) {
      details::check_culib_error(cublasCreate(&handle));
      details::check_culib_error(
          cublasSetStream(handle, (cudaStream_t)Cuda::ref_cuda_context(cupol.getProcid())
                                      .stream_spare(cupol.getStreamid())));
    }
    ~CudaLibHandle() { details::check_culib_error(cublasDestroy(handle)); }
  };
  template <> struct CudaLibHandle<culib_cusolversp> {
    cusolverSpHandle_t handle{nullptr};
    CudaLibHandle(CudaExecutionPolicy& cupol) {
      details::check_culib_error(cusolverSpCreate(&handle));
      details::check_culib_error(
          cusolverSpSetStream(handle, (cudaStream_t)Cuda::ref_cuda_context(cupol.getProcid())
                                          .stream_spare(cupol.getStreamid())));
    }
    ~CudaLibHandle() { details::check_culib_error(cusolverSpDestroy(handle)); }
  };

  template <CudaLibraryComponentFlagBit flagbit> struct CudaLibComponentExecutionPolicy
      : CudaLibHandle<flagbit> {
    constexpr auto& handle() noexcept { return CudaLibHandle<flagbit>::handle; }

    template <typename Fn, typename... Args>
    std::enable_if_t<is_same_v<cudaLibStatus_t<flagbit>, typename function_traits<Fn>::return_t>>
    call(Fn&& fn, Args&&... args) const {
      using fts = function_traits<Fn>;
      if constexpr (sizeof...(Args) == fts::arity)
        details::check_culib_error(fn(FWD(args)...));
      else
        details::check_culib_error(fn(handle(), FWD(args)...));
    }

    CudaLibComponentExecutionPolicy(CudaExecutionPolicy& cupol) : CudaLibHandle<flagbit>{cupol} {}
  };

  template <CudaLibraryComponentFlagBit... flagbits> struct CudaLibExecutionPolicy
      : std::reference_wrapper<CudaExecutionPolicy>,
        ExecutionPolicyInterface<CudaLibExecutionPolicy<flagbits...>>,
        CudaLibComponentExecutionPolicy<flagbits>... {
    template <CudaLibraryComponentFlagBit flagbit> constexpr auto& handle() noexcept {
      return CudaLibComponentExecutionPolicy<flagbit>::handle();
    }

    template <CudaLibraryComponentFlagBit flagbit, typename Fn, typename... Args>
    constexpr std::enable_if_t<
        is_same_v<cudaLibStatus_t<flagbit>, typename function_traits<Fn>::return_t>>
    call(Fn&& fn, Args&&... args) const {
      CudaLibComponentExecutionPolicy<flagbit>::call(FWD(fn), FWD(args)...);
    }

    CudaLibExecutionPolicy(CudaExecutionPolicy& cupol)
        : std::reference_wrapper<CudaExecutionPolicy>{cupol},
          CudaLibComponentExecutionPolicy<flagbits>{cupol}... {}
  };

}  // namespace zs