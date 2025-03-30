#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "Bbox.h"

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
		std::size_t operator()(const uvec3& v) const {
			return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
		}
	};

	// Une cellule de la grille contient ses voxels sous forme de hashmap
	struct SparseCell {
		std::unordered_map<uvec3, Voxel, Vec3Hash> voxels;
	};

	// La Sparse Grid est une hashmap de cellules (chaque cellule représente un bloc GRID_SIZE³)
	struct SparseGrid{
		
		AABB3 bbox;
		std::unordered_map<uvec3, SparseCell, Vec3Hash> map;

		void addVoxel(const Voxel& voxel);
		bool hasVoxelsInRegion(const uvec3& minPos, const uvec3& maxPos);
		std::vector<Voxel> getVoxelsInRegion(const uvec3& minPos, const uvec3& maxPos);
	};
}

