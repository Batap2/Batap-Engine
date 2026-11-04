#ifndef BATAP_MESH_INSTANCES_HLSLI
#define BATAP_MESH_INSTANCES_HLSLI

#include "ShaderInterop.h"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

float MeshCutDistance(StaticMeshGPUData inst, float3 objectPos)
{
    return dot(inst.cut_.xyz, objectPos) + inst.cut_.w;
}

#endif
