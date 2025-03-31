#include "VoxelDataStructs.h"
#include <random>

using namespace glm;

namespace VoxelDataStructs
{
	void SparseGrid::addVoxel(const Voxel& voxel) {
		uvec3 chunkPos = { voxel.pos.x / GRID_SIZE, voxel.pos.y / GRID_SIZE, voxel.pos.z / GRID_SIZE };
		uvec3 localPos = { voxel.pos.x % GRID_SIZE, voxel.pos.y % GRID_SIZE, voxel.pos.z % GRID_SIZE };

		map[chunkPos].voxels[localPos] = voxel;
		bbox.extends(voxel.pos);
	}

	bool SparseGrid::hasVoxelsInRegion(const uvec3& minPos, const uvec3& maxPos) {
		uvec3 minChunk = { minPos.x / GRID_SIZE, minPos.y / GRID_SIZE, minPos.z / GRID_SIZE };
		uvec3 maxChunk = { maxPos.x / GRID_SIZE, maxPos.y / GRID_SIZE, maxPos.z / GRID_SIZE };

		for (int cx = minChunk.x; cx <= maxChunk.x; ++cx) {
			for (int cy = minChunk.y; cy <= maxChunk.y; ++cy) {
				for (int cz = minChunk.z; cz <= maxChunk.z; ++cz) {
					uvec3 chunkPos = { cx, cy, cz };

					if (map.find(chunkPos) != map.end()) {
						return true;
					}
				}
			}
		}
		return false;
	}

	std::vector<Voxel> SparseGrid::getVoxelsInRegion(const uvec3& minPos, const uvec3& maxPos) {
		std::vector<Voxel> result;

		uvec3 minChunk = { minPos.x / GRID_SIZE, minPos.y / GRID_SIZE, minPos.z / GRID_SIZE };
		uvec3 maxChunk = { maxPos.x / GRID_SIZE, maxPos.y / GRID_SIZE, maxPos.z / GRID_SIZE };

		for (int cx = minChunk.x; cx <= maxChunk.x; ++cx) {
			for (int cy = minChunk.y; cy <= maxChunk.y; ++cy) {
				for (int cz = minChunk.z; cz <= maxChunk.z; ++cz) {
					uvec3 chunkPos = { cx, cy, cz };

					auto it = map.find(chunkPos);
					if (it != map.end()) {
						for (const auto& [localPos, voxel] : it->second.voxels) {
							uvec3 voxelPos = {
								chunkPos.x * GRID_SIZE + localPos.x,
								chunkPos.y * GRID_SIZE + localPos.y,
								chunkPos.z * GRID_SIZE + localPos.z
							};
							if (voxelPos.x >= minPos.x && voxelPos.y >= minPos.y && voxelPos.z >= minPos.z &&
								voxelPos.x <= maxPos.x && voxelPos.y <= maxPos.y && voxelPos.z <= maxPos.z) {
								result.push_back(voxel);
							}
						}
					}
				}
			}
		}
		return result;
	}

	std::vector<Voxel> SparseGrid::getAllVoxels()
	{
		std::vector<Voxel> vec;
		for (auto& [pos, cell] : map) {
			for (auto& [pos2, voxel] : cell.voxels) {
				vec.push_back(voxel);
			}
		}
		return vec;
	}

	void SparseGrid::DEBUG_fill()
	{
		for (int x = 0; x < 100; x++)
		{
			for (int y = 0; y < 4; y++)
			{
				for (int z = 0; z < 4; z++) {
					Voxel v{ .pos = vec3(x,y,z) };
					addVoxel(v);
				}
			}
		}
	}
}
