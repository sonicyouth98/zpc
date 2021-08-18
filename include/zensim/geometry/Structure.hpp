#pragma once
#include <map>

#include "zensim/TypeAlias.hpp"
#include "zensim/container/TileVector.hpp"
#include "zensim/container/Vector.hpp"
#include "zensim/math/Vec.h"
#include "zensim/tpls/gcem/gcem_incl/pow.hpp"
#include "zensim/types/Polymorphism.h"

namespace zs {

  template <typename V = f32, typename I = i32, int d = 3> struct MeshObject {
    using ElemT = I[d + 1];
    using TV = V[d];
    using TM = V[d][d];
    using TMAffine = V[d + 1][d + 1];
    static constexpr int dim = d;
    Vector<V> M;
    Vector<TV> X;
    Vector<ElemT> Elems;
    Vector<TM> F;
  };

  using GeneralMesh
      = variant<MeshObject<f32, i32, 2>, MeshObject<f32, i64, 2>, MeshObject<f32, i32, 3>,
                MeshObject<f32, i64, 3>, MeshObject<f64, i32, 2>, MeshObject<f64, i64, 2>,
                MeshObject<f64, i32, 3>, MeshObject<f64, i64, 3>>;

  /// sizeof(float) = 4
  /// bin_size = 64
  /// attrib_size = 16
  template <typename V = dat32, int dim_ = 3, int channel_bits = 4, int domain_bits = 2>
  struct GridBlock {
    using value_type = V;
    using size_type = int;
    static constexpr int dim = dim_;
    using IV = vec<size_type, dim>;
    static constexpr int num_chns() noexcept { return 1 << channel_bits; }
    static constexpr int side_length() noexcept { return 1 << domain_bits; }
    static constexpr int space() noexcept { return 1 << domain_bits * dim; }

    constexpr auto &operator()(int c, IV loc) noexcept { return _data[c][offset(loc)]; }
    constexpr auto operator()(int c, IV loc) const noexcept { return _data[c][offset(loc)]; }
    constexpr auto &operator()(int c, size_type cellid) noexcept { return _data[c][cellid]; }
    constexpr auto operator()(int c, size_type cellid) const noexcept { return _data[c][cellid]; }
    static constexpr IV to_coord(size_type cellid) {
      IV ret{IV::zeros()};
      for (int d = dim - 1; d >= 0 && cellid > 0; --d, cellid >>= domain_bits)
        ret[d] = cellid % side_length();
      return ret;
    }

  protected:
    constexpr int offset(const IV &loc) const noexcept {
      // using Seq = typename gen_seq<d>::template uniform_values_t<vseq_t, (1 << domain_bits)>;
      size_type ret{0};
      for (int d = 0; d < dim; ++d) ret = (ret << domain_bits) + loc[d];
      return ret;
    }
    V _data[num_chns()][space()];
  };

  template <typename Block> struct GridBlocks;
  template <typename V, int d, int chn_bits, int domain_bits>
  struct GridBlocks<GridBlock<V, d, chn_bits, domain_bits>> {
    using value_type = V;
    using block_t = GridBlock<V, d, chn_bits, domain_bits>;
    static constexpr int dim = block_t::dim;
    using IV = typename block_t::IV;
    using size_type = typename Vector<block_t>::size_type;

    constexpr GridBlocks(float dx = 1.f, std::size_t numBlocks = 0, memsrc_e mre = memsrc_e::host,
                         ProcID devid = -1)
        : blocks{numBlocks, mre, devid}, dx{dx} {}

    void resize(size_type newSize) { blocks.resize(newSize); }
    void capacity() const noexcept { blocks.capacity(); }
    Vector<block_t> blocks;
    V dx;
  };

#if 0
  using GeneralGridBlocks
      = variant<GridBlocks<GridBlock<dat32, 2, 3, 2>>, GridBlocks<GridBlock<dat32, 3, 3, 2>>,
                GridBlocks<GridBlock<dat64, 2, 3, 2>>, GridBlocks<GridBlock<dat64, 3, 3, 2>>,
                GridBlocks<GridBlock<dat32, 2, 4, 2>>, GridBlocks<GridBlock<dat32, 3, 4, 2>>,
                GridBlocks<GridBlock<dat64, 2, 4, 2>>, GridBlocks<GridBlock<dat64, 3, 4, 2>>>;
#else
  using GeneralGridBlocks = variant<GridBlocks<GridBlock<dat32, 3, 2, 2>>>;
#endif

  template <execspace_e, typename GridBlocksT, typename = void> struct GridBlocksView;
  template <execspace_e space, typename GridBlocksT> struct GridBlocksView<space, GridBlocksT> {
    using value_type = typename GridBlocksT::value_type;
    using block_t = typename GridBlocksT::block_t;
    static constexpr int dim = block_t::dim;
    using IV = typename block_t::IV;
    using size_type = typename GridBlocksT::size_type;

    constexpr GridBlocksView() = default;
    ~GridBlocksView() = default;
    explicit constexpr GridBlocksView(GridBlocksT &gridblocks)
        : _gridBlocks{gridblocks.blocks.data()},
          _blockCount{gridblocks.blocks.size()},
          _dx{gridblocks.dx} {}

    constexpr block_t &operator[](size_type i) { return _gridBlocks[i]; }
    constexpr const block_t &operator[](size_type i) const { return _gridBlocks[i]; }

    block_t *_gridBlocks;
    size_type _blockCount;
    value_type _dx;
  };

  template <execspace_e ExecSpace, typename V, int d, int chn_bits, int domain_bits>
  constexpr decltype(auto) proxy(GridBlocks<GridBlock<V, d, chn_bits, domain_bits>> &blocks) {
    return GridBlocksView<ExecSpace, GridBlocks<GridBlock<V, d, chn_bits, domain_bits>>>{blocks};
  }

  template <typename ValueT = f32, int d_ = 3, auto SideLength = 4,
            grid_e category = grid_e::collocated>
  struct Grid {
    static_assert(d_ > 0, "dimension must be positive!");
    using value_type = ValueT;
    using allocator_type = ZSPmrAllocator<>;
    using cell_index_type = std::make_unsigned_t<decltype(SideLength)>;
    using domain_index_type = conditional_t<(sizeof(value_type) <= 4), i32, i64>;
    static constexpr int dim = d_;
    static constexpr cell_index_type side_length = SideLength;
    static constexpr cell_index_type block_space() noexcept {
      auto ret = side_length;
      for (int d = 1; d != dim; ++d) ret *= side_length;
      return ret;
    }
    /// ninja optimization
    static constexpr bool is_power_of_two
        = side_length > 0 && ((side_length & (side_length - 1)) == 0);
    static constexpr auto num_cell_bits = bit_count(side_length);

    using grid_storage_t = TileVector<value_type, (std::size_t)block_space()>;
    using size_type = typename grid_storage_t::size_type;
    using channel_counter_type = typename grid_storage_t::channel_counter_type;

    using CellIV = vec<cell_index_type, dim>;
    using IV = vec<domain_index_type, dim>;
    using TV = vec<value_type, dim>;

    constexpr MemoryLocation memoryLocation() const noexcept { return blocks.memoryLocation(); }
    constexpr memsrc_e space() const noexcept { return blocks.memspace(); }
    constexpr ProcID devid() const noexcept { return blocks.devid(); }
    constexpr auto size() const noexcept { return blocks.size(); }
    constexpr decltype(auto) allocator() const noexcept { return blocks.allocator(); }
    constexpr decltype(auto) numBlocks() const noexcept { return blocks.numTiles(); }
    constexpr channel_counter_type numChannels() const noexcept { return blocks.numChannels(); }

    Grid(const allocator_type &allocator, const std::vector<PropertyTag> &channelTags,
         value_type dx, size_type count = 0)
        : blocks{allocator, channelTags, count * block_space()}, dx{dx} {}
    Grid(const std::vector<PropertyTag> &channelTags, value_type dx, size_type count,
         memsrc_e mre = memsrc_e::host, ProcID devid = -1)
        : Grid{get_memory_source(mre, devid), channelTags, dx, count} {}
    Grid(channel_counter_type numChns, value_type dx, size_type count,
         memsrc_e mre = memsrc_e::host, ProcID devid = -1)
        : Grid{get_memory_source(mre, devid), {{"unnamed", numChns}}, dx, count} {}
    Grid(value_type dx = 1.f, memsrc_e mre = memsrc_e::host, ProcID devid = -1)
        : Grid{get_memory_source(mre, devid), {{"mass", 1}, {"vel", dim}}, dx, 0} {}

    void resize(size_type numBlocks) { blocks.resize(numBlocks * block_space()); }
    bool hasProperty(const SmallString &str) const { return blocks.hasProperty(str); }
    constexpr channel_counter_type getChannelOffset(const SmallString &str) const {
      return blocks.getChannelOffset(str);
    }

    grid_storage_t blocks;
    value_type dx;
  };
  template <typename ValueT = f32, int d_ = 3, auto SideLength = 4> struct Grids {
    template <grid_e category = grid_e::collocated> using grid_t
        = Grid<ValueT, d_, SideLength, category>;
    using collocated_grid_t = Grid<ValueT, d_, SideLength>;
    using value_type = typename collocated_grid_t::value_type;
    using allocator_type = typename collocated_grid_t::allocator_type;
    using cell_index_type = typename collocated_grid_t::cell_index_type;
    using domain_index_type = typename collocated_grid_t::domain_index_type;
    static constexpr int dim = d_;
    static constexpr cell_index_type side_length = collocated_grid_t::side_length;
    static constexpr cell_index_type block_space() noexcept {
      return collocated_grid_t::block_space();
    }
    /// ninja optimization
    static constexpr bool is_power_of_two = collocated_grid_t::is_power_of_two;
    static constexpr auto num_cell_bits = collocated_grid_t::num_cell_bits;

    using grid_storage_t = typename collocated_grid_t::grid_storage_t;
    using size_type = typename collocated_grid_t::size_type;
    using channel_counter_type = typename collocated_grid_t::channel_counter_type;

    using CellIV = typename collocated_grid_t::CellIV;
    using IV = typename collocated_grid_t::IV;
    using TV = typename collocated_grid_t::TV;

    constexpr MemoryLocation memoryLocation() const noexcept {
      return grid(collocated_v).memoryLocation();
    }
    constexpr memsrc_e space() const noexcept { return grid(collocated_v).memspace(); }
    constexpr ProcID devid() const noexcept { return grid(collocated_v).devid(); }
    constexpr auto size() const noexcept { return grid(collocated_v).size(); }
    constexpr decltype(auto) allocator() const noexcept { return grid(collocated_v).allocator(); }
    constexpr decltype(auto) numBlocks() const noexcept { return grid(collocated_v).numTiles(); }

    Grids(const allocator_type &allocator,
          const std::vector<PropertyTag> &channelTags = {{"mass", 1}, {"vel", dim}},
          value_type dx = 1.f, size_type numBlocks = 0, grid_e ge = grid_e::collocated)
        : _collocatedGrid{allocator, channelTags, dx},
          _cellcenteredGrid{allocator, channelTags, dx},
          _staggeredGrid{allocator, channelTags, dx},
          _dx{dx} {
      if (ge == grid_e::collocated)
        _collocatedGrid.resize(numBlocks);
      else if (ge == grid_e::cellcentered)
        _cellcenteredGrid.resize(numBlocks);
      else if (ge == grid_e::staggered)
        _staggeredGrid.resize(numBlocks);
    }
    Grids(const std::vector<PropertyTag> &channelTags = {{"mass", 1}, {"vel", dim}},
          value_type dx = 1.f, size_type numBlocks = 0, memsrc_e mre = memsrc_e::host,
          ProcID devid = -1, grid_e ge = grid_e::collocated)
        : Grids{get_memory_source(mre, devid), channelTags, dx, numBlocks, ge} {}

    template <grid_e category = grid_e::collocated>
    constexpr auto &grid(wrapv<category> = {}) noexcept {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid;
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid;
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid;
    }
    template <grid_e category = grid_e::collocated>
    constexpr const auto &grid(wrapv<category> = {}) const noexcept {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid;
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid;
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid;
    }
    template <grid_e category = grid_e::collocated>
    constexpr auto numCells(wrapv<category> = {}) const noexcept {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid.size() * _collocatedGrid.numChannels();
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid.size() * _cellcenteredGrid.numChannels();
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid.size() * _staggeredGrid.numChannels();
    }

    grid_t<grid_e::collocated> _collocatedGrid{};
    grid_t<grid_e::cellcentered> _cellcenteredGrid{};
    grid_t<grid_e::staggered> _staggeredGrid{};
    value_type _dx{};
  };

  using GeneralGrids = variant<Grids<f32, 3, 4>, Grids<f32, 3, 2>, Grids<f32, 3, 1>,
                               Grids<f32, 2, 4>, Grids<f32, 2, 2>, Grids<f32, 2, 1>>;

  template <execspace_e, typename GridsT, typename = void> struct GridsView;
  template <execspace_e space, typename GridsT> struct GridsView<space, GridsT> {
    static constexpr bool is_const_structure = std::is_const_v<GridsT>;
    using grids_t = std::remove_const_t<GridsT>;
    using value_type = typename grids_t::value_type;
    static constexpr int dim = grids_t::dim;
    static constexpr auto side_length = grids_t::side_length;
    static constexpr auto block_space() noexcept { return grids_t::block_space(); }
    static constexpr bool is_power_of_two = grids_t::is_power_of_two;
    static constexpr auto num_cell_bits = grids_t::num_cell_bits;

    using grid_storage_t = typename grids_t::grid_storage_t;
    using grid_view_t = RM_CVREF_T(proxy<space>(
        std::declval<
            conditional_t<is_const_structure, const grid_storage_t &, grid_storage_t &>>()));
    using grid_block_view_t = typename grid_view_t::tile_view_type;  // tilevector view property
    using size_type = typename grids_t::size_type;
    using channel_counter_type = typename grids_t::channel_counter_type;
    using cell_index_type = typename grids_t::cell_index_type;
    using domain_index_type = typename grids_t::domain_index_type;

    using CellIV = typename grids_t::CellIV;
    using IV = typename grids_t::IV;
    using TV = typename grids_t::TV;

    static constexpr auto cellid_to_coord(cell_index_type cellid) noexcept {
      CellIV ret{CellIV::zeros()};
      if constexpr (is_power_of_two) {
        for (int d = dim - 1; d >= 0 && cellid; --d, cellid >>= num_cell_bits)
          ret[d] = cellid & (side_length - 1);
      } else {
        for (int d = dim - 1; d >= 0 && cellid; --d, cellid /= side_length)
          ret[d] = cellid % side_length;
      }

      return ret;
    }
    template <typename Ti>
    static constexpr auto coord_to_cellid(const vec<Ti, dim> &coord) noexcept {
      cell_index_type ret{0};
      if constexpr (is_power_of_two)
        for (int d = 0; d != dim; ++d) ret = (ret << num_cell_bits) | coord[d];
      else
        for (int d = 0; d != dim; ++d) ret = (ret * (Ti)side_length) + coord[d];
      return ret;
    }
    template <typename Ti>
    static constexpr auto global_coord_to_cellid(const vec<Ti, dim> &coord) noexcept {
      cell_index_type ret{0};
      if constexpr (is_power_of_two)
        for (int d = 0; d != dim; ++d)
          ret = (ret << num_cell_bits) | (coord[d] & (side_length - 1));
      else
        for (int d = 0; d != dim; ++d) ret = (ret * (Ti)side_length) + (coord[d] % (Ti)side_length);
      return ret;
    }

    constexpr GridsView() = default;
    ~GridsView() = default;
    explicit GridsView(GridsT &grids)
        : _collocatedGrid{proxy<space>(grids.grid(collocated_v).blocks)},
          _cellcenteredGrid{proxy<space>(grids.grid(cellcentered_v).blocks)},
          _staggeredGrid{proxy<space>(grids.grid(staggered_v).blocks)},
          _dx{grids._dx} {}

    template <grid_e category = grid_e::collocated> struct Block {
      static constexpr auto block_space() noexcept { return GridsView::block_space(); }
      constexpr Block(grid_block_view_t tile, value_type dx) noexcept : block{tile}, dx{dx} {}

      template <typename Ti>
      constexpr auto &operator()(channel_counter_type c, const vec<Ti, dim> &loc) noexcept {
        return block(c, coord_to_cellid(loc));
      }
      template <typename Ti>
      constexpr auto operator()(channel_counter_type c, const vec<Ti, dim> &loc) const noexcept {
        return block(c, coord_to_cellid(loc));
      }
      constexpr auto &operator()(channel_counter_type chn, cell_index_type cellid) {
        return block(chn, cellid);
      }
      constexpr auto operator()(channel_counter_type chn, cell_index_type cellid) const {
        return block(chn, cellid);
      }
      template <auto N>
      constexpr auto pack(channel_counter_type chn, cell_index_type cellid) const {
        return block.template pack<N>(chn, cellid);
      }
      template <auto N, typename V, bool b = is_const_structure, enable_if_t<!b> = 0>
      constexpr void set(channel_counter_type chn, cell_index_type cellid, const vec<V, N> &val) {
        block.template tuple<N>(chn, cellid) = val;
      }

      constexpr auto size() const noexcept { return block.size(); }

      grid_block_view_t block;
      value_type dx;
    };
    template <grid_e category = grid_e::collocated> struct Grid {
      using size_type = typename GridsView::size_type;
      using value_type = typename GridsView::value_type;
      static constexpr int dim = GridsView::dim;
      static constexpr auto side_length = GridsView::side_length;
      static constexpr auto block_space() noexcept { return GridsView::block_space(); }
      static constexpr auto cellid_to_coord(cell_index_type cellid) noexcept {
        return GridsView::cellid_to_coord(cellid);
      }
      template <typename Ti>
      static constexpr auto coord_to_cellid(const vec<Ti, dim> &coord) noexcept {
        return GridsView::coord_to_cellid(coord);
      }
      template <typename Ti>
      static constexpr auto global_coord_to_cellid(const vec<Ti, dim> &coord) noexcept {
        return GridsView::global_coord_to_cellid(coord);
      }
      constexpr Grid(grid_view_t grid, value_type dx) noexcept : grid{grid}, dx{dx} {}

      constexpr auto block(size_type i) { return Block<category>{grid.tile(i), dx}; }
      constexpr auto block(size_type i) const { return Block<category>{grid.tile(i), dx}; }
      constexpr auto operator[](size_type i) { return block(i); }
      constexpr auto operator[](size_type i) const { return block(i); }
      template <typename Ti> constexpr auto &operator()(channel_counter_type c, size_type blockid,
                                                        const vec<Ti, dim> &loc) noexcept {
        return grid(c, blockid * block_space() + coord_to_cellid(loc));
      }
      template <typename Ti> constexpr auto operator()(channel_counter_type c, size_type blockid,
                                                       const vec<Ti, dim> &loc) const noexcept {
        return grid(c, blockid * block_space() + coord_to_cellid(loc));
      }
      constexpr auto &operator()(channel_counter_type chn, cell_index_type cellid) {
        return grid(chn, cellid);
      }
      constexpr auto operator()(channel_counter_type chn, cell_index_type cellid) const {
        return grid(chn, cellid);
      }
      template <auto N>
      constexpr auto pack(channel_counter_type chn, cell_index_type cellid) const {
        return grid.template pack<N>(chn, cellid);
      }
      template <auto N, typename V, bool b = is_const_structure, enable_if_t<!b> = 0>
      constexpr void set(channel_counter_type chn, cell_index_type cellid, const vec<V, N> &val) {
        grid.template tuple<N>(chn, cellid) = val;
      }

      constexpr auto size() const noexcept { return grid.size(); }
      constexpr auto numChannels() const noexcept { return grid.numChannels(); }

      grid_view_t grid;
      value_type dx;
    };

    /// grid
    template <grid_e category = grid_e::collocated, bool b = is_const_structure,
              enable_if_t<!b> = 0>
    constexpr auto &grid(wrapv<category> = {}) {
      if constexpr (category == grid_e::collocated)
        return Grid<category>{_collocatedGrid, _dx};
      else if constexpr (category == grid_e::cellcentered)
        return Grid<category>{_cellcenteredGrid, _dx};
      else if constexpr (category == grid_e::staggered)
        return Grid<category>{_staggeredGrid, _dx};
    }
    template <grid_e category = grid_e::collocated>
    constexpr const auto &grid(wrapv<category> = {}) const {
      if constexpr (category == grid_e::collocated)
        return Grid<category>{_collocatedGrid, _dx};
      else if constexpr (category == grid_e::cellcentered)
        return Grid<category>{_cellcenteredGrid, _dx};
      else if constexpr (category == grid_e::staggered)
        return Grid<category>{_staggeredGrid, _dx};
    }

    /// block
    template <grid_e category = grid_e::collocated, bool b = is_const_structure,
              enable_if_t<!b> = 0>
    constexpr auto block(size_type i) {
      if constexpr (category == grid_e::collocated)
        return Block<category>{_collocatedGrid.tile(i), _dx};
      else if constexpr (category == grid_e::cellcentered)
        return Block<category>{_cellcenteredGrid.tile(i), _dx};
      else if constexpr (category == grid_e::staggered)
        return Block<category>{_staggeredGrid.tile(i), _dx};
    }
    template <grid_e category = grid_e::collocated> constexpr auto block(size_type i) const {
      if constexpr (category == grid_e::collocated)
        return Block<category>{_collocatedGrid.tile(i), _dx};
      else if constexpr (category == grid_e::cellcentered)
        return Block<category>{_cellcenteredGrid.tile(i), _dx};
      else if constexpr (category == grid_e::staggered)
        return Block<category>{_staggeredGrid.tile(i), _dx};
    }
    template <grid_e category = grid_e::collocated, bool b = is_const_structure,
              enable_if_t<!b> = 0>
    constexpr auto operator[](size_type i) {
      return block(i);
    }
    template <grid_e category = grid_e::collocated> constexpr auto operator[](size_type i) const {
      return block(i);
    }

    /// cell
    template <grid_e category = grid_e::collocated, bool b = is_const_structure,
              enable_if_t<!b> = 0>
    constexpr value_type &cell(channel_counter_type chn, size_type bid, cell_index_type cid) {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid(chn, bid * block_space() + cid);
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid(chn, bid * block_space() + cid);
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid(chn, bid * block_space() + cid);
    }
    template <grid_e category = grid_e::collocated>
    constexpr const value_type &cell(channel_counter_type chn, size_type bid,
                                     cell_index_type cid) const {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid(chn, bid * block_space() + cid);
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid(chn, bid * block_space() + cid);
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid(chn, bid * block_space() + cid);
    }
    template <grid_e category = grid_e::collocated, bool b = is_const_structure,
              enable_if_t<!b> = 0>
    constexpr value_type &operator()(channel_counter_type chn, size_type dof) {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid(chn, dof);
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid(chn, dof);
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid(chn, dof);
    }
    template <grid_e category = grid_e::collocated>
    constexpr const value_type &operator()(channel_counter_type chn, size_type dof) const {
      if constexpr (category == grid_e::collocated)
        return _collocatedGrid(chn, dof);
      else if constexpr (category == grid_e::cellcentered)
        return _cellcenteredGrid(chn, dof);
      else if constexpr (category == grid_e::staggered)
        return _staggeredGrid(chn, dof);
    }

    grid_view_t _collocatedGrid;
    grid_view_t _cellcenteredGrid;
    grid_view_t _staggeredGrid;
    value_type _dx;
  };

  template <execspace_e space, typename V, int d, auto SideLength>
  constexpr decltype(auto) proxy(Grids<V, d, SideLength> &grids) {
    return GridsView<space, Grids<V, d, SideLength>>{grids};
  }
  template <execspace_e space, typename V, int d, auto SideLength>
  constexpr decltype(auto) proxy(const Grids<V, d, SideLength> &grids) {
    return GridsView<space, const Grids<V, d, SideLength>>{grids};
  }

  template <execspace_e space, typename V, int d, auto SideLength, grid_e category>
  constexpr decltype(auto) proxy(Grid<V, d, SideLength, category> &grid) {
    return typename GridsView<space, Grids<V, d, SideLength>>::template Grid<category>{
        proxy<space>(grid.blocks), grid.dx};
  }
  template <execspace_e space, typename V, int d, auto SideLength, grid_e category>
  constexpr decltype(auto) proxy(const Grid<V, d, SideLength, category> &grid) {
    return typename GridsView<space, const Grids<V, d, SideLength>>::template Grid<category>{
        proxy<space>(grid.blocks), grid.dx};
  }

}  // namespace zs