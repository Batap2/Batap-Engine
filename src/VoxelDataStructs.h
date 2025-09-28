#pragma once

#include "glm/glm.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "Bbox.hpp"

using namespace glm;

struct Voxel
{
	ivec3 pos;
	vec3 normal;
	uint32_t color;
};

constexpr int GRID_SIZE = 32;

struct Vec3Hash {
	std::size_t operator()(const uvec3& v) const {
		return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
	}
};

struct SparseCell {
	std::unordered_map<ivec3, Voxel, Vec3Hash> voxels;
};

struct SparseGrid {

	AABB3<vec3> bbox;
	std::unordered_map<ivec3, SparseCell, Vec3Hash> map;

	void addVoxel(const Voxel& voxel);
	bool hasVoxelsInRegion(const ivec3& minPos, const ivec3& maxPos);
	std::vector<Voxel> getVoxelsInRegion(const ivec3& minPos, const ivec3& maxPos);

	std::vector<Voxel> getAllVoxels();

	void DEBUG_fill();
};


