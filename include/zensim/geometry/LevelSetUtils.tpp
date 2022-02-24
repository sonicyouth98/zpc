#include <stdexcept>

#include "LevelSetUtils.hpp"

namespace zs {

  /// max velocity
  template <typename ExecPol, int dim, grid_e category>
  auto get_level_set_max_speed(ExecPol &&pol, const SparseLevelSet<dim, category> &ls) ->
      typename SparseLevelSet<dim, category>::value_type {
    constexpr execspace_e space = RM_CVREF_T(pol)::exec_tag::value;

    Vector<typename SparseLevelSet<dim, category>::value_type> vel{ls.get_allocator(), 1};
    vel.setVal(0);
    if (ls.hasProperty("vel")) {
      auto nbs = ls.numBlocks();
      pol(std::initializer_list<sint_t>{(sint_t)nbs, (sint_t)ls.block_size},
          [ls = proxy<space>(ls), vel = vel.data()] ZS_LAMBDA(
              typename RM_CVREF_T(ls)::size_type bi,
              typename RM_CVREF_T(ls)::cell_index_type ci) mutable {
            using ls_t = RM_CVREF_T(ls);
            auto coord = ls._table._activeKeys[bi] + ls_t::grid_view_t::cellid_to_coord(ci);
            typename ls_t::TV vi{};
            if constexpr (ls_t::category == grid_e::staggered)
              vi = ls.ipack("vel", ls.cellToIndex(coord), 0);
            else
              vi = ls.wpack<3>("vel", ls.indexToWorld(coord), 0);
            vi = vi.abs();
            auto vm = vi[0];
            if (vi[1] > vm) vm = vi[1];
            if (vi[2] > vm) vm = vi[2];
            atomic_max(wrapv<space>{}, vel, vm);
          });
    }
    return vel.getVal();
  }

  /// mark
  template <typename ExecPol, int dim, grid_e category>
  void mark_level_set(ExecPol &&pol, SparseLevelSet<dim, category> &ls,
                      typename SparseLevelSet<dim, category>::value_type threshold
                      = limits<typename SparseLevelSet<dim, category>::value_type>::epsilon()
                        * 128) {
    auto nbs = ls.numBlocks();
    constexpr execspace_e space = RM_CVREF_T(pol)::exec_tag::value;

    ls.append_channels(pol, {{"mark", 1}});

    Vector<u64> numActiveVoxels{ls.get_allocator(), 1};
    numActiveVoxels.setVal(0);

    pol(std::initializer_list<sint_t>{(sint_t)nbs, (sint_t)ls.block_size},
        [ls = proxy<space>(ls), threshold, cnt = numActiveVoxels.data()] ZS_LAMBDA(
            typename RM_CVREF_T(ls)::size_type bi,
            typename RM_CVREF_T(ls)::cell_index_type ci) mutable {
          using ls_t = RM_CVREF_T(ls);
          bool done = false;
          if constexpr (ls_t::category == grid_e::staggered) {
            ls._grid("mark", bi, ci) = 0;
            for (typename ls_t::channel_counter_type propNo = 0; propNo != ls.numProperties();
                 ++propNo) {
              if (ls.getPropertyNames()[propNo] == "mark") continue;  // skip property ["mark"]
              bool isSdf = ls.getPropertyNames()[propNo] == "sdf";
              auto propOffset = ls.getPropertyOffsets()[propNo];
              auto propSize = ls.getPropertySizes()[propNo];
              auto coord = ls._table._activeKeys[bi] + ls_t::grid_view_t::cellid_to_coord(ci);
              // usually this is
              for (typename ls_t::channel_counter_type chn = 0; chn != propSize; ++chn) {
                if ((!isSdf
                     && (zs::abs(ls.value_or(propOffset + chn, coord, chn % 3, 0)) > threshold
                         || zs::abs(ls.value_or(propOffset + chn, coord, chn % 3 + 3, 0))
                                > threshold))
                    || (isSdf
                        && (ls.value_or(propOffset + chn, coord, chn % 3, 0) < -threshold
                            || ls.value_or(propOffset + chn, coord, chn % 3 + 3, 0)
                                   < -threshold))) {
                  ls._grid("mark", bi, ci) = 1;
                  atomic_add(wrapv<space>{}, cnt, (u64)1);
                  done = true;
                  break;  // no need further checking
                }
              }
              if (done) break;
            }
          } else {
            auto block = ls._grid.block(bi);
            block("mark", ci) = 0;
            // const auto nchns = ls.numChannels();
            for (typename ls_t::channel_counter_type propNo = 0; propNo != ls.numProperties();
                 ++propNo) {
              if (ls.getPropertyNames()[propNo] == "mark") continue;  // skip property ["mark"]
              bool isSdf = ls.getPropertyNames()[propNo] == "sdf";
              auto propOffset = ls.getPropertyOffsets()[propNo];
              auto propSize = ls.getPropertySizes()[propNo];
              for (typename ls_t::channel_counter_type chn = 0; chn != propSize; ++chn)
                if ((zs::abs(block(propOffset + chn, ci)) > threshold && !isSdf)
                    || (block(propOffset + chn, ci) < -threshold && isSdf)) {
                  block("mark", ci) = 1;
                  atomic_add(wrapv<space>{}, cnt, (u64)1);
#if 0
                    printf("b[%d]c[%d] prop[%s] chn<%d, %d (%d)> val[%f]\n", (int)bi, (int)ci,
                           ls.getPropertyNames()[propNo].asChars(), (int)propOffset, (int)chn,
                           (int)propSize, (float)block(propOffset + chn, ci));
#endif
                  done = true;
                  break;  // no need further checking
                }
              if (done) break;
            }
          }
        });

    fmt::print("{} voxels ot of {} in total are active. (threshold: {})\n",
               numActiveVoxels.getVal(), ls.numBlocks() * (std::size_t)ls.block_size, threshold);
  }

  /// shrink
  template <typename ExecPol, int dim, grid_e category> void refit_level_set_domain(
      ExecPol &&pol, SparseLevelSet<dim, category> &ls,
      typename SparseLevelSet<dim, category>::value_type threshold
      = zs::limits<typename SparseLevelSet<dim, category>::value_type>::epsilon() * 128) {
    using SplsT = SparseLevelSet<dim, category>;

    constexpr execspace_e space = RM_CVREF_T(pol)::exec_tag::value;
    std::size_t nbs = ls.numBlocks();
    const auto &allocator = ls.get_allocator();

#if 1
    {
      Vector<float> test{ls.get_allocator(), 1};
      test.setVal(0);
      pol(range(nbs), [grid = proxy<space>(ls._grid), test = test.data()] ZS_LAMBDA(
                          typename RM_CVREF_T(ls)::size_type bi) mutable {
        using grid_t = RM_CVREF_T(grid);
        const auto block = grid.block(bi);
        if (!block.hasProperty("sdf")) return;
        for (int ci = 0; ci != grid.block_size; ++ci)
          if (block("sdf", ci) < 0) atomic_add(wrapv<space>{}, test, block("sdf", ci));
      });
      fmt::print("before refit, {} blocks sdf sum: {}\n", nbs, test.getVal());
    }
#endif

    // mark
    Vector<typename SplsT::size_type> marks{allocator, nbs + 1};
    pol(range(nbs), [marks = proxy<space>(marks)] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) {
      marks[bi] = 0;
    });
    pol(std::initializer_list<sint_t>{(sint_t)nbs, (sint_t)ls.block_size},
        [ls = proxy<space>(ls), marks = proxy<space>(marks)] ZS_LAMBDA(
            typename RM_CVREF_T(ls)::size_type bi,
            typename RM_CVREF_T(ls)::cell_index_type ci) mutable {
          if ((int)ls._grid("mark", bi, ci) != 0) marks[bi] = 1;
        });

    // establish mapping
    Vector<typename SplsT::size_type> offsets{allocator, nbs + 1};
    exclusive_scan(pol, std::begin(marks), std::end(marks), std::begin(offsets));
    std::size_t newNbs = offsets.getVal(nbs);  // retrieve latest numblocks
    fmt::print(fg(fmt::color::blue_violet), "shrink [{}] blocks to [{}] blocks\n", nbs, newNbs);
    Vector<typename SplsT::size_type> preservedBlockNos{allocator, newNbs};
    pol(range(nbs),
        [marks = proxy<space>(marks), offsets = proxy<space>(offsets),
         blocknos = proxy<space>(
             preservedBlockNos)] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) mutable {
          if (marks[bi] != 0) blocknos[offsets[bi]] = bi;
          // printf("mapping [%d]-th to offset [%d]\n", (int)bi, (int)offsets[bi]);
        });

    using TableT = typename SplsT::table_t;
    using GridT = typename SplsT::grid_t;

    auto prevKeys = ls._table._activeKeys.clone(allocator);
    auto prevGrid = ls._grid.clone(allocator);
    // ls._table.resize(pol, newNbs);
    // ls._grid.resize(newNbs);
    // TableT newTable{allocator, newNbs};
    // GridT newGrid{allocator, ls._grid.getPropertyTags(), ls._grid.dx, newNbs};

#if 0
    {
      Vector<float> test{ls.get_allocator(), 1};
      test.setVal(0);
      pol(range(nbs), [grid = proxy<space>(ls), test = test.data()] ZS_LAMBDA(
                          typename RM_CVREF_T(ls)::size_type bi) mutable {
        using grid_t = RM_CVREF_T(grid);
        const auto block = grid.block(bi);
        if (!block.hasProperty("sdf")) return;
        for (int ci = 0; ci != grid.block_size; ++ci)
          if (block("sdf", ci) < 0) atomic_add(wrapv<space>{}, test, block("sdf", ci));
      });
      fmt::print("after map, {} blocks sdf sum: {}\n", nbs, test.getVal());
    }
#endif

#if 0
    // shrink table
    pol(range(ls._table._tableSize),
        zs::ResetHashTable{proxy<space>(ls._table)});  // cnt not yet cleared
    pol(range(newNbs), [blocknos = proxy<space>(preservedBlockNos),
                        blockids = proxy<space>(prevKeys), newTable = proxy<space>(ls._table),
                        newNbs] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) mutable {
      auto blockid = blockids[blocknos[bi]];
      newTable.insert(blockid, bi);
      newTable._activeKeys[bi] = blockid;
      if (bi == 0) *newTable._cnt = newNbs;
    });
// shrink grid
#  if 0
    {
      Vector<float> test{ls.get_allocator(), 1};
      test.setVal(0);
      pol(range(nbs), [grid = proxy<space>(ls), test = test.data()] ZS_LAMBDA(
                          typename RM_CVREF_T(ls)::size_type bi) mutable {
        using grid_t = RM_CVREF_T(grid);
        const auto block = grid.block(bi);
        if (!block.hasProperty("sdf")) return;
        for (int ci = 0; ci != grid.block_size; ++ci)
          if (block("sdf", ci) < 0) atomic_add(wrapv<space>{}, test, block("sdf", ci));
      });
      fmt::print("before grid built, {} blocks sdf sum: {}\n", nbs, test.getVal());
    }
#  endif
    Vector<float> test{ls.get_allocator(), 1};
    test.setVal(0);
    pol(std::initializer_list<sint_t>{(sint_t)newNbs, (sint_t)ls.block_size},
        [blocknos = proxy<space>(preservedBlockNos), grid = proxy<space>(prevGrid),
         newGrid = proxy<space>(ls), marks = proxy<space>(marks), offsets = proxy<space>(offsets),
         ls = proxy<space>(ls),
         test = test.data()] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi,
                                       typename RM_CVREF_T(ls)::cell_index_type ci) mutable {
#  if 1
          if (ci == 0) {
            auto blockid = ls._table._activeKeys[bi];
            printf("## new[%d] <- prev[%d]; mark{%d}, offset{%d}, coord{%d, %d, %d}\n", (int)bi,
                   (int)blocknos[bi], (int)marks[blocknos[bi]], (int)offsets[blocknos[bi]],
                   (int)blockid[0], (int)blockid[1], (int)blockid[2]);
          }
#  endif
          using grid_t = RM_CVREF_T(grid);
          const auto block = grid.block(blocknos[bi]);
          auto newBlock = newGrid.block(bi);
          for (typename grid_t::channel_counter_type chn = 0; chn != newGrid.numChannels(); ++chn)
            newBlock(chn, ci) = block(chn, ci);

          if (newGrid.hasProperty("sdf"))
            if (newBlock("sdf", ci) < 0) atomic_add(wrapv<space>{}, test, newBlock("sdf", ci));
        });
#  if 0
    {
      Vector<float> test{ls.get_allocator(), 1};
      test.setVal(0);
      pol(range(nbs), [grid = proxy<space>(ls), test = test.data()] ZS_LAMBDA(
                          typename RM_CVREF_T(ls)::size_type bi) mutable {
        using grid_t = RM_CVREF_T(grid);
        const auto block = grid.block(bi);
        if (!block.hasProperty("sdf")) return;
        for (int ci = 0; ci != grid.block_size; ++ci)
          if (block("sdf", ci) < 0) atomic_add(wrapv<space>{}, test, block("sdf", ci));
      });
      fmt::print("after grid built, {} blocks sdf sum: {}\n", nbs, test.getVal());
    }
#  endif
#  if 1
    auto newSum = test.getVal();
    test.setVal(0);
    pol(range(nbs), [grid = proxy<space>(prevGrid), marks = proxy<space>(marks),
                     offsets = proxy<space>(offsets),
                     test = test.data()] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) mutable {
      if (marks[bi] == 1)
        printf("@@ [%d] mark{%d}, offset{%d}\n", (int)bi, (int)marks[bi], (int)offsets[bi]);
      using grid_t = RM_CVREF_T(grid);
      const auto block = grid.block(bi);
      if (!block.hasProperty("sdf")) return;
      for (int ci = 0; ci != grid.block_size; ++ci)
        if (block("sdf", ci) < -1e-6) atomic_add(wrapv<space>{}, test, block("sdf", ci));
    });
    auto totalSum = test.getVal();
    test.setVal(0);
    pol(range(nbs), [grid = proxy<space>(prevGrid), marks = proxy<space>(marks),
                     offsets = proxy<space>(offsets),
                     test = test.data()] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) mutable {
      if (marks[bi] == 0) return;
      using grid_t = RM_CVREF_T(grid);
      const auto block = grid.block(bi);
      if (!block.hasProperty("sdf")) return;
      for (int ci = 0; ci != grid.block_size; ++ci)
        if (block("sdf", ci) < -1e-6) atomic_add(wrapv<space>{}, test, block("sdf", ci));
    });
    fmt::print("\tsummed sdf [old] {} (marked {}); [new] {}\n", totalSum, test.getVal(), newSum);
    getchar();
#  endif
#endif

#if 0
    ls._table = newTable;
    ls._grid = newGrid;
#endif
  }

  /// usually shrink before extend
  template <typename ExecPol, int dim, grid_e category>
  void extend_level_set_domain(ExecPol &&pol, SparseLevelSet<dim, category> &ls, int nlayers) {
    constexpr execspace_e space = RM_CVREF_T(pol)::exec_tag::value;

    while (nlayers--) {
      auto nbs = ls.numBlocks();
      if (nbs * 26 >= ls.numReservedBlocks()) {
        ls.resize(pol, nbs * 26);  // at most 26 neighbor blocks spawned
        fmt::print("resizing to {} blocks\n", nbs * 26);
      }
      pol(range(nbs),
          [ls = proxy<space>(ls)] ZS_LAMBDA(typename RM_CVREF_T(ls)::size_type bi) mutable {
            using ls_t = RM_CVREF_T(ls);
            using table_t = RM_CVREF_T(ls._table);
            auto coord = ls._table._activeKeys[bi];
            for (auto loc : ndrange<3>(3)) {
              auto offset = (make_vec<int>(loc) - 1) * ls_t::side_length;
              using TV = RM_CVREF_T(offset);
              if (offset == TV::zeros()) return;
              if (auto blockno = ls._table.insert(coord + offset);
                  blockno != table_t::sentinel_v) {  // initialize newly inserted block
                auto bid = coord + offset;
#if 0
                printf("[%d]-th block {%d, %d, %d} inserting [%d]-th block {%d, %d, %d}\n", (int)bi,
                       (int)coord[0], (int)coord[1], (int)coord[2], (int)blockno, (int)bid[0],
                       (int)bid[1], (int)bid[2]);
#endif
                auto block = ls._grid.block(blockno);
                for (typename ls_t::channel_counter_type chn = 0; chn != ls.numChannels(); ++chn)
                  for (typename ls_t::cell_index_type ci = 0; ci != ls.block_size; ++ci)
                    block(chn, ci) = 0;  // ls._backgroundValue;
              }
            }
          });
    }
  }

  template <typename ExecPol, int dim, grid_e category>
  void flood_fill_levelset(ExecPol &&policy, SparseLevelSet<dim, category> &ls) {
    constexpr execspace_e space = RM_CVREF_T(policy)::exec_tag::value;
    using SpLs = SparseLevelSet<dim, category>;

    auto &grid = ls._grid;
    auto &table = ls._table;
    auto &blocks = table._activeKeys;

    if (!ls.hasProperty("mask")) throw std::runtime_error("missing mask info in the levelset!");
    std::vector<PropertyTag> tags{};
    if (!ls.hasProperty("tag")) tags.push_back({"tag", 1});
    if (!ls.hasProperty("tagmask")) tags.push_back({"tagmask", 1});
    if (tags.size()) {
      ls.append_channels(policy, tags);
      policy(range(grid.size()),
             InitFloodFillGridChannels<RM_CVREF_T(proxy<space>(grid))>{proxy<space>(grid)});
      fmt::print("tagmask at chn {}, tag at chn {}\n", grid.getChannelOffset("tagmask"),
                 grid.getChannelOffset("tag"));
    }
    fmt::print("sdf at chn {}, mask at chn {}\n", grid.getChannelOffset("sdf"),
               grid.getChannelOffset("mask"));

    fmt::print(
        "block capacity: {}, table block count: {}, cell count: {} ({}), tag chn offset: {}\n",
        blocks.size(), table.size(), blocks.size() * grid.block_space(), grid.size(),
        grid.getChannelOffset("tag"));

    std::size_t tableSize = table.size();
    int iter = 0;
    grid.resize(tableSize * 2);

    Vector<typename SpLs::size_type> ks{grid.get_allocator(), grid.size()};
    do {
      Vector<int> tmp{1, memsrc_e::host, -1};
      tmp[0] = 0;
      auto seedcnt = tmp.clone(grid.get_allocator());

      policy(range(tableSize * (std::size_t)grid.block_space()),
             ReserveForNeighbor<space, RM_CVREF_T(ls)>{ls});
#if 0
      {
        auto lsv = proxy<space>(ls);
        lsv.print();
      }
      puts("done expansion");
      getchar();
#endif

      ks.resize(tableSize * (std::size_t)grid.block_space());
      policy(range(tableSize * (std::size_t)grid.block_space()),
             MarkInteriorTag<space, RM_CVREF_T(ls), RM_CVREF_T(ks)>{ls, ks, seedcnt.data()});
      tmp = seedcnt.clone({memsrc_e::host, -1});
      fmt::print("floodfill iter [{}]: {} -> {}, {} seeds\n", iter, tableSize, table.size(),
                 tmp[0]);
      if (tmp[0] == 0) break;
#if 0
      {
        auto lsv = proxy<space>(ls);
        lsv.print();
      }
      puts("done tagging");
      getchar();
#endif

      tableSize = table.size();
      grid.resize(tableSize * 2);

      policy(range(tmp[0]), ComputeTaggedSDF<space, RM_CVREF_T(ls), RM_CVREF_T(ks)>{ls, ks});

#if 0
      {
        auto lsv = proxy<space>(ls);
        lsv.print();
      }
      puts("done sdf compute");
      getchar();
#endif
      iter++;
    } while (true);

    fmt::print("floodfill finished at iter [{}] with {} blocks\n", iter, table.size());
    return;
  }

}  // namespace zs