#pragma once
#include "Scheme.hpp"
#include "zensim/container/HashTable.hpp"
#include "zensim/container/IndexBuckets.hpp"
#include "zensim/container/Vector.hpp"
#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/geometry/Structure.hpp"
#include "zensim/geometry/Structurefree.hpp"
#include "zensim/math/matrix/MatrixUtils.h"
#include "zensim/physics/ConstitutiveModel.hpp"

namespace zs {

  template <transfer_scheme_e, typename ConstitutiveModel, typename BucketsT, typename GridsT,
            typename TableT, typename ParticlesT>
  struct G2C2PTransfer;

  template <execspace_e space, transfer_scheme_e scheme, typename Model, typename BucketsT,
            typename T, int d, auto l, typename TableT, typename ParticlesT>
  G2C2PTransfer(wrapv<space>, wrapv<scheme>, float, Model, BucketsT, Grids<T, d, l>, TableT,
                ParticlesT)
      -> G2C2PTransfer<scheme, Model, IndexBucketsView<space, BucketsT>,
                       GridsView<space, Grids<T, d, l>>, HashTableView<space, TableT>,
                       ParticlesView<space, ParticlesT>>;

  template <typename ParticlesT> struct PreG2C2PTransfer;
  template <transfer_scheme_e, typename ConstitutiveModel, typename ParticlesT>
  struct PostG2C2PTransfer;

  template <execspace_e space, typename ParticlesT> PreG2C2PTransfer(wrapv<space>, ParticlesT)
      -> PreG2C2PTransfer<ParticlesView<space, ParticlesT>>;
  template <execspace_e space, transfer_scheme_e scheme, typename Model, typename ParticlesT>
  PostG2C2PTransfer(wrapv<space>, wrapv<scheme>, float, Model, ParticlesT)
      -> PostG2C2PTransfer<scheme, Model, ParticlesView<space, ParticlesT>>;

  template <execspace_e space, transfer_scheme_e scheme, typename ModelT, typename BucketsT,
            typename GridsT, typename TableT, typename ParticlesT>
  struct G2C2PTransfer<scheme, ModelT, IndexBucketsView<space, BucketsT>, GridsView<space, GridsT>,
                       HashTableView<space, TableT>, ParticlesView<space, ParticlesT>> {
    using model_t = ModelT;  ///< constitutive model
    using buckets_t = IndexBucketsView<space, BucketsT>;
    using grids_t = GridsView<space, GridsT>;
    using value_type = typename grids_t::value_type;
    using partition_t = HashTableView<space, TableT>;
    using particles_t = ParticlesView<space, ParticlesT>;
    static_assert(particles_t::dim == partition_t::dim && particles_t::dim == grids_t::dim,
                  "[particle-partition-grid] dimension mismatch");

    explicit G2C2PTransfer(wrapv<space>, wrapv<scheme>, float dt, const ModelT& model,
                           BucketsT& buckets, GridsT& grids, TableT& table, ParticlesT& particles)
        : model{model},
          buckets{proxy<space>(buckets)},
          grids{proxy<space>(grids)},
          partition{proxy<space>(table)},
          particles{proxy<space>(particles)},
          dt{dt} {}

#if 1
    constexpr void operator()(typename grids_t::size_type blockid,
                              typename grids_t::cell_index_type cellid) noexcept {
      float const dx = grids._dx;
      value_type const dx_inv = (value_type)1 / dx;
      if constexpr (grids_t::dim == 3) {
        float const D_inv = 4.f * dx_inv * dx_inv;
        using value_type = typename GridsT::value_type;
        auto grid = grids.grid(cellcentered_v);
        auto coord = partition._activeKeys[blockid] * (typename partition_t::Tn)grids_t::side_length
                     + grids_t::cellid_to_coord(cellid).template cast<typename partition_t::Tn>();
        auto posi = (coord + (value_type)0.5) * dx;
        auto block = grid.block(blockid);
        coord = coord - 1;  /// move to base coord
        for (auto&& iter : ndrange<grids_t::dim>(3)) {
          auto bucketno = buckets.table.query(coord + make_vec<typename partition_t::Tn>(iter));
          if (bucketno >= 0) {
            for (int st = buckets.offsets(bucketno), ed = buckets.offsets(bucketno + 1); st != ed;
                 ++st) {
              auto parid = buckets.indices(st);
              auto posp = particles.pos(parid);
              if (true) {
                auto xixp = posi - posp;
                value_type W = 1.f;
                auto diff = xixp * dx_inv;
                for (int d = 0; d != grids_t::dim; ++d) {
                  const auto xabs = gcem::abs(diff[d]);
                  if (xabs <= 0.5)
                    W *= (3. / 4 - xabs * xabs);
                  else if (xabs <= 1.5)
                    W *= (0.5 * (1.5 - xabs) * (1.5 - xabs));
                  else
                    W *= 0.f;
                }
                auto vi = block.template pack<grids_t::dim>(1, cellid);
                auto& vp = particles.vel(parid);
                auto& Cp = particles.C(parid);

                for (int d = 0; d != grids_t::dim; ++d)
                  atomic_add(wrapv<space>{}, &vp[d], vi[d] * W);
                for (int d = 0; d != grids_t::dim * grids_t::dim; ++d)
                  atomic_add(wrapv<space>{}, &Cp[d], W * vi[d % 3] * xixp[d / 3] * D_inv);
              }
            }
          }
        }
      }
    }

#elif 1

    constexpr void operator()(int bucketno) noexcept {
      float const dx = grids._dx;
      value_type const dx_inv = (value_type)1 / dx;
      if constexpr (grids_t::dim == 3) {
        float const D_inv = 4.f * dx_inv * dx_inv;
        using value_type = typename GridsT::value_type;
        auto grid = grids.grid(cellcentered_v);
        if (bucketno == 0 || buckets.offsets(bucketno + 1) == particles.size()
            || bucketno == buckets.counts.size() - 1)
          printf("bucket %d [%d, %d)\n", bucketno, (int)buckets.offsets(bucketno),
                 (int)buckets.offsets(bucketno + 1));
        for (int st = buckets.offsets(bucketno), ed = buckets.offsets(bucketno + 1); st < ed;
             ++st) {
          using TV = typename particles_t::TV;
          using TM = typename particles_t::TM;
          auto parid = buckets.indices(st);
          auto pos = particles.pos(parid);

          auto vel{TV::zeros()};
          TM C{TM::zeros()};

          auto arena = make_local_arena<grid_e::cellcentered>(dx, pos);
          for (auto loc : arena.range()) {
            auto [grid_block, local_index] = unpack_coord_in_grid(
                arena.coord(loc), grids_t::side_length, partition, grids.grid(cellcentered_v));
            auto xixp = arena.diff(loc);
            float W = arena.weight(loc);

            auto vi = grid_block.pack<particles_t::dim>(1, grids_t::coord_to_cellid(local_index));
            vel += vi * W;
            for (int d = 0; d < 9; ++d) C[d] += W * vi(d % 3) * xixp(d / 3) * D_inv;
          }
          particles.vel(parid) = vel;
          particles.C(parid) = C;
        }
      }
    }

#else
    constexpr void operator()(typename particles_t::size_type parid) noexcept {
      value_type const dx = grids._dx;
      value_type const dx_inv = (value_type)1 / dx;
      if constexpr (particles_t::dim == 3) {
        value_type const D_inv = 4.f * dx_inv * dx_inv;
        using ivec3 = vec<int, particles_t::dim>;
        using vec3 = vec<value_type, particles_t::dim>;
        using vec9 = vec<value_type, particles_t::dim * particles_t::dim>;
        using vec3x3 = vec<value_type, particles_t::dim, particles_t::dim>;
        vec3 pos{particles.pos(parid)};
        vec3 vel{vec3::zeros()};

        vec9 C{vec9::zeros()};
        auto arena = make_local_arena<grid_e::cellcentered>(dx, pos);
        for (auto loc : arena.range()) {
          auto [grid_block, local_index] = unpack_coord_in_grid(
              arena.coord(loc), grids_t::side_length, partition, grids.grid(cellcentered_v));
          auto xixp = arena.diff(loc);
          float W = arena.weight(loc);

          vec3 vi = grid_block.pack<particles_t::dim>(1, grids_t::coord_to_cellid(local_index));
          vel += vi * W;
          for (int d = 0; d < 9; ++d) C[d] += W * vi(d % 3) * xixp(d / 3) * D_inv;
        }
#  if 0
        pos += vel * dt;

        if constexpr (is_same_v<model_t, EquationOfStateConfig>) {
          float J = particles.J(parid);
          J = (1 + (C[0] + C[4] + C[8]) * dt) * J;
          // if (J < 0.1) J = 0.1;
          particles.J(parid) = J;
        } else {
          vec9 oldF{particles.F(parid)}, tmp{}, F{};
          for (int d = 0; d < 9; ++d) tmp(d) = C[d] * dt + ((d & 0x3) ? 0.f : 1.f);
          matrixMatrixMultiplication3d(tmp.data(), oldF.data(), F.data());
          particles.F(parid) = F;
        }
        particles.pos(parid) = pos;
#  endif
        particles.vel(parid) = vel;
        particles.C(parid) = C;
      }
    }
#endif

    model_t model;
    buckets_t buckets;
    grids_t grids;
    partition_t partition;
    particles_t particles;
    float dt;
  };

  template <execspace_e space, typename ParticlesT>
  struct PreG2C2PTransfer<ParticlesView<space, ParticlesT>> {
    using particles_t = ParticlesView<space, ParticlesT>;
    using value_type = typename particles_t::T;

    explicit PreG2C2PTransfer(wrapv<space>, ParticlesT& particles)
        : particles{proxy<space>(particles)} {}

    constexpr void operator()(typename particles_t::size_type parid) noexcept {
      particles.vel(parid) = particles_t::TV::zeros();
      particles.C(parid) = particles_t::TM::zeros();
    }

    particles_t particles;
  };

  template <execspace_e space, transfer_scheme_e scheme, typename ModelT, typename ParticlesT>
  struct PostG2C2PTransfer<scheme, ModelT, ParticlesView<space, ParticlesT>> {
    using model_t = ModelT;  ///< constitutive model
    using particles_t = ParticlesView<space, ParticlesT>;
    using value_type = typename particles_t::T;

    explicit PostG2C2PTransfer(wrapv<space>, wrapv<scheme>, float dt, const ModelT& model,
                               ParticlesT& particles)
        : model{model}, particles{proxy<space>(particles)}, dt{dt} {}

    constexpr void operator()(typename particles_t::size_type parid) noexcept {
      if constexpr (particles_t::dim == 3) {
        using ivec3 = vec<int, particles_t::dim>;
        using vec3 = vec<value_type, particles_t::dim>;
        using vec9 = vec<value_type, particles_t::dim * particles_t::dim>;
        using vec3x3 = vec<value_type, particles_t::dim, particles_t::dim>;
        vec3 pos{particles.pos(parid)};
        vec3 vel{particles.vel(parid)};

        vec9 C{particles.C(parid)};

        pos += vel * dt;

        if constexpr (is_same_v<model_t, EquationOfStateConfig>) {
          float J = particles.J(parid);
          J = (1 + (C[0] + C[4] + C[8]) * dt) * J;
          // if (J < 0.1) J = 0.1;
          particles.J(parid) = J;
        } else {
          vec9 oldF{particles.F(parid)}, tmp{}, F{};
          for (int d = 0; d < 9; ++d) tmp(d) = C[d] * dt + ((d & 0x3) ? 0.f : 1.f);
          matrixMatrixMultiplication3d(tmp.data(), oldF.data(), F.data());
          particles.F(parid) = F;
        }
        particles.pos(parid) = pos;
      }
    }

    model_t model;
    particles_t particles;
    float dt;
  };

}  // namespace zs