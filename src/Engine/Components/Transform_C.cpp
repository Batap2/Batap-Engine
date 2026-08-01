#include "Transform_C.h"

#include "Reflection/ComponentRegistry.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

namespace batap {

    void Transform_C::afterDeserialize(EntityHandle h, World& world)
    {
        auto* tc = h._reg->try_get<Transform_C>(h._entity);
        if (!tc)
            return;
        tc->_localDirty = true;
        world.systems_->_transforms->markDirty(h);
    }

    void Transform_C::registerReflection()
    {
        // Keys kept as pos/rot/scale — they are what scenes on disk use.
        addComponentType<Transform_C>(
            "transform",
            ComponentMeta{.flag = ComponentFlag::Transform,
                          .onDeserialized = &Transform_C::afterDeserialize,
                          .customEditor = true},
            {field<&Transform_C::_localPosition>("pos"),
             field<&Transform_C::_localRotation>("rot"),
             field<&Transform_C::_localScale>("scale")});
    }

    namespace
    {
    // Registration at static init is the point: silence -Wglobal-constructors.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
    [[maybe_unused]] const bool _batapTransformRegistered =
        (Transform_C::registerReflection(), true);
#pragma clang diagnostic pop
    }  // namespace

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
}
