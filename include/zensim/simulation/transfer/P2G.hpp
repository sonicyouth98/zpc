#pragma once
#include "Scheme.hpp"
#include "zensim/container/HashTable.hpp"
#include "zensim/container/Structure.hpp"
#include "zensim/container/Structurefree.hpp"
#include "zensim/container/Vector.hpp"

namespace zs {

  template <transfer_scheme_e, typename ConstitutiveModel, typename ParticlesT, typename TableT,
            typename GridBlocksT>
  struct P2GTransfer;

  template <execspace_e space, transfer_scheme_e scheme, typename Model, typename ParticlesT,
            typename TableT, typename GridBlocksT>
  P2GTransfer(wrapv<space>, wrapv<scheme>, float, Model, ParticlesT, TableT, GridBlocksT)
      -> P2GTransfer<scheme, Model, ParticlesProxy<space, ParticlesT>,
                     HashTableProxy<space, TableT>, GridBlocksProxy<space, GridBlocksT>>;

}  // namespace zs