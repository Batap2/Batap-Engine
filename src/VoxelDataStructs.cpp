#include "VoxelDataStructs.h"
#include <random>

namespace VoxelDataStructs
{
    void SVO::construct(const std::vector<Voxel>& voxelList) {

    }

    void SVO::construct64_3(const std::vector<Voxel>& voxelList)
    {
        for(int i = 0; i < 6; i++)
        {

        }
    }

    static std::vector<Voxel> generateChunk_debug()
    {
        std::vector<Voxel> list;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);

        for(int i = 0; i < 64; ++i)
        {
            for(int j = 0; j < 40; ++j)
            {
                for(int k = 0; k < 64; ++k)
                {
                    //list.push_back({{i,j,k}, dis(gen)});
                }
            }
        }

        return list;
    }
}
