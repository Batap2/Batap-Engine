#include "SVO.h"

void SVO::generate(const SparseGrid& sparseGrid)
{
	vec3 bboxSize = sparseGrid.bbox.size();

	populate(sparseGrid, bbox);
}

SVOHelperNode SVO::populate(const SparseGrid& sparseGrid, const AABB3<vec3>& bbox)
{
	SVOHelperNode node;

	// voir s'i

	return node;
}

void SVO::addNode(LeafNode node)
{
}

void SVO::addNode(BranchNode node)
{
}

void SVO::addNode(FarNode node)
{
}
