RWTexture2D<float4> buffer_render0 : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dt_id
                                : SV_DispatchThreadID, uint3 group_id
                                : SV_GroupID) {
  uint2 screen_coord = uint2(dt_id.x, dt_id.y);
  uint width, height;
  buffer_render0.GetDimensions(width, height);

  float4 finalColor = float4(1, 0, 0, 1);

  buffer_render0[screen_coord.xy] = finalColor;
}