#pragma once

#include <bitset>

namespace SVO{

    struct BranchNode{
        std::bitset<8> leaflMask;
        std::bitset<8> childMask;
        std::bitset<16> relativeAddress;

        uint32_t ToRaw();
    };

    struct LeafNode{
        std::bitset<30> normal;
        std::bitset<32> material;

        uint64_t ToRaw();
    };

}