#pragma once

#include <bitset>
#include <vector>
#include "Bbox.hpp"

struct BranchNode {
	std::bitset<8> leaflMask;
	std::bitset<8> childMask;
	std::bitset<16> relativeAddress;

	uint32_t ToRaw();
};

struct FarNode {
	uint32_t farPtr;
};

struct LeafNode {
	std::bitset<30> normal;
	std::bitset<32> material;

	uint64_t ToRaw();
};

struct SVO {
	AABB3 bbox;
	uint maxDepth = 0;
	std::vector<uint32_t> data;
};
