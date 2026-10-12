#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "core/math/math.h"

namespace ne::test {

NE_TEST_CASE("mat4", "Mat4 Identity & Basic Operations") {
  Mat4 defMat;
  NE_TEST_ASSERT(defMat.equals(Mat4::Identity), "Default Mat4 constructor must equal Mat4::Identity.");

  Mat4 scalarMat(0.0f);
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      NE_TEST_ASSERT(math::equals(scalarMat[c][r], 0.0f), "Mat4(0.0f) elements must all be zero.");
    }
  }

  NE_TEST_ASSERT(defMat.toString().find("Mat4") != std::string::npos, "Mat4 toString formatting check.");
}

NE_TEST_CASE("mat4", "Mat4 Matrix-Vector & Matrix-Matrix Multiplication") {
  Mat4 a(Vec4(1.0f, 2.0f, 0.0f, 0.0f), Vec4(0.0f, 1.0f, 0.0f, 0.0f), Vec4(0.0f, 0.0f, 1.0f, 0.0f), Vec4(3.0f, 4.0f, 5.0f, 1.0f));

  Vec4 v(2.0f, 1.0f, 0.0f, 1.0f);
  Vec4 transformed = a * v;
  // x = 1*2 + 0*1 + 0*0 + 3*1 = 5
  // y = 2*2 + 1*1 + 0*0 + 4*1 = 9
  // z = 0*2 + 0*1 + 1*0 + 5*1 = 5
  // w = 0*2 + 0*1 + 0*0 + 1*1 = 1
  NE_TEST_ASSERT(transformed.equals(Vec4(5.0f, 9.0f, 5.0f, 1.0f)), "Mat4 * Vec4 product check.");

  Mat4 identityTest = a * Mat4::Identity;
  NE_TEST_ASSERT(identityTest.equals(a), "Mat4 * Identity == Mat4.");
}

NE_TEST_CASE("mat4", "Mat4 Transposition") {
  Mat4 m(Vec4(1.0f, 2.0f, 3.0f, 4.0f), Vec4(5.0f, 6.0f, 7.0f, 8.0f), Vec4(9.0f, 10.0f, 11.0f, 12.0f),
         Vec4(13.0f, 14.0f, 15.0f, 16.0f));

  Mat4 mT = m.transposed();
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      NE_TEST_ASSERT(math::equals(mT[c][r], m[r][c]), "Mat4::transposed swaps rows and columns.");
    }
  }

  NE_TEST_ASSERT(mT.transposed().equals(m), "(M^T)^T == M.");
}

NE_TEST_CASE("mat4", "Mat4 Inversion & Identity Invariant") {
  Mat4 m(Vec4(2.0f, 0.0f, 0.0f, 0.0f), Vec4(0.0f, 3.0f, 0.0f, 0.0f), Vec4(0.0f, 0.0f, 4.0f, 0.0f), Vec4(5.0f, 6.0f, 7.0f, 1.0f));

  Mat4 invM = m.inversed();

  Mat4 identity1 = m * invM;
  Mat4 identity2 = invM * m;

  NE_TEST_ASSERT(identity1.equals(Mat4::Identity, 1e-4f), "m * m.inversed() == Identity.");
  NE_TEST_ASSERT(identity2.equals(Mat4::Identity, 1e-4f), "m.inversed() * m == Identity.");
}

NE_TEST_CASE("mat4", "Mat4 Memory Layout & POD Properties") {
  static_assert(sizeof(Mat4) == 64, "Mat4 must be 64 bytes in size.");
  static_assert(std::is_standard_layout_v<Mat4>, "Mat4 must be standard layout.");

  constexpr Mat4 constDiag(Vec4(2.0f, 0.0f, 0.0f, 0.0f), Vec4(0.0f, 2.0f, 0.0f, 0.0f), Vec4(0.0f, 0.0f, 2.0f, 0.0f),
                           Vec4(0.0f, 0.0f, 0.0f, 1.0f));
  constexpr Mat4 constInv = constDiag.inversed();
  static_assert(constInv.cols[0].x == 0.5f, "Mat4 constexpr inverse test.");
  constexpr Mat4 constT = constDiag.transposed();
  static_assert(constT.cols[0].x == 2.0f, "Mat4 constexpr transpose test.");

  NE_TEST_ASSERT(sizeof(Mat4) == 64, "Mat4 sizeof check.");
  NE_TEST_ASSERT(std::is_standard_layout_v<Mat4>, "Mat4 standard layout check.");

  Mat4 m;
  NE_TEST_ASSERT(m.data() == &m[0][0], "Mat4 data() must point to column 0 row 0.");
}

NE_TEST_CASE("mat4", "Mat4 Singular Inversion Fallback") {
  // Degenerate matrix with zero determinant
  Mat4 singular(0.0f);
  Mat4 invSingular = singular.inversed();
  NE_TEST_ASSERT(invSingular.equals(Mat4::Identity), "Inverting singular matrix must safely fallback to Identity.");
}

} // namespace ne::test

#endif
