RWTexture2D<float4> buffer_render0 : register(u0);

float HeavyCompute(uint iterations) {
  float r = 0.0f;
  [loop] for (uint i = 0; i < iterations; i++) {
    // Combinaisons non triviales pour empêcher l’optimisation
    r += sin(r + (float)i * 0.001) * cos(r * 0.0001 + (float)(i % 100));
    r = frac(r); // garde la valeur entre 0 et 1
  }
  return r;
}

[numthreads(8, 8, 1)] void main(uint3 dt_id
                                : SV_DispatchThreadID, uint3 group_id
                                : SV_GroupID) {
  uint2 screen_coord = uint2(dt_id.x, dt_id.y);
  uint width, height;
  buffer_render0.GetDimensions(width, height);

  float4 finalColor = float4(1, 0, 0, 1);

  float result = HeavyCompute(screen_coord.x * 1);
    finalColor.x = result;


  buffer_render0[screen_coord.xy] = finalColor;
}