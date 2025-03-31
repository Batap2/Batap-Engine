#include "Bbox.hpp"

void AABB3::extends(const vec3& vec) {
	min = glm::min(min, vec);
	max = glm::max(max, vec);
}