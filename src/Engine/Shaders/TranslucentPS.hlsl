#include "MeshSurface.hlsli"
#include "Translucency.hlsli"

TranslucentOutput main(VS_OUTPUT i)
{
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    Material mat = MeshMaterial(inst);
    float albedoAlpha;
    Surface s = MeshSurface(i, inst, mat, albedoAlpha);
    const float coverage = saturate(albedoAlpha);
    return TranslucentAccumulate(ShadeSurface(mat.shadingModel_, s, coverage), coverage,
                                 length(i.posRel_));
}
