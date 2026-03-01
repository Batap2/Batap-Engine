RWTexture2D<float4> framebuffer : register(u0);

cbuffer CameraBuffer : register(b0)
{
    struct CameraBuffer
    {
        float3 pos;
        float Znear;
        float3 forward;
        float Zfar;
        float3 right;
        float fov;
    }cameraBuffer;
};

static const int MAX_MARCHING_STEPS = 70;
static const float MIN_DIST = 0.0f;
static const float MAX_DIST = 100;
static const float EPSILON = 0.0001f;
static const float PI = 3.141592f;

struct Ray
{
    float3 origin;
    float3 direction;
};

struct HD
{
	//pos, normal, color;
	float3x3 pnc;
	float d;
	bool hit;
};

float4 min_c(float4 a, float4 b){
	return a.w < b.w ? a : b;
}

float sdBox( float3 p, float3 b )
{
  float3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

float4 DE(float3 p, float3 index = float3(0,0,0))
{
	float4 d = float4(0,0,0,999999999);
	for(int i = 0; i < 64; ++i)
	{
        for(int j = 0; j < 64; ++j)
            {
                d = min_c(d, float4(float(i)/10,0,0, sdBox(p - float3(i,0,j), float3(0.45f,0.45f,0.45f) )));
            }
	}

    return d;
}

Ray GenerateRay(float2 pixelPos, float2 screenSize, float3 cameraPos,
                float3 forward, float3 right, float fov)
{
    float aspectRatio = screenSize.x / screenSize.y;
    float2 ndc = (pixelPos / screenSize) * 2.0f - float2(1.0f, 1.0f);

    float tanFov = tan(radians(fov) * 0.5f);
    float3 up = cross(right, forward);

    right *= tanFov * aspectRatio;
    up *= tanFov;


    float3 cameraSpaceDir = normalize(ndc.x * right + ndc.y * up + forward);

    // Construct the ray
    Ray ray;
    ray.origin = cameraPos;
    ray.direction = cameraSpaceDir;

    return ray;
}

HD RayMarch(float3 ro, float3 rd)
{
	HD hd;
    for(int i=0;i<MAX_MARCHING_STEPS;i++)
    {

		hd.pnc[0] = ro + rd * hd.d;

		float4 ds = DE(hd.pnc[0]);

		hd.pnc[2] = ds.xyz;
        hd.d += ds.w;

        if(hd.d > MAX_DIST){
            hd.hit = false;
			return hd;
		};
		if(hd.d < MIN_DIST)
		{
			break;
		}
    }
	hd.hit = true;
    return hd;
}

float3 computeColor(float3 ro, float3 rd)
{

	HD hd = RayMarch(ro,rd); // Distance

	if(hd.hit == false)
		return float3(0,0,0);

	//float3 p = hd.pnc[0];
	//float3 n = calcNormalRep(p);
    //hd.d/= 10.;

	//Light l = Light(ro, vec3(1,1,1));

	float3 color = float3(1,1,1);

	//vec3 phong = phong(l.p, ro, n, l.c, color, p, 0.2, 1, 1000*((cos(t)+1))+100, 100);

	//float shadow = RayMarch(p + n/100000,normalize(l.p - p)).d;
	//shadow = 1 - step(0.000001, shadow)/4;

	//return(vec3(hd.d/10));

	return(hd.pnc[2]);
}

[numthreads(8, 8, 1)]
void main(uint3 dt_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID)
{
    uint2 screen_coord = uint2(dt_id.x, dt_id.y);
    uint width, height;
    framebuffer.GetDimensions(width, height);

    float4 finalColor = float4(0,0,0,1);


    Ray ray = GenerateRay(screen_coord, uint2(width, height), cameraBuffer.pos,
    cameraBuffer.forward, cameraBuffer.right, cameraBuffer.fov);


//     for(int i = 0; i < 64; i++)
//     {
//         for(int j = 0; j < 64; j++)
//         {
//             h = RayIntersectsAABB(ray, float3(i,0,j), float3(i + 0.9f, 0.9f, j + 0.9f));
//             if(h.hit && h.distance < lh.distance){
//                     lh = h;
//             }
//         }
//     }

    finalColor = float4(computeColor(ray.origin, ray.direction),1);

    framebuffer[screen_coord.xy] = finalColor;
}