#pragma once
#include "zensim/container/Vector.hpp"
#include "zensim/memory/MemoryResource.h"
#include "zensim/resource/Resource.h"

namespace zs {

  enum struct matrix_order_e : char { rowMajor = 0, colMajor };

  /// matrix base
  template <typename ValueType = float, typename IndexType = int> struct MatrixBase : MemoryHandle {
    using value_type = ValueType;
    using index_type = IndexType;

    constexpr index_type rows() const noexcept { return nrows; }
    constexpr index_type cols() const noexcept { return ncols; }
    constexpr index_type size() const noexcept { return rows() * cols(); }
    constexpr bool isVector() const noexcept { return rows() == 1 || cols() == 1; }
    constexpr bool isRowMajor() const noexcept { return order == matrix_order_e::rowMajor; }

    constexpr index_type outerSize() const noexcept {
      return isVector() ? 1 : isRowMajor() ? rows() : cols();
    }
    constexpr index_type innerSize() const noexcept {
      return isVector() ? size() : isRowMajor() ? cols() : rows();
    }

    index_type nrows{1}, ncols{1};
    matrix_order_e order{matrix_order_e::rowMajor};
  };

  /// matrix access
  template <typename Derived> struct MatrixAccessor {
    // using value_type = typename Derived::value_type;
    // using index_type = typename Derived::index_type;
    template <typename Ti> constexpr decltype(auto) coeff(Ti r, Ti c) {
      return self()->coeff(r, c);
    }
    template <typename Ti> constexpr decltype(auto) coeff(Ti r, Ti c) const {
      return self()->coeff(r, c);
    }

  protected:
    constexpr auto &self() noexcept { return static_cast<Derived &>(*this); }
    constexpr const auto &self() const noexcept { return static_cast<const Derived &>(*this); }
  };

  /// matrix

  /// matrix
  template <typename ValueType = float, typename IndexType = int> struct IdentityMatrix
      : MatrixBase<ValueType, IndexType>,
        MatrixAccessor<IdentityMatrix<ValueType, IndexType>> {
    using base_t = MatrixBase<ValueType, IndexType>;
    using value_type = ValueType;
    using index_type = IndexType;
    constexpr value_type coeff(index_type r, index_type c) const { return r == c ? identity : 0; }
    value_type identity{1};
  };
  template <typename ValueType = float, typename IndexType = int> struct YaleMatrix
      : MatrixBase<ValueType, IndexType>,
        MatrixAccessor<YaleMatrix<ValueType, IndexType>> {
    using base_t = MatrixBase<ValueType, IndexType>;
    using value_type = ValueType;
    using index_type = IndexType;
    constexpr value_type &coeff(index_type r, index_type c) {
      index_type i = c;
      index_type j = r;
      if (base_t::isRowMajor()) {
        i = r;
        j = c;
      }
      for (index_type st = offsets[i], ed = offsets[i + 1]; st < ed; ++st)
        if (indices[st] == j) return vals[st];
    }
    constexpr const value_type &coeff(index_type r, index_type c) const {
      index_type i = c;
      index_type j = r;
      if (base_t::isRowMajor()) {
        i = r;
        j = c;
      }
      for (index_type st = offsets[i], ed = offsets[i + 1]; st < ed; ++st)
        if (indices[st] == j) return vals[st];
    }
    zs::Vector<index_type> offsets{memsrc_e::host}, indices{memsrc_e::host};
    zs::Vector<value_type> vals{memsrc_e::host};
  };
  template <typename ValueType = float, typename IndexType = int> struct CooSparseMatrix
      : MatrixBase<ValueType, IndexType>,
        MatrixAccessor<CooSparseMatrix<ValueType, IndexType>> {
    using base_t = MatrixBase<ValueType, IndexType>;
    using value_type = ValueType;
    using index_type = IndexType;
    Vector<index_type> rowInds{memsrc_e::host}, colInds{memsrc_e::host};
    Vector<value_type> vals{memsrc_e::host};
  };
  template <typename ValueType = float, typename IndexType = int> struct DenseMatrix
      : MatrixBase<ValueType, IndexType>,
        MatrixAccessor<DenseMatrix<ValueType, IndexType>> {
    using base_t = MatrixBase<ValueType, IndexType>;
    using value_type = ValueType;
    using index_type = IndexType;
    Vector<value_type> vals{base_t::size(), memsrc_e::host};
  };

}  // namespace zs