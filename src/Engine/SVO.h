#pragma once

#include <bitset>
#include <vector>
#include "Bbox.hpp"
#include "VoxelDataStructs.h"

struct SVOHelperNode {
	uint32_t data1;
	uint32_t data2;
	bool isLeaf;
	bool exists;
	uint32_t childRelativeIndex;
	uint32_t Index;

};

struct BranchNode {
	std::bitset<8> leaflMask;
	std::bitset<8> childMask;
	std::bitset<16> relativeAddress;

	uint32_t pack();
};

struct FarNode {
	uint32_t farPtr;
};

struct LeafNode {
	std::bitset<30> normal;
	std::bitset<32> material;

	std::tuple<uint32_t, uint32_t> pack();
};

struct SVO {
public:
// 	void generate(const SparseGrid& sparseGrid);
// private:
// 	AABB3<vec3> bbox;
// 	uint maxDepth = 0;
// 	std::vector<uint32_t> data;

// 	SVOHelperNode populate(const SparseGrid& sparseGrid, const AABB3<vec3>& bbox);

// 	void addNode(LeafNode node);
// 	void addNode(BranchNode node);
// 	void addNode(FarNode node);
};
