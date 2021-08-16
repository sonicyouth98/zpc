#pragma once
#include "zensim/container/HashTable.hpp"
#include "zensim/geometry/Structure.hpp"
#include "zensim/geometry/Structurefree.hpp"
#include "zensim/math/linear/LinearOperators.hpp"
#include "zensim/simulation/mpm/Simulator.hpp"
#include "zensim/simulation/transfer/G2P2G.hpp"

namespace zs {

  struct ImplicitMPMSystem {
    ImplicitMPMSystem(MPMSimulator& simulator, float dt, std::size_t partI)
        : simulator{simulator}, partI{partI}, dt{dt} {}

    template <typename DofA, typename DofB, typename DofC, typename DofD> struct MulDtSqrDivMass {
      using Index = typename DofA::size_type;
      MulDtSqrDivMass(DofA a, DofB b, DofC c, DofD d, float dt) : f{a}, m{b}, c{c}, x0{d}, dt{dt} {}

      constexpr void operator()(Index node) {
        if (m.get(node) != 0)
          c.set(node, f.get(node, vector_v) * dt / m.get(node) * dt + x0.get(node, vector_v));
      }
      DofA f;
      DofB m;
      DofC c;
      DofD x0;
      float dt;
    };
    template <class ExecutionPolicy, typename In, typename Out>
    void multiply(ExecutionPolicy&& policy, In&& in, Out&& out) {
      constexpr execspace_e space = RM_CVREF_T(policy)::exec_tag::value;
      constexpr auto execTag = wrapv<space>{};
      auto mh = simulator.memDsts[partI];
      for (auto&& [modelId, objId] : simulator.groups[partI]) {
        auto& [model, objId_] = simulator.models[modelId];
        assert_with_msg(objId_ == objId, "[MPMSimulator] model-object id conflicts, error build");
        if (objId_ != objId) throw std::runtime_error("WTF???");
        match(
            [&, this, did = mh.devid()](auto& constitutiveModel, auto& partition, auto& obj,
                                        auto& grids)
                -> std::enable_if_t<remove_cvref_t<decltype(obj)>::dim
                                        == remove_cvref_t<decltype(partition)>::dim
                                    && remove_cvref_t<decltype(obj)>::dim == RM_CVREF_T(in)::dim> {
              // compute f_i (out)
              policy({obj.size()}, G2P2GTransfer{execTag, wrapv<transfer_scheme_e::apic>{}, dt,
                                                 constitutiveModel, in, out, partition, obj});
              // update v_i
              auto gridm = dof_view<space, 1>(grids.grid(), 0);
              policy({in.size()}, MulDtSqrDivMass{out, gridm, out, in, dt});
            },
            [](...) {})(model, simulator.partitions[partI], simulator.particles[objId],
                        simulator.grids[partI]);
      }
      // DofCompwiseUnaryOp{std::negate<void>{}}(policy, FWD(in), FWD(out));
      // policy(range(out.size()), DofAssign{FWD(in), FWD(out)});
    }

    template <typename ColliderView, typename TableView, typename GridDofView> struct Projector {
      using grids_t = typename GridDofView::structure_view_t;
      using dof_index_t = typename grids_t::size_type;
      using value_type = typename grids_t::value_type;

      Projector(ColliderView col, TableView part, GridDofView grids)
          : collider{col}, partition{part}, griddof{grids} {}

      constexpr void operator()(dof_index_t dofi) {
        auto blockid = dofi / grids_t::block_space();
        auto cellid = dofi % grids_t::block_space();
        auto blockkey = partition._activeKeys[blockid];
        auto block = griddof.getStructure().block(blockid);

        if (block(0, cellid) > 0) {
          auto vel = block.pack<GridDofView::dim>(1, cellid);
          auto pos
              = (blockkey * (value_type)grids_t::side_length + grids_t::cellid_to_coord(cellid))
                * griddof.getStructure().dx;

          collider.resolveCollision(pos, vel);

          block.set(1, cellid, vel);
        }
      }

      ColliderView collider;
      TableView partition;
      GridDofView griddof;
    };

    template <class ExecutionPolicy, typename InOut>
    void project(ExecutionPolicy&& policy, InOut&& inout) {
      constexpr execspace_e space = RM_CVREF_T(policy)::exec_tag::value;
      auto mh = simulator.memDsts[partI];
      assert_with_msg(mh.devid() >= 0, "[MPMSimulator] should not put data on host");
      for (auto& boundary : simulator.boundaries) {
        match(
            [&, did = mh.devid()](auto& collider, auto& partition)
                -> std::enable_if_t<remove_cvref_t<decltype(collider)>::dim
                                        == remove_cvref_t<decltype(partition)>::dim
                                    && remove_cvref_t<decltype(collider)>::dim
                                           == RM_CVREF_T(inout)::dim> {
              using Grid = remove_cvref_t<decltype(inout.getStructure())>;
              fmt::print("[gpu {}]\tprojecting {} grid blocks\n", (int)did, partition.size());
              if constexpr (is_levelset_boundary<RM_CVREF_T(collider)>::value)
                policy({(std::size_t)inout.size()},
                       Projector{Collider{proxy<space>(collider.levelset), collider.type},
                                 proxy<space>(partition), inout});
              else {
                policy({(std::size_t)inout.size()},
                       Projector{collider, proxy<space>(partition), inout});
              }
            },
            [](...) {})(boundary, simulator.partitions[partI]);
      }
    }

    template <typename DofA, typename DofB, typename DofC> struct DivPernodeMass {
      using Index = typename DofA::size_type;
      DivPernodeMass(DofA a, DofB b, DofC c) : a{a}, b{b}, c{c} {}

      constexpr void operator()(Index node) {
        if (b.get(node) != 0) c.set(node, a.get(node, vector_v) / b.get(node));
      }
      DofA a;
      DofB b;
      DofC c;
    };

    template <class ExecutionPolicy, typename In, typename Out>
    void precondition(ExecutionPolicy&& policy, In&& in, Out&& out) {
      constexpr execspace_e space = RM_CVREF_T(policy)::exec_tag::value;
      auto mh = simulator.memDsts[partI];
      assert_with_msg(mh.devid() >= 0, "[MPMSimulator] should not put data on host");
      match(
          [&, did = mh.devid()](auto& partition, auto& grids)
              -> std::enable_if_t<remove_cvref_t<decltype(partition)>::dim == RM_CVREF_T(in)::dim> {
            fmt::print("[gpu {}]\tprojecting {} grid blocks\n", (int)did, partition.size());
            auto gridm = dof_view<space, 1>(grids.grid(), 0);
            policy({in.size()}, DivPernodeMass{in, gridm, out});
          },
          [](...) {})(simulator.partitions[partI], simulator.grids[partI]);
      // policy(range(out.size()), DofAssign{FWD(in), FWD(out)});
    }

    MPMSimulator& simulator;
    std::size_t partI;
    float dt;
  };

}  // namespace zs