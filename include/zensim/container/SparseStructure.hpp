#pragma once
#include "HashTable.hpp"
#include "Structure.hpp"
#include "TileVector.hpp"
#include "Vector.hpp"

namespace zs {

  struct AdaptiveFloatGrid {
    AdaptiveFloatGrid(const MemoryHandle mh = {}) {
      leaves = Layer<3>{0, mh};
      int2 = Layer<4>{1, mh};
      int1 = Layer<5>{2, mh};
    }
    template <auto NumSideLengthBits = 3> struct Layer {
      Layer(const int level = 0, const MemoryHandle mh = {})
          : level{level},
            tables{mh.memspace(), mh.devid()},
            blocks{1.f, 0, mh.memspace(), mh.devid()} {}

      void resize(const std::size_t numBlocks) {
        tables.resize(numBlocks);
        blocks.resize(numBlocks);
      }

      int level{0};
      HashTable<int, 3, int> tables{};
      GridBlocks<GridBlock<float, 3, 2, NumSideLengthBits>>
          blocks;  ///< signed distance (1), velocity (3)
    };

    void resize(const std::size_t numBlocks) { leaves.resize(numBlocks); }

    float dx{1.f};
    Layer<3> leaves;
    Layer<4> int2;
    Layer<5> int1;
  };

}  // namespace zs