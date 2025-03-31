#pragma once
#include <glm/glm.hpp>

using namespace glm;
struct AABB3 {
	vec3 min = vec3(0.0f, 0.0f, 0.0f);
	vec3 max = vec3(0.0f, 0.0f, 0.0f);

public:
	void extends(const vec3& vec);
};
