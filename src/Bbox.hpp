#pragma once
#include <glm/glm.hpp>

using namespace glm;

template<typename T>
struct AABB3 {
	T min = T(0);
	T max = T(0);

	void extends(const T& vec);
	T size() const;
};

extern template struct AABB3<glm::vec3>;
