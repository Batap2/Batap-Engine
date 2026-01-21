#include "Transform_C.h"

#include <algorithm>

namespace rayvox
{

void Transform_C::setParent(Transform_C* newParent, bool keepWorld)
{
    if (newParent == _parent)
        return;
    if (newParent == this)
        return;

    // Avoid cycles
    for (auto* p = newParent; p; p = p->_parent)
    {
        if (p == this)
            return;
    }

    transform oldWorld = _world;

    if (_parent)
    {
        auto& siblings = _parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    _parent = newParent;

    if (_parent)
    {
        auto& ch = _parent->children;
        if (std::find(ch.begin(), ch.end(), this) == ch.end())
            ch.push_back(this);
    }

    if (keepWorld)
    {
        transform parentWorld = (_parent) ? _parent->_world : transform::Identity();
        transform localT = parentWorld.inverse().linear() * oldWorld;
        setLocalFromTransform(localT);
    }

    markDirty();
}

void Transform_C::setLocalPosition(const v3f& p)
{
    _localPosition = p;
    markDirty();
}

void Transform_C::setLocalRotation(const quatf& q)
{
    _localRotation = q.normalized();
    markDirty();
}

void Transform_C::setLocalScale(const v3f& s)
{
    _localScale = s;
    markDirty();
}

void Transform_C::translate(const v3f& vec, Space space)
{
    switch (space)
    {
        case Space::Local:
            _localPosition += _localRotation * vec;
            break;

        case Space::Parent:
            _localPosition += vec;
            break;

        case Space::World:
            if (_parent)
            {
                const transform& pw = _parent->_world;
                transform inv = pw.inverse();
                _localPosition += (inv * vec);
            }
            else
            {
                _localPosition += vec;
            }
            break;
    }

    markDirty();
}

void Transform_C::rotate(const quatf& delta, Space space)
{
    quatf d = delta.normalized();

    switch (space)
    {
        case Space::Local:
            _localRotation = (_localRotation * d).normalized();
            break;

        case Space::Parent:
            _localRotation = (d * _localRotation).normalized();
            break;

        case Space::World:
            if (_parent)
            {
                const quatf Qp = extractWorldRotation(_parent->_world);
                _localRotation = (Qp.conjugate() * d * Qp * _localRotation).normalized();
            }
            else
            {
                _localRotation = (d * _localRotation).normalized();
            }
            break;
    }

    markDirty();
}

void Transform_C::rotate(const v3f& axis, float radians, Space space)
{
    if (axis.squaredNorm() == 0.f)
        return;
    rotate(quatf(angleaxisf(radians, axis.normalized())), space);
}

void Transform_C::scale(const v3f& vec)
{
    _localScale = _localScale.cwiseProduct(vec);
    markDirty();
}

void Transform_C::updateIfDirty()
{
    if (!_dirty)
        return;

    if (_parent)
        _parent->updateIfDirty();

    updateWorldMatrix_NoRecurse();
    _dirty = false;
}

void Transform_C::markDirty()
{
    if (_dirty)
        return;
    _dirty = true;
    for (auto* c : children)
        if (c)
            c->markDirty();
}

void Transform_C::updateWorldMatrix_NoRecurse()
{
    _local = TRS_Transform(_localPosition, _localRotation, _localScale);
    _world = (_parent) ? (_parent->_world * _local) : _local;
}

void Transform_C::setLocalFromTransform(const transform& t)
{
    _localPosition = t.translation();

    const m3f A = t.linear();
    float sx = A.col(0).norm();
    if (sx == 0.f)
        sx = 1.f;
    float sy = A.col(1).norm();
    if (sy == 0.f)
        sy = 1.f;
    float sz = A.col(2).norm();
    if (sz == 0.f)
        sz = 1.f;
    _localScale = {sx, sy, sz};

    m3f R;
    R.col(0) = A.col(0) / sx;
    R.col(1) = A.col(1) / sy;
    R.col(2) = A.col(2) / sz;

    _localRotation = quatf(R).normalized();
}

quatf Transform_C::extractWorldRotation(const transform& t)
{
    // Retire scale (approx TRS sans shear)
    m3f R = t.linear();
    float sx = R.col(0).norm();
    if (sx == 0.f)
        sx = 1.f;
    float sy = R.col(1).norm();
    if (sy == 0.f)
        sy = 1.f;
    float sz = R.col(2).norm();
    if (sz == 0.f)
        sz = 1.f;
    R.col(0) /= sx;
    R.col(1) /= sy;
    R.col(2) /= sz;
    return quatf(R).normalized();
}

}  // namespace rayvox
