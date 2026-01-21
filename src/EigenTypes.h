#pragma once
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/StdVector>
#include <cstdint>

using v2f = Eigen::Vector2f;
using v3f = Eigen::Vector3f;
using v4f = Eigen::Vector4f;

using v2d = Eigen::Vector2d;
using v3d = Eigen::Vector3d;
using v4d = Eigen::Vector4d;

using v2i = Eigen::Vector2i;
using v3i = Eigen::Vector3i;
using v4i = Eigen::Vector4i;

using v2u = Eigen::Matrix<uint32_t, 2, 1>;
using v3u = Eigen::Matrix<uint32_t, 3, 1>;
using v4u = Eigen::Matrix<uint32_t, 4, 1>;

using m2f = Eigen::Matrix2f;
using m3f = Eigen::Matrix3f;
using m4f = Eigen::Matrix4f;

using m2d = Eigen::Matrix2d;
using m3d = Eigen::Matrix3d;
using m4d = Eigen::Matrix4d;

template<typename T, int R, int C>
using MatRM = Eigen::Matrix<T, R, C, Eigen::RowMajor>;

using m3f_rm = MatRM<float, 3, 3>;
using m4f_rm = MatRM<float, 4, 4>;
using m3d_rm = MatRM<double, 3, 3>;
using m4d_rm = MatRM<double, 4, 4>;

using quatf      = Eigen::Quaternionf;
using quatd      = Eigen::Quaterniond;

using angleaxisf = Eigen::AngleAxisf;
using angleaxisd = Eigen::AngleAxisd;

using affine3f   = Eigen::Affine3f;
using isometry3f = Eigen::Isometry3f;
using affine3d   = Eigen::Affine3d;
using isometry3d = Eigen::Isometry3d;

using transform = Eigen::Transform<float, 3, Eigen::Affine>;
using transformd = Eigen::Transform<double, 3, Eigen::Affine>;

using m4f_ref      = Eigen::Ref<m4f>;
using m4f_cref     = Eigen::Ref<const m4f>;
using v3f_ref      = Eigen::Ref<v3f>;
using v3f_cref     = Eigen::Ref<const v3f>;

inline m4f TRS(const v3f& translation, const quatf& rotation, const v3f& scale)
{
    m4f m = m4f::Identity();

    // Rotation
    m.block<3,3>(0,0) = rotation.normalized().toRotationMatrix();

    // Scale columns (Eigen column-major convention)
    m.block<3,1>(0,0) *= scale.x();
    m.block<3,1>(0,1) *= scale.y();
    m.block<3,1>(0,2) *= scale.z();

    // Translation
    m.block<3,1>(0,3) = translation;

    return m;
}

inline transform TRS_Transform(const v3f& translation,
                               const quatf& rotation,
                               const v3f& scale)
{
    transform t = transform::Identity();
    t.linear() = rotation.normalized().toRotationMatrix() * scale.asDiagonal();
    t.translation() = translation;
    return t;
}
