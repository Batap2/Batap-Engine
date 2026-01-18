#pragma once

#include <vector>
#include "EigenTypes.h"
#include "Handles.h"

namespace rayvox
{

enum class Space
{
    Local,
    World
};

struct Transform_C
{
    bool _dirty = false;
    v3f _localPosition{0, 0, 0};
    quatf _localRotation = quatf::Identity();
    v3f _localScale{1, 1, 1};

    // cache
    m4f _local = m4f::Identity();
    m4f _world = m4f::Identity();

    Transform_C* _parent = nullptr;
    std::vector<Transform_C*> children;

    void translate(const v3f& vec, Space space = Space::Local);
    void rotate();
    void scale(const v3f& vec, Space space = Space::Local);

    void UpdateWorldMatrix()
    {
        _local = TRS(_localPosition, _localRotation, _localScale);

        if (_parent)
            _world = _parent->_world * _local;
        else
            _world = _local;
    }

    // pour update et garder l'ordre, il faut que le parent soit update avant l'enfant
    void UpdateIfDirty(Transform_C* t)
    {
        if (t->_dirty)
        {
            if (t->_parent)
                UpdateIfDirty(t->_parent);  // garantit l’ordre
            t->UpdateWorldMatrix();
            t->_dirty = false;
        }
    }
};
}  // namespace rayvox
