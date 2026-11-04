#include "Billboard.hlsli"
#include "Translucency.hlsli"

TranslucentOutput main(VS_OUTPUT i)
{
    Material mat;
    float alpha;
    Surface s = BillboardSurface(i, mat, alpha);
    const float coverage = saturate(alpha);
    if (coverage <= 0.0f)
        discard;
    return TranslucentAccumulate(ShadeSurface(mat.shadingModel_, s, coverage), coverage,
                                 length(i.posRel_));
}
