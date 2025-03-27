#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <unordered_map>

using namespace glm;
namespace VoxelDataStructs
{
	struct Voxel
	{
		uvec3 pos;
		uint32_t color;
	};

	constexpr int GRID_SIZE = 32;

	struct Vec3Hash {
	};

	// Une cellule de la grille contient ses voxels sous forme de hashmap
	struct SparseCell {
		//std::unordered_map<XMUINT3, Voxel, Vec3Hash> voxels;
	};

	// La Sparse Grid est une hashmap de cellules (chaque cellule représente un bloc GRID_SIZE³)
	struct SparseGrid{
		//std::unordered_map<XMUINT3, SparseCell, Vec3Hash> map;

		//void addVoxel(const Voxel& voxel);
	};
}

