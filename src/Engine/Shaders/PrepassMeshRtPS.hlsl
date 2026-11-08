#include "MeshSurface.hlsli"
#include "Reflections.hlsli"

struct PrepassOutput
{
    float4 normal_ : SV_Target0;
    uint2 surface_ : SV_Target1;
};

PrepassOutput main(VS_OUTPUT i)
{
    const StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    const Material mat = MeshMaterial(inst);
    float albedoAlpha;
    const Surface s = MeshSurface(i, inst, mat, albedoAlpha);
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    PrepassOutput o;
    o.normal_ = float4(normalize(mul((float3x3)cam.view_, i.nrmWS_)) * 0.5f + 0.5f, 1.0f);
    o.surface_ = ReflectSurfaceEncode(s.N_, ReflectRoughness(mat.shadingModel_, s.roughness_),
                                      ReflectKindMesh, g_draw.instanceIndex_);
    return o;
}
