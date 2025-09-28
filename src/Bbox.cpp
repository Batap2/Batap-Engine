#include "Bbox.hpp"

template struct AABB3<glm::vec3>;

template<typename T>
void AABB3<T>::extends(const T& vec) {
	min = glm::min(min, vec);
	max = glm::max(max, vec);
}

template<typename T>
T AABB3<T>::size() const {
	return max - min;
}
