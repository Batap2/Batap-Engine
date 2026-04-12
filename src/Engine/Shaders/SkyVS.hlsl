struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

VS_OUT main(uint vID : SV_VertexID)
{
    // Full-screen triangle covering the entire [-1,1]x[-1,1] NDC space.
    // vID=0 → (-1,-1), vID=1 → (3,-1), vID=2 → (-1,3)  (excess clipped by rasterizer)
    float2 uv  = float2((vID << 1) & 2, vID & 2);
    float2 ndc = uv * 2.0f - 1.0f;

    VS_OUT o;
    o.pos = float4(ndc, 1.0f, 1.0f);  // z == w → depth = 1.0 after divide (far plane)
    o.ndc = ndc;
    return o;
}
