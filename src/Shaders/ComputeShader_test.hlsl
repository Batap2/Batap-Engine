RWTexture2D<float4> buffer_render0 : register(u0);

struct CameraData
{
    float3  _pos;
    float   _znear;
    float3  _forward;
    float   _zfar;
    float3  _right;
    float   _fov;
    float4x4 _view;
    float4x4 _proj;
};

cbuffer CameraCB : register(b0)
{
    CameraData cam;
};

[numthreads(8, 8, 1)] void main(uint3 dt_id
                                : SV_DispatchThreadID, uint3 group_id
                                : SV_GroupID) {
  uint2 screen_coord = uint2(dt_id.x, dt_id.y);
  uint width, height;
  buffer_render0.GetDimensions(width, height);

  float4 finalColor = float4(float(dt_id.x)/width, float(dt_id.y)/height, 0, 1);

  finalColor = float4(cam._pos,1);

  buffer_render0[screen_coord.xy] = finalColor;
}