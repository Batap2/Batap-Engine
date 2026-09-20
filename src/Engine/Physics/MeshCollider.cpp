#include "Physics/MeshCollider.h"

#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <iostream>

namespace batap
{

JPH::ShapeRefC makeMeshShape(const BmeshData& data)
{
    if (data.vertices.empty() || data.indices.size() < 3)
        return nullptr;

    JPH::VertexList vertices;
    vertices.reserve(data.vertices.size());
    for (const v3f& v : data.vertices)
        vertices.push_back(JPH::Float3(v.x(), v.y(), v.z()));

    JPH::IndexedTriangleList triangles;
    triangles.reserve(data.indices.size() / 3);
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
        triangles.emplace_back(data.indices[i], data.indices[i + 1], data.indices[i + 2], 0u);

    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    const JPH::Shape::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        std::cerr << "[MeshCollider] " << result.GetError() << "\n";
        return nullptr;
    }
    return result.Get();
}

}  // namespace batap
