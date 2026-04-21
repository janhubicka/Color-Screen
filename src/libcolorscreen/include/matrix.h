#ifndef MATRIX_H
#define MATRIX_H
/* Basic template for square matrix.  */
#include <cassert>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include "base.h"

namespace colorscreen
{

/* Square matrix template.  */
template <typename T, size_t dim>
class matrix
{
public:
  static constexpr size_t m_dim = dim;
  typedef T Te[dim][dim];
  Te m_elements;

  /* Default constructor: build identity matrix.  */
  constexpr matrix()
  {
    for (size_t col = 0; col < m_dim; col++)
      for (size_t row = 0; row < m_dim; row++)
        m_elements[col][row] = (T)(row == col);
  }

  /* Support for Row, Column indexing.  Storage is column-major.  */
  always_inline_attr inline T&
  operator() (size_t row, size_t col)
  {
    return m_elements[col][row];
  }

  always_inline_attr inline const T&
  operator() (size_t row, size_t col) const
  {
    return m_elements[col][row];
  }

  /* Make matrix random (for unit testing).  */
  void
  randomize ()
  {
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < m_dim; i++)
        m_elements[j][i] = (T)(rand() % 100);
  }

  /* Copy constructor.  */
  constexpr matrix(const matrix &m)
  {
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < m_dim; i++)
        m_elements[j][i] = m.m_elements[j][i];
  }

  /* Usual matrix operations.  */
  inline matrix<T, dim>
  operator+ (const matrix<T, dim>& rhs) const
  {
    matrix<T, dim> ret;
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < m_dim; i++)
        ret.m_elements[j][i] = m_elements[j][i] + rhs.m_elements[j][i];
    return ret;
  }

  inline matrix<T, dim>
  operator* (const matrix<T, dim>& rhs) const
  {
    matrix<T, dim> ret;
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < m_dim; i++)
        {
          T a = 0;
          for (size_t k = 0; k < m_dim; k++)
            a += m_elements[k][i] * rhs.m_elements[j][k];
          ret.m_elements[j][i] = a;
        }
    return ret;
  }

  inline matrix<T, dim>
  operator* (const T rhs) const
  {
    matrix<T, dim> ret;
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < m_dim; i++)
        ret.m_elements[j][i] = m_elements[j][i] * rhs;
    return ret;
  }

  inline void
  transpose()
  {
    for (size_t j = 0; j < m_dim; j++)
      for (size_t i = 0; i < j; i++)
        std::swap (m_elements[j][i], m_elements[i][j]);
  }

  void
  print (FILE *f)
  {
    for (size_t row = 0; row < m_dim; row++)
      {
        for (size_t col = 0; col < m_dim; col++)
          fprintf (f, " %f", (double) (*this)(row, col));
        fprintf (f, "\n");
      }
  }
};

/* 2x2 matrix with inverse operation.  */
template<typename T>
class matrix2x2 : public matrix<T, 2>
{
public:
  using matrix<T, 2>::m_elements;
  using matrix<T, 2>::operator();

  /* Constructor with standard row-major input parameters.  */
  inline
  matrix2x2 (T e00, T e01,
             T e10, T e11)
  {
    (*this)(0, 0) = e00; (*this)(0, 1) = e01;
    (*this)(1, 0) = e10; (*this)(1, 1) = e11;
  }

  inline
  matrix2x2 ()
  {
  }

  inline
  matrix2x2& operator=(const matrix<T, 2>& rhs)
  {
    for (size_t col = 0; col < 2; col++)
      for (size_t row = 0; row < 2; row++)
        m_elements[col][row] = rhs.m_elements[col][row];
    return *this;
  }

  /* Matrix-vector multiplication.  */
  inline void
  apply_to_vector (T x, T y, T *xx, T *yy) const
  {
    *xx = x * (*this)(0, 0) + y * (*this)(0, 1);
    *yy = x * (*this)(1, 0) + y * (*this)(1, 1);
  }

  /* Compute inversion.  */
  pure_attr inline matrix2x2
  invert () const
  {
    T a = (*this)(0, 0);
    T b = (*this)(0, 1);
    T c = (*this)(1, 0);
    T d = (*this)(1, 1);
    T det_rec = (T) 1 / (a * d - b * c);
    return matrix2x2 (d * det_rec, -b * det_rec,
                      -c * det_rec, a * det_rec);
  }
};

/* 3x3 matrix with inverse operation.  */
template<typename T>
class matrix3x3 : public matrix<T, 3>
{
public:
  using matrix<T, 3>::m_elements;
  using matrix<T, 3>::operator();

  matrix3x3 () { }

  /* Constructor with standard row-major input parameters.  */
  inline
  matrix3x3 (T e00, T e01, T e02,
             T e10, T e11, T e12,
             T e20, T e21, T e22)
  {
    (*this)(0, 0) = e00; (*this)(0, 1) = e01; (*this)(0, 2) = e02;
    (*this)(1, 0) = e10; (*this)(1, 1) = e11; (*this)(1, 2) = e12;
    (*this)(2, 0) = e20; (*this)(2, 1) = e21; (*this)(2, 2) = e22;
  }

  inline
  matrix3x3<T>& operator=(const matrix<T, 3>& rhs)
  {
    for (size_t col = 0; col < 3; col++)
      for (size_t row = 0; row < 3; row++)
        m_elements[col][row] = rhs.m_elements[col][row];
    return *this;
  }

  /* Apply matrix to vector (x,y,1) and return its two elements.  */
  inline void
  apply (T x, T y, T *xx, T *yy) const
  {
    *xx = x * (*this)(0, 0) + y * (*this)(0, 1) + (*this)(0, 2);
    *yy = x * (*this)(1, 0) + y * (*this)(1, 1) + (*this)(1, 2);
  }

  /* Compute inversion.  */
  pure_attr inline matrix3x3
  invert () const
  {
    T a = (*this)(0, 0), b = (*this)(0, 1), c = (*this)(0, 2);
    T d = (*this)(1, 0), e = (*this)(1, 1), f = (*this)(1, 2);
    T g = (*this)(2, 0), h = (*this)(2, 1), i = (*this)(2, 2);

    T det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    T idet = (T) 1 / det;

    return matrix3x3 (
      (e * i - f * h) * idet, (c * h - b * i) * idet, (b * f - c * e) * idet,
      (f * g - d * i) * idet, (a * i - c * g) * idet, (c * d - a * f) * idet,
      (d * h - e * g) * idet, (b * g - a * h) * idet, (a * e - b * d) * idet
    );
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
template<typename T>
class matrix4x4 : public matrix<T, 4>
{
public:
  using matrix<T, 4>::m_elements;
  using matrix<T, 4>::operator();
  matrix4x4 () { }

  /* Constructor with standard row-major input parameters.  */
  inline
  matrix4x4 (T e00, T e01, T e02, T e03,
             T e10, T e11, T e12, T e13,
             T e20, T e21, T e22, T e23,
             T e30, T e31, T e32, T e33)
  {
    (*this)(0, 0) = e00; (*this)(0, 1) = e01; (*this)(0, 2) = e02; (*this)(0, 3) = e03;
    (*this)(1, 0) = e10; (*this)(1, 1) = e11; (*this)(1, 2) = e12; (*this)(1, 3) = e13;
    (*this)(2, 0) = e20; (*this)(2, 1) = e21; (*this)(2, 2) = e22; (*this)(2, 3) = e23;
    (*this)(3, 0) = e30; (*this)(3, 1) = e31; (*this)(3, 2) = e32; (*this)(3, 3) = e33;
  }

  inline
  matrix4x4<T>& operator=(const matrix<T, 4> &rhs)
  {
    for (size_t col = 0; col < 4; col++)
      for (size_t row = 0; row < 4; row++)
        m_elements[col][row] = rhs.m_elements[col][row];
    return *this;
  }

  /* Apply matrix to 2 dimensional coordinates and do perspective projection.
     Note: indexing is consistent with homography-matrix.C expectations.  */
  inline void
  perspective_transform (T x, T y, T &xr, T &yr) const
  {
    xr =   (x * (*this)(0, 0) + y * (*this)(0, 1) + (*this)(0, 2) + (*this)(0, 3))
         / (x * (*this)(2, 0) + y * (*this)(2, 1) + (*this)(2, 2) + (*this)(2, 3));
    yr =   (x * (*this)(1, 0) + y * (*this)(1, 1) + (*this)(1, 2) + (*this)(1, 3))
         / (x * (*this)(3, 0) + y * (*this)(3, 1) + (*this)(3, 2) + (*this)(3, 3));
  }

  /* Inverse transform for the operation above.  */
  inline void
  inverse_perspective_transform (T x, T y, T &xr, T &yr) const
  {
    matrix2x2<T> m ((*this)(0, 0) - (*this)(2, 0) * x,
                    (*this)(1, 0) - (*this)(3, 0) * y,
                    (*this)(0, 1) - (*this)(2, 1) * x,
                    (*this)(1, 1) - (*this)(3, 1) * y);
    matrix2x2<T> m2 = m.invert ();
    T xx = ((*this)(2, 2) + (*this)(2, 3)) * x - (*this)(0, 2) - (*this)(0, 3);
    T yy = ((*this)(3, 2) + (*this)(3, 3)) * y - (*this)(1, 2) - (*this)(1, 3);

    xr = m2(0, 0) * xx + m2(1, 0) * yy;
    yr = m2(0, 1) * xx + m2(1, 1) * yy;
  }

  inline void
  scale_channels (T r, T g, T b)
  {
    for (size_t j = 0; j < 4; j++)
    {
      (*this)(j, 0) *= r;
      (*this)(j, 1) *= g;
      (*this)(j, 2) *= b;
    }
  }

  inline void
  verify_last_row_0001()
  {
    assert ((*this)(3, 0) == 0);
    assert ((*this)(3, 1) == 0);
    assert ((*this)(3, 2) == 0);
    assert ((*this)(3, 3) == 1);
  }

  /* Matrix-vector multiplication used for RGB values.  */
  always_inline_attr inline void
  apply_to_rgb (T r, T g, T b, T *rr, T *gg, T *bb) const
  {
    *rr = r * (*this)(0, 0) + g * (*this)(0, 1) + b * (*this)(0, 2) + (*this)(0, 3);
    *gg = r * (*this)(1, 0) + g * (*this)(1, 1) + b * (*this)(1, 2) + (*this)(1, 3);
    *bb = r * (*this)(2, 0) + g * (*this)(2, 1) + b * (*this)(2, 2) + (*this)(2, 3);
  }

  /* This adjust the profile so grayscale has given r,g,b values.
     If RW is non-null return scale weights for individual columns of the matrix.  */
  inline void
  normalize_grayscale (T r = 1, T g = 1, T b = 1, T *rw = nullptr, T *gw = nullptr, T *bw = nullptr)
  {
    matrix4x4 inv = invert ();
    T ivec[4] = {r, g, b, 1};
    T vec[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < 4; j++)
        vec[i] += ivec[j] * inv(i, j);

    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < 3; j++)
        (*this)(j, i) *= vec[i];

    if (rw != nullptr)
      {
        *rw = vec[0];
        *gw = vec[1];
        *bw = vec[2];
      }
  }
  inline void
  normalize_xyz_brightness ()
  {
    T sum = (*this)(1, 0) + (*this)(1, 1) + (*this)(1, 2);
    T mul = (T) 1 / sum;
    for (size_t i = 0; i < 3; i++)
      for (size_t j = 0; j < 3; j++)
        (*this)(i, j) *= mul;
  }

  /* Compute inversion.  */
  pure_attr inline matrix4x4
  invert () const
  {
    T A2323 = (*this)(2, 2) * (*this)(3, 3) - (*this)(2, 3) * (*this)(3, 2);
    T A1323 = (*this)(2, 1) * (*this)(3, 3) - (*this)(2, 3) * (*this)(3, 1);
    T A1223 = (*this)(2, 1) * (*this)(3, 2) - (*this)(2, 2) * (*this)(3, 1);
    T A0323 = (*this)(2, 0) * (*this)(3, 3) - (*this)(2, 3) * (*this)(3, 0);
    T A0223 = (*this)(2, 0) * (*this)(3, 2) - (*this)(2, 2) * (*this)(3, 0);
    T A0123 = (*this)(2, 0) * (*this)(3, 1) - (*this)(2, 1) * (*this)(3, 0);
    T A2313 = (*this)(1, 2) * (*this)(3, 3) - (*this)(1, 3) * (*this)(3, 2);
    T A1313 = (*this)(1, 1) * (*this)(3, 3) - (*this)(1, 3) * (*this)(3, 1);
    T A1213 = (*this)(1, 1) * (*this)(3, 2) - (*this)(1, 2) * (*this)(3, 1);
    T A2312 = (*this)(1, 2) * (*this)(2, 3) - (*this)(1, 3) * (*this)(2, 2);
    T A1312 = (*this)(1, 1) * (*this)(2, 3) - (*this)(1, 3) * (*this)(2, 1);
    T A1212 = (*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1);
    T A0313 = (*this)(1, 0) * (*this)(3, 3) - (*this)(1, 3) * (*this)(3, 0);
    T A0213 = (*this)(1, 0) * (*this)(3, 2) - (*this)(1, 2) * (*this)(3, 0);
    T A0312 = (*this)(1, 0) * (*this)(2, 3) - (*this)(1, 3) * (*this)(2, 0);
    T A0212 = (*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0);
    T A0113 = (*this)(1, 0) * (*this)(3, 1) - (*this)(1, 1) * (*this)(3, 0);
    T A0112 = (*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0);

    T det = (*this)(0, 0) * ((*this)(1, 1) * A2323 - (*this)(1, 2) * A1323 + (*this)(1, 3) * A1223)
            - (*this)(0, 1) * ((*this)(1, 0) * A2323 - (*this)(1, 2) * A0323 + (*this)(1, 3) * A0223)
            + (*this)(0, 2) * ((*this)(1, 0) * A1323 - (*this)(1, 1) * A0323 + (*this)(1, 3) * A0123)
            - (*this)(0, 3) * ((*this)(1, 0) * A1223 - (*this)(1, 1) * A0223 + (*this)(1, 2) * A0123);
    det = (T) 1 / det;

    matrix4x4 ret;
    ret(0, 0) = det * ((*this)(1, 1) * A2323 - (*this)(1, 2) * A1323 + (*this)(1, 3) * A1223);
    ret(1, 0) = det * -((*this)(1, 0) * A2323 - (*this)(1, 2) * A0323 + (*this)(1, 3) * A0223);
    ret(2, 0) = det * ((*this)(1, 0) * A1323 - (*this)(1, 1) * A0323 + (*this)(1, 3) * A0123);
    ret(3, 0) = det * -((*this)(1, 0) * A1223 - (*this)(1, 1) * A0223 + (*this)(1, 2) * A0123);

    ret(0, 1) = det * -((*this)(0, 1) * A2323 - (*this)(0, 2) * A1323 + (*this)(0, 3) * A1223);
    ret(1, 1) = det * ((*this)(0, 0) * A2323 - (*this)(0, 2) * A0323 + (*this)(0, 3) * A0223);
    ret(2, 1) = det * -((*this)(0, 0) * A1323 - (*this)(0, 1) * A0323 + (*this)(0, 3) * A0123);
    ret(3, 1) = det * ((*this)(0, 0) * A1223 - (*this)(0, 1) * A0223 + (*this)(0, 2) * A0123);

    ret(0, 2) = det * ((*this)(0, 1) * A2313 - (*this)(0, 2) * A1313 + (*this)(0, 3) * A1213);
    ret(1, 2) = det * -((*this)(0, 0) * A2313 - (*this)(0, 2) * A0313 + (*this)(0, 3) * A0213);
    ret(2, 2) = det * ((*this)(0, 0) * A1313 - (*this)(0, 1) * A0313 + (*this)(0, 3) * A0113);
    ret(3, 2) = det * -((*this)(0, 0) * A1213 - (*this)(0, 1) * A0213 + (*this)(0, 2) * A0113);

    ret(0, 3) = det * -((*this)(0, 1) * A2312 - (*this)(0, 2) * A1312 + (*this)(0, 3) * A1212);
    ret(1, 3) = det * ((*this)(0, 0) * A2312 - (*this)(0, 2) * A0312 + (*this)(0, 3) * A0212);
    ret(2, 3) = det * -((*this)(0, 0) * A1312 - (*this)(0, 1) * A0312 + (*this)(0, 3) * A0112);
    ret(3, 3) = det * ((*this)(0, 0) * A1212 - (*this)(0, 1) * A0212 + (*this)(0, 2) * A0112);
    return ret;
  }
};
}
#endif
