#pragma once

#include "core/math/vec3.h"
#include "core/math/vec4.h"
#include <string>

namespace ne {

// Column-major, matching Vulkan and Slang
struct Mat4 {
  Vec4 cols[4]{
    Vec4(1.0f, 0.0f, 0.0f, 0.0f),
    Vec4(0.0f, 1.0f, 0.0f, 0.0f),
    Vec4(0.0f, 0.0f, 1.0f, 0.0f),
    Vec4(0.0f, 0.0f, 0.0f, 1.0f)
  };

  constexpr Mat4() = default;
  constexpr explicit Mat4(float iScalar)
    : cols{
        Vec4(iScalar, 0.0f, 0.0f, 0.0f),
        Vec4(0.0f, iScalar, 0.0f, 0.0f),
        Vec4(0.0f, 0.0f, iScalar, 0.0f),
        Vec4(0.0f, 0.0f, 0.0f, iScalar)
      } {}
  constexpr Mat4(const Vec4& iC0, const Vec4& iC1, const Vec4& iC2, const Vec4& iC3)
    : cols{iC0, iC1, iC2, iC3} {}

  constexpr Vec4& operator[](size_t iIndex) { return cols[iIndex]; }
  constexpr const Vec4& operator[](size_t iIndex) const { return cols[iIndex]; }

  constexpr const float* data() const { return cols[0].data(); }
  constexpr float* data() { return cols[0].data(); }

  static const Mat4 Identity;

  constexpr Mat4 operator*(const Mat4& iM) const {
    Mat4 res;
    res.cols[0] = cols[0] * iM.cols[0].x + cols[1] * iM.cols[0].y + cols[2] * iM.cols[0].z + cols[3] * iM.cols[0].w;
    res.cols[1] = cols[0] * iM.cols[1].x + cols[1] * iM.cols[1].y + cols[2] * iM.cols[1].z + cols[3] * iM.cols[1].w;
    res.cols[2] = cols[0] * iM.cols[2].x + cols[1] * iM.cols[2].y + cols[2] * iM.cols[2].z + cols[3] * iM.cols[2].w;
    res.cols[3] = cols[0] * iM.cols[3].x + cols[1] * iM.cols[3].y + cols[2] * iM.cols[3].z + cols[3] * iM.cols[3].w;
    return res;
  }

  constexpr Vec4 operator*(const Vec4& iV) const {
    return Vec4(
      cols[0].x * iV.x + cols[1].x * iV.y + cols[2].x * iV.z + cols[3].x * iV.w,
      cols[0].y * iV.x + cols[1].y * iV.y + cols[2].y * iV.z + cols[3].y * iV.w,
      cols[0].z * iV.x + cols[1].z * iV.y + cols[2].z * iV.z + cols[3].z * iV.w,
      cols[0].w * iV.x + cols[1].w * iV.y + cols[2].w * iV.z + cols[3].w * iV.w
    );
  }

  constexpr Mat4 transposed() const {
    return Mat4(
      Vec4(cols[0].x, cols[1].x, cols[2].x, cols[3].x),
      Vec4(cols[0].y, cols[1].y, cols[2].y, cols[3].y),
      Vec4(cols[0].z, cols[1].z, cols[2].z, cols[3].z),
      Vec4(cols[0].w, cols[1].w, cols[2].w, cols[3].w)
    );
  }

  constexpr Mat4 inversed() const {
    const float m00 = cols[0].x, m01 = cols[0].y, m02 = cols[0].z, m03 = cols[0].w;
    const float m10 = cols[1].x, m11 = cols[1].y, m12 = cols[1].z, m13 = cols[1].w;
    const float m20 = cols[2].x, m21 = cols[2].y, m22 = cols[2].z, m23 = cols[2].w;
    const float m30 = cols[3].x, m31 = cols[3].y, m32 = cols[3].z, m33 = cols[3].w;

    const float s0 = m00 * m11 - m10 * m01;
    const float s1 = m00 * m12 - m10 * m02;
    const float s2 = m00 * m13 - m10 * m03;
    const float s3 = m01 * m12 - m11 * m02;
    const float s4 = m01 * m13 - m11 * m03;
    const float s5 = m02 * m13 - m12 * m03;

    const float c5 = m22 * m33 - m32 * m23;
    const float c4 = m21 * m33 - m31 * m23;
    const float c3 = m21 * m32 - m31 * m22;
    const float c2 = m20 * m33 - m30 * m23;
    const float c1 = m20 * m32 - m30 * m22;
    const float c0 = m20 * m31 - m30 * m21;

    const float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

    if (math::abs(det) <= math::kSmallNumber) {
      return Mat4::Identity;
    }

    const float invDet = 1.0f / det;

    Mat4 res(1.0f);
    res.cols[0] = Vec4((m11 * c5 - m12 * c4 + m13 * c3) * invDet, (-m01 * c5 + m02 * c4 - m03 * c3) * invDet,
                       (m31 * s5 - m32 * s4 + m33 * s3) * invDet, (-m21 * s5 + m22 * s4 - m23 * s3) * invDet);
    res.cols[1] = Vec4((-m10 * c5 + m12 * c2 - m13 * c1) * invDet, (m00 * c5 - m02 * c2 + m03 * c1) * invDet,
                       (-m30 * s5 + m32 * s2 - m33 * s1) * invDet, (m20 * s5 - m22 * s2 + m23 * s1) * invDet);
    res.cols[2] = Vec4((m10 * c4 - m11 * c2 + m13 * c0) * invDet, (-m00 * c4 + m01 * c2 - m03 * c0) * invDet,
                       (m30 * s4 - m31 * s2 + m33 * s0) * invDet, (-m20 * s4 + m21 * s2 - m23 * s0) * invDet);
    res.cols[3] = Vec4((-m10 * c3 + m11 * c1 - m12 * c0) * invDet, (m00 * c3 - m01 * c1 + m02 * c0) * invDet,
                       (-m30 * s3 + m31 * s1 - m32 * s0) * invDet, (m20 * s3 - m21 * s1 + m22 * s0) * invDet);
    return res;
  }

  inline bool equals(const Mat4& iOther, float iTolerance = math::kKindaSmallNumber) const {
    return cols[0].equals(iOther.cols[0], iTolerance) && cols[1].equals(iOther.cols[1], iTolerance) &&
           cols[2].equals(iOther.cols[2], iTolerance) && cols[3].equals(iOther.cols[3], iTolerance);
  }

  inline std::string toString() const {
    char buf[256];
    const auto& m = *this;
    std::snprintf(buf, sizeof(buf),
                  "Mat4(\n  [%.3f, %.3f, %.3f, %.3f]\n  [%.3f, %.3f, %.3f, %.3f]\n  [%.3f, %.3f, %.3f, %.3f]\n  [%.3f, %.3f, "
                  "%.3f, %.3f]\n)",
                  m[0][0], m[1][0], m[2][0], m[3][0], m[0][1], m[1][1], m[2][1], m[3][1], m[0][2], m[1][2], m[2][2], m[3][2],
                  m[0][3], m[1][3], m[2][3], m[3][3]);
    return std::string(buf);
  }
};

inline const Mat4 Mat4::Identity{1.0f};

} // namespace ne
