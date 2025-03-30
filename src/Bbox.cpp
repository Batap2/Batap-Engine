#include "Bbox.h"

void AABB3::extends(vec3 &vec){
    min = glm::min(min, vec);
    max = glm::max(max, vec);
}