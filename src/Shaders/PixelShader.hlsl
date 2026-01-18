struct VS_OUTPUT {
    float4 _position : SV_POSITION;
    float3 _normal : TEXCOORD0;
    float2 _uv : TEXCOORD1;
};

float4 main(VS_OUTPUT input) : SV_Target
{
    return float4(input._normal, 1);
}