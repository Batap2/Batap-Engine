#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

using namespace DirectX;
namespace VoxelDataStructs
{
	struct Voxel
	{
		XMUINT3 pos;
		uint32_t color;
	};

	struct SVO_node
	{
		bool leaf;
		SVO_node* children[8];
		Voxel voxel;
	};

	struct SVO
	{
		bool leaf;
		SVO_node* root;

		void construct(const std::vector<Voxel>& voxelList);
		void construct64_3(const std::vector<Voxel>& voxelList);
	};

	struct Chunk
	{
		XMINT3 offsetPos;
		SVO SVO64_3;
	};

	static std::vector<Voxel> generateChunk_debug();
}

