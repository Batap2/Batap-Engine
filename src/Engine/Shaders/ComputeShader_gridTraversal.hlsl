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

struct Ray
{
    float3 origin;
    float3 direction;
};

struct Hit
{
    bool hit;
    float distance;
    float3 hitPoint;
    float3 normal;
};

struct Sphere
{
    float3 center;
    float radius;
};

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

Hit IntersectRaySphere(Ray ray, Sphere sphere)
{
    Hit result;
    result.hit = false;

    float3 oc = ray.origin - sphere.center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant > 0.0f)
    {
        float sqrtDiscriminant = sqrt(discriminant);
        float t1 = (-b - sqrtDiscriminant) / (2.0f * a);
        float t2 = (-b + sqrtDiscriminant) / (2.0f * a);

        // Find the closest positive intersection
        if (t1 > 0.0f || t2 > 0.0f)
        {
            float t = min(t1, t2);
            if (t > 0.0f)
            {
                result.hit = true;
                result.distance = t;
                result.hitPoint = ray.origin + t * ray.direction;
                result.normal = normalize(result.hitPoint - sphere.center);
            }
        }
    }

    return result;
}

struct AABB
{
    float3 center;
    float3 extent;
};

bool intersectAABB(float3 origin, float3 direction, AABB box)
{
    float3 boxCenter = box.center;
    float3 boxExtent = box.extent;

    // Transform the ray to the aligned-box coordinate system.
    float3 rayOrigin = origin - boxCenter;

    if (any(abs(rayOrigin) > boxExtent && rayOrigin * direction >= 0.0))
    {
        return false;
    }

    // Perform AABB-line test
    float3 WxD = cross(direction, rayOrigin);
    float3 absWdU = abs(direction);
    if (any(WxD > float3(dot(boxExtent.yz, absWdU.zy),
                         dot(boxExtent.xz, absWdU.zx),
                         dot(boxExtent.xy, absWdU.yx))))
    {
        return false;
    }

    return true;
}

Hit RayIntersectsAABB(Ray ray, float3 boxMin, float3 boxMax)
{
    float3 invDir = 1.0 / ray.direction;

    float3 t0s = (boxMin - ray.origin) * invDir;
    float3 t1s = (boxMax - ray.origin) * invDir;

    float3 tsmaller = min(t0s, t1s);
    float3 tbigger = max(t0s, t1s);

    float tMin = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
    float tMax = min(min(tbigger.x, tbigger.y), tbigger.z);

    Hit result;
    result.hit = tMax >= tMin && tMax >= 0.0;

    if (result.hit)
    {
        result.distance = tMin > 0.0 ? tMin : tMax;
        result.hitPoint = ray.origin + result.distance * ray.direction;

        if (result.distance == tsmaller.x)
            result.normal = invDir.x > 0.0 ? float3(-1, 0, 0) : float3(1, 0, 0);
        else if (result.distance == tsmaller.y)
            result.normal = invDir.y > 0.0 ? float3(0, -1, 0) : float3(0, 1, 0);
        else
            result.normal = invDir.z > 0.0 ? float3(0, 0, -1) : float3(0, 0, 1);
    }
    else
    {
        result.distance = 0.0;
        result.hitPoint = float3(0, 0, 0);
        result.normal = float3(0, 0, 0);
    }

    return result;
}

[numthreads(8, 8, 1)]
void main(uint3 dt_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID)
{
    uint2 screen_coord = uint2(dt_id.x, dt_id.y);
    uint width, height;
    framebuffer.GetDimensions(width, height);

    float4 finalColor = float4(0,0,0,1);

    Sphere sphere;
    sphere.center = float3(0,0,0);
    sphere.radius = 1;

    Ray ray = GenerateRay(screen_coord, uint2(width, height), cameraBuffer.pos,
    cameraBuffer.forward, cameraBuffer.right, cameraBuffer.fov);

    Hit lh;
    Hit h;
    lh.distance = 9999999999.0f;

    for(int i = 0; i < 64; i++)
    {
        for(int j = 0; j < 64; j++)
        {
            h = RayIntersectsAABB(ray, float3(i,0,j), float3(i + 0.9f, 0.9f, j + 0.9f));
            if(h.hit && h.distance < lh.distance){
                    lh = h;
            }
        }
    }

    if(lh.hit)
    {
        finalColor = float4(abs(lh.normal),1);
    }
    framebuffer[screen_coord.xy] = finalColor;
}