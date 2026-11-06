// Modèle de binding Vulkan (docs/vulkan.md §10) :
//   set 0 = bindless global (sampler s0, textures t1[])
//   set 1 = données de frame (storage buffers)
//   push constants = indices du draw courant
#include "MeshSurface.hlsli"
#include "Motion.hlsli"

float4 main(VS_OUTPUT i) : SV_Target
{
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    Material mat = MeshMaterial(inst);
    float albedoAlpha;
    Surface s = MeshSurface(i, inst, mat, albedoAlpha);
    ApplyScreenAo(s, i.position_);

    if ((g_draw.debugFlags_ & DebugMotionVectors) != 0u)
    {
        const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
        const float3 prevRel = PreviousPosition(i.posRel_, RelativeTo(inst.world_, cam.pos_),
                                                RelativeTo(inst.prevWorld_, cam.prevPos_));
        return float4(MotionDebugColor(MotionNdc(cam, i.posRel_, prevRel)), 1.0f);
    }
    return float4(ShadeSurface(mat.shadingModel_, s), 1.0f);
}
