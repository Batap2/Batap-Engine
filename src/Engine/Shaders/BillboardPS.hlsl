#include "Billboard.hlsli"

float4 main(VS_OUTPUT i) : SV_Target
{
    Material mat;
    float alpha;
    Surface s = BillboardSurface(i, mat, alpha);

    // Cutout, not blending: blending would need a back-to-front sort per frame.
    if (alpha < 0.5f)
        discard;

    return float4(ShadeSurface(mat.shadingModel_, s), 1.f);
}
