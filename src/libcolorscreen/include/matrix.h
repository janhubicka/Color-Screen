/* Square matrix template.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef MATRIX_H
#define MATRIX_H

#include <cassert>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <type_traits>
#include "base.h"

namespace colorscreen
{

/* Square matrix template.
   DIM is the dimension of the matrix.  */
template <typename T, size_t dim>
class matrix
{
public:
  static constexpr size_t m_dim = dim;

  /* Default constructor: build identity matrix.  */
  constexpr matrix () : m_elements{}
  {
    for (size_t i = 0; i < m_dim; i++)
      m_elements[i][i] = (T) 1;
  }

  /* Variadic constructor to initialize all elements in row-major order.
     ARGS are the elements of the matrix.  */
  template <typename... Args, typename = std::enable_if_t<sizeof... (Args) == dim * dim>>
  constexpr matrix (Args... args) : m_elements{}
  {
    T values[] = { static_cast<T> (args)... };
    for (size_t i = 0; i < dim; i++)
      for (size_t j = 0; j < dim; j++)
        m_elements[i][j] = values[i * dim + j];
  }

  /* Support for Row, Column indexing.  Storage is row-major.
     ROW is the row index, COL is the column index.  */
  always_inline_attr inline T &
  operator() (size_t row, size_t col) noexcept
  {
    return m_elements[row][col];
  }

  /* Support for Row, Column indexing.  Storage is row-major.
     ROW is the row index, COL is the column index.  */
  always_inline_attr inline const T &
  operator() (size_t row, size_t col) const noexcept
  {
    return m_elements[row][col];
  }

  /* Make matrix random (for unit testing).  */
  void
  randomize ()
  {
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < m_dim; j++)
        m_elements[i][j] = (T) (rand () % 100);
  }

  /* Copy constructor.  M is the matrix to copy from.  */
  constexpr matrix (const matrix &m) : m_elements{}
  {
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < m_dim; j++)
        m_elements[i][j] = m.m_elements[i][j];
  }

  /* Add two matrices.  RHS is the matrix to add.  */
  pure_attr inline matrix<T, dim>
  operator+ (const matrix<T, dim> &rhs) const noexcept
  {
    matrix<T, dim> ret;
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < m_dim; j++)
        ret (i, j) = (*this) (i, j) + rhs (i, j);
    return ret;
  }

  /* Multiply two matrices.  RHS is the matrix to multiply with.  */
  pure_attr inline matrix<T, dim>
  operator* (const matrix<T, dim> &rhs) const noexcept
  {
    matrix<T, dim> ret;
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < m_dim; j++)
        {
          T a = 0;
          for (size_t k = 0; k < m_dim; k++)
            a += (*this) (i, k) * rhs (k, j);
          ret (i, j) = a;
        }
    return ret;
  }

  /* Multiply matrix by a scalar.  RHS is the scalar to multiply with.  */
  pure_attr inline matrix<T, dim>
  operator* (const T rhs) const noexcept
  {
    matrix<T, dim> ret;
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < m_dim; j++)
        ret (i, j) = (*this) (i, j) * rhs;
    return ret;
  }

  /* Transpose the matrix in-place.  */
  inline void
  transpose () noexcept
  {
    for (size_t i = 0; i < m_dim; i++)
      for (size_t j = 0; j < i; j++)
        std::swap (m_elements[i][j], m_elements[j][i]);
  }

  /* Print the matrix to a file.  F is the file pointer.  */
  void
  print (FILE *f) const
  {
    for (size_t row = 0; row < m_dim; row++)
      {
        for (size_t col = 0; col < m_dim; col++)
          fprintf (f, " %f", (double) (*this) (row, col));
        fprintf (f, "\n");
      }
  }

protected:
  T m_elements[dim][dim];
};

/* 2x2 matrix with inverse operation.  */
template <typename T>
class matrix2x2 : public matrix<T, 2>
{
public:
  using matrix<T, 2>::matrix;
  using matrix<T, 2>::operator();
  using matrix<T, 2>::operator=;

  /* Default constructor: build identity matrix.  */
  constexpr matrix2x2 () : matrix<T, 2> () {}
  /* Construct from base matrix.  RHS is the matrix to copy from.  */
  constexpr matrix2x2 (const matrix<T, 2> &rhs) : matrix<T, 2> (rhs) {}

  /* Assign from base matrix.  RHS is the matrix to copy from.  */
  matrix2x2 &
  operator= (const matrix<T, 2> &rhs) noexcept
  {
    matrix<T, 2>::operator= (rhs);
    return *this;
  }

  /* Add two matrices.  RHS is the matrix to add.  */
  pure_attr inline matrix2x2
  operator+ (const matrix<T, 2> &rhs) const noexcept
  {
    return matrix2x2 (matrix<T, 2>::operator+ (rhs));
  }

  /* Multiply two matrices.  RHS is the matrix to multiply with.  */
  pure_attr inline matrix2x2
  operator* (const matrix<T, 2> &rhs) const noexcept
  {
    return matrix2x2 (matrix<T, 2>::operator* (rhs));
  }

  /* Multiply matrix by a scalar.  RHS is the scalar to multiply with.  */
  pure_attr inline matrix2x2
  operator* (const T rhs) const noexcept
  {
    return matrix2x2 (matrix<T, 2>::operator* (rhs));
  }

  /* Matrix-vector multiplication.  X and Y are the vector coordinates,
     XX and YY are the result coordinates.  */
  inline void
  apply_to_vector (T x, T y, T *xx, T *yy) const noexcept
  {
    *xx = x * (*this) (0, 0) + y * (*this) (0, 1);
    *yy = x * (*this) (1, 0) + y * (*this) (1, 1);
  }

  /* Matrix-vector multiplication.  P is the point to transform.  */
  pure_attr inline point_t
  apply_to_vector (point_t p) const noexcept
  {
    return { (coord_t) (p.x * (*this) (0, 0) + p.y * (*this) (0, 1)),
             (coord_t) (p.x * (*this) (1, 0) + p.y * (*this) (1, 1)) };
  }

  /* Compute inversion.  */
  pure_attr inline matrix2x2
  invert () const noexcept
  {
    T a = (*this) (0, 0);
    T b = (*this) (0, 1);
    T c = (*this) (1, 0);
    T d = (*this) (1, 1);
    T det_rec = (T) 1 / (a * d - b * c);
    return matrix2x2 (d * det_rec, -b * det_rec, -c * det_rec, a * det_rec);
  }
};

/* 3x3 matrix with inverse operation.  */
template <typename T>
class matrix3x3 : public matrix<T, 3>
{
public:
  using matrix<T, 3>::matrix;
  using matrix<T, 3>::operator();
  using matrix<T, 3>::operator=;

  /* Default constructor: build identity matrix.  */
  constexpr matrix3x3 () : matrix<T, 3> () {}
  /* Construct from base matrix.  RHS is the matrix to copy from.  */
  constexpr matrix3x3 (const matrix<T, 3> &rhs) : matrix<T, 3> (rhs) {}

  /* Assign from base matrix.  RHS is the matrix to copy from.  */
  matrix3x3 &
  operator= (const matrix<T, 3> &rhs) noexcept
  {
    matrix<T, 3>::operator= (rhs);
    return *this;
  }

  /* Add two matrices.  RHS is the matrix to add.  */
  pure_attr inline matrix3x3
  operator+ (const matrix<T, 3> &rhs) const noexcept
  {
    return matrix3x3 (matrix<T, 3>::operator+ (rhs));
  }

  /* Multiply two matrices.  RHS is the matrix to multiply with.  */
  pure_attr inline matrix3x3
  operator* (const matrix<T, 3> &rhs) const noexcept
  {
    return matrix3x3 (matrix<T, 3>::operator* (rhs));
  }

  /* Multiply matrix by a scalar.  RHS is the scalar to multiply with.  */
  pure_attr inline matrix3x3
  operator* (const T rhs) const noexcept
  {
    return matrix3x3 (matrix<T, 3>::operator* (rhs));
  }

  /* Apply matrix to vector (x,y,1) and return its two elements.  X and Y are the
     vector coordinates, XX and YY are the result coordinates.  */
  inline void
  apply (T x, T y, T *xx, T *yy) const noexcept
  {
    *xx = x * (*this) (0, 0) + y * (*this) (0, 1) + (*this) (0, 2);
    *yy = x * (*this) (1, 0) + y * (*this) (1, 1) + (*this) (1, 2);
  }

  /* Apply matrix to point (x,y,1) and return the resulting point.
     P is the point to transform.  */
  pure_attr inline point_t
  apply (point_t p) const noexcept
  {
    return { (coord_t) (p.x * (*this) (0, 0) + p.y * (*this) (0, 1) + (*this) (0, 2)),
             (coord_t) (p.x * (*this) (1, 0) + p.y * (*this) (1, 1) + (*this) (1, 2)) };
  }

  /* Compute inversion.  */
  pure_attr inline matrix3x3
  invert () const noexcept
  {
    T a = (*this) (0, 0), b = (*this) (0, 1), c = (*this) (0, 2);
    T d = (*this) (1, 0), e = (*this) (1, 1), f = (*this) (1, 2);
    T g = (*this) (2, 0), h = (*this) (2, 1), i = (*this) (2, 2);

    T det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    T idet = (T) 1 / det;

    return matrix3x3 ((e * i - f * h) * idet, (c * h - b * i) * idet, (b * f - c * e) * idet,
                      (f * g - d * i) * idet, (a * i - c * g) * idet, (c * d - a * f) * idet,
                      (d * h - e * g) * idet, (b * g - a * h) * idet, (a * e - b * d) * idet);
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
template <typename T>
class matrix4x4 : public matrix<T, 4>
{
public:
  using matrix<T, 4>::matrix;
  using matrix<T, 4>::operator();
  using matrix<T, 4>::operator=;

  /* Default constructor: build identity matrix.  */
  constexpr matrix4x4 () : matrix<T, 4> () {}
  /* Construct from base matrix.  RHS is the matrix to copy from.  */
  constexpr matrix4x4 (const matrix<T, 4> &rhs) : matrix<T, 4> (rhs) {}

  /* Assign from base matrix.  RHS is the matrix to copy from.  */
  matrix4x4 &
  operator= (const matrix<T, 4> &rhs) noexcept
  {
    matrix<T, 4>::operator= (rhs);
    return *this;
  }

  /* Add two matrices.  RHS is the matrix to add.  */
  pure_attr inline matrix4x4
  operator+ (const matrix<T, 4> &rhs) const noexcept
  {
    return matrix4x4 (matrix<T, 4>::operator+ (rhs));
  }

  /* Multiply two matrices.  RHS is the matrix to multiply with.  */
  pure_attr inline matrix4x4
  operator* (const matrix<T, 4> &rhs) const noexcept
  {
    return matrix4x4 (matrix<T, 4>::operator* (rhs));
  }

  /* Multiply matrix by a scalar.  RHS is the scalar to multiply with.  */
  pure_attr inline matrix4x4
  operator* (const T rhs) const noexcept
  {
    return matrix4x4 (matrix<T, 4>::operator* (rhs));
  }



  /* Apply matrix to point and do perspective projection.
     P is the point to transform.  */
  pure_attr inline point_t
  perspective_transform (point_t p) const noexcept
  {
    T xr = (T) ((p.x * (*this) (0, 0) + p.y * (*this) (0, 1) + (*this) (0, 2) + (*this) (0, 3))
                / (p.x * (*this) (2, 0) + p.y * (*this) (2, 1) + (*this) (2, 2) + (*this) (2, 3)));
    T yr = (T) ((p.x * (*this) (1, 0) + p.y * (*this) (1, 1) + (*this) (1, 2) + (*this) (1, 3))
                / (p.x * (*this) (3, 0) + p.y * (*this) (3, 1) + (*this) (3, 2) + (*this) (3, 3)));
    return { (coord_t) xr, (coord_t) yr };
  }



  /* Inverse transform for point.  P is the point to transform.  */
  pure_attr inline point_t
  inverse_perspective_transform (point_t p) const noexcept
  {
    matrix2x2<T> m ((*this) (0, 0) - (*this) (2, 0) * (T) p.x,
                    (*this) (1, 0) - (*this) (3, 0) * (T) p.y,
                    (*this) (0, 1) - (*this) (2, 1) * (T) p.x,
                    (*this) (1, 1) - (*this) (3, 1) * (T) p.y);
    matrix2x2<T> m2 = m.invert ();
    T xx = ((*this) (2, 2) + (*this) (2, 3)) * (T) p.x - (*this) (0, 2) - (*this) (0, 3);
    T yy = ((*this) (3, 2) + (*this) (3, 3)) * (T) p.y - (*this) (1, 2) - (*this) (1, 3);

    return { (coord_t) (m2 (0, 0) * xx + m2 (1, 0) * yy),
             (coord_t) (m2 (0, 1) * xx + m2 (1, 1) * yy) };
  }

  /* Scale individual color channels.  R, G, and B are the scale factors.  */
  inline void
  scale_channels (T r, T g, T b) noexcept
  {
    for (size_t i = 0; i < 4; i++)
      {
        (*this) (i, 0) *= r;
        (*this) (i, 1) *= g;
        (*this) (i, 2) *= b;
      }
  }

  /* Assert that the last row is (0, 0, 0, 1).  */
  inline void
  verify_last_row_0001 () const noexcept
  {
    assert ((*this) (3, 0) == 0);
    assert ((*this) (3, 1) == 0);
    assert ((*this) (3, 2) == 0);
    assert ((*this) (3, 3) == 1);
  }

  /* Matrix-vector multiplication used for RGB values.  R, G, and B are the
     input values, RR, GG, and BB are the result values.  */
  always_inline_attr inline void
  apply_to_rgb (T r, T g, T b, T *rr, T *gg, T *bb) const noexcept
  {
    *rr = r * (*this) (0, 0) + g * (*this) (0, 1) + b * (*this) (0, 2) + (*this) (0, 3);
    *gg = r * (*this) (1, 0) + g * (*this) (1, 1) + b * (*this) (1, 2) + (*this) (1, 3);
    *bb = r * (*this) (2, 0) + g * (*this) (2, 1) + b * (*this) (2, 2) + (*this) (2, 3);
  }

  /* Apply matrix to rgbdata structure.  C is the color to transform.  */
  template <typename RGBType>
  always_inline_attr inline RGBType
  apply_to_rgb (const RGBType &c) const noexcept
  {
    return { (T) (c.red * (*this) (0, 0) + c.green * (*this) (0, 1) + c.blue * (*this) (0, 2)
                  + (*this) (0, 3)),
             (T) (c.red * (*this) (1, 0) + c.green * (*this) (1, 1) + c.blue * (*this) (1, 2)
                  + (*this) (1, 3)),
             (T) (c.red * (*this) (2, 0) + c.green * (*this) (2, 1) + c.blue * (*this) (2, 2)
                  + (*this) (2, 3)) };
  }

  /* Apply matrix to xyz structure.  C is the color to transform.  */
  template <typename XYZType>
  always_inline_attr inline XYZType
  apply_to_xyz (const XYZType &c) const noexcept
  {
    return { (T) (c.x * (*this) (0, 0) + c.y * (*this) (0, 1) + c.z * (*this) (0, 2) + (*this) (0, 3)),
             (T) (c.x * (*this) (1, 0) + c.y * (*this) (1, 1) + c.z * (*this) (1, 2) + (*this) (1, 3)),
             (T) (c.x * (*this) (2, 0) + c.y * (*this) (2, 1) + c.z * (*this) (2, 2)
                  + (*this) (2, 3)) };
  }

  /* This adjust the profile so grayscale has given r,g,b values.  If RW is
     non-null return scale weights for individual columns of the matrix.  R, G,
     and B are the target grayscale values, RW, GW, and BW are the result scale
     factors.  */
  inline void
  normalize_grayscale (T r = 1, T g = 1, T b = 1, T *rw = nullptr, T *gw = nullptr,
                       T *bw = nullptr)
  {
    matrix4x4 inv = invert ();
    T ivec[4] = { r, g, b, 1 };
    T vec[4] = { 0, 0, 0, 0 };
    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < 4; j++)
        vec[i] += ivec[j] * inv (i, j);

    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < 3; j++)
        (*this) (j, i) *= vec[i];

    if (rw != nullptr)
      {
        *rw = vec[0];
        *gw = vec[1];
        *bw = vec[2];
      }
  }

  /* Normalize the matrix so the XYZ brightness (Y channel) of white is 1.  */
  inline void
  normalize_xyz_brightness () noexcept
  {
    T sum = (*this) (1, 0) + (*this) (1, 1) + (*this) (1, 2);
    T mul = (T) 1 / sum;
    for (size_t i = 0; i < 3; i++)
      for (size_t j = 0; j < 3; j++)
        (*this) (i, j) *= mul;
  }

  /* Compute inversion.  */
  pure_attr inline matrix4x4
  invert () const noexcept
  {
    T A2323 = (*this) (2, 2) * (*this) (3, 3) - (*this) (2, 3) * (*this) (3, 2);
    T A1323 = (*this) (2, 1) * (*this) (3, 3) - (*this) (2, 3) * (*this) (3, 1);
    T A1223 = (*this) (2, 1) * (*this) (3, 2) - (*this) (2, 2) * (*this) (3, 1);
    T A0323 = (*this) (2, 0) * (*this) (3, 3) - (*this) (2, 3) * (*this) (3, 0);
    T A0223 = (*this) (2, 0) * (*this) (3, 2) - (*this) (2, 2) * (*this) (3, 0);
    T A0123 = (*this) (2, 0) * (*this) (3, 1) - (*this) (2, 1) * (*this) (3, 0);
    T A2313 = (*this) (1, 2) * (*this) (3, 3) - (*this) (1, 3) * (*this) (3, 2);
    T A1313 = (*this) (1, 1) * (*this) (3, 3) - (*this) (1, 3) * (*this) (3, 1);
    T A1213 = (*this) (1, 1) * (*this) (3, 2) - (*this) (1, 2) * (*this) (3, 1);
    T A2312 = (*this) (1, 2) * (*this) (2, 3) - (*this) (1, 3) * (*this) (2, 2);
    T A1312 = (*this) (1, 1) * (*this) (2, 3) - (*this) (1, 3) * (*this) (2, 1);
    T A1212 = (*this) (1, 1) * (*this) (2, 2) - (*this) (1, 2) * (*this) (2, 1);
    T A0313 = (*this) (1, 0) * (*this) (3, 3) - (*this) (1, 3) * (*this) (3, 0);
    T A0213 = (*this) (1, 0) * (*this) (3, 2) - (*this) (1, 2) * (*this) (3, 0);
    T A0312 = (*this) (1, 0) * (*this) (2, 3) - (*this) (1, 3) * (*this) (2, 0);
    T A0212 = (*this) (1, 0) * (*this) (2, 2) - (*this) (1, 2) * (*this) (2, 0);
    T A0113 = (*this) (1, 0) * (*this) (3, 1) - (*this) (1, 1) * (*this) (3, 0);
    T A0112 = (*this) (1, 0) * (*this) (2, 1) - (*this) (1, 1) * (*this) (2, 0);

    T det = (*this) (0, 0) * ((*this) (1, 1) * A2323 - (*this) (1, 2) * A1323 + (*this) (1, 3) * A1223)
            - (*this) (0, 1) * ((*this) (1, 0) * A2323 - (*this) (1, 2) * A0323 + (*this) (1, 3) * A0223)
            + (*this) (0, 2) * ((*this) (1, 0) * A1323 - (*this) (1, 1) * A0323 + (*this) (1, 3) * A0123)
            - (*this) (0, 3) * ((*this) (1, 0) * A1223 - (*this) (1, 1) * A0223 + (*this) (1, 2) * A0123);
    det = (T) 1 / det;

    matrix4x4 ret;
    ret (0, 0) = det * ((*this) (1, 1) * A2323 - (*this) (1, 2) * A1323 + (*this) (1, 3) * A1223);
    ret (1, 0) = det * -((*this) (1, 0) * A2323 - (*this) (1, 2) * A0323 + (*this) (1, 3) * A0223);
    ret (2, 0) = det * ((*this) (1, 0) * A1323 - (*this) (1, 1) * A0323 + (*this) (1, 3) * A0123);
    ret (3, 0) = det * -((*this) (1, 0) * A1223 - (*this) (1, 1) * A0223 + (*this) (1, 2) * A0123);

    ret (0, 1) = det * -((*this) (0, 1) * A2323 - (*this) (0, 2) * A1323 + (*this) (0, 3) * A1223);
    ret (1, 1) = det * ((*this) (0, 0) * A2323 - (*this) (0, 2) * A0323 + (*this) (0, 3) * A0223);
    ret (2, 1) = det * -((*this) (0, 0) * A1323 - (*this) (0, 1) * A0323 + (*this) (0, 3) * A0123);
    ret (3, 1) = det * ((*this) (0, 0) * A1223 - (*this) (0, 1) * A0223 + (*this) (0, 2) * A0123);

    ret (0, 2) = det * ((*this) (0, 1) * A2313 - (*this) (0, 2) * A1313 + (*this) (0, 3) * A1213);
    ret (1, 2) = det * -((*this) (0, 0) * A2313 - (*this) (0, 2) * A0313 + (*this) (0, 3) * A0213);
    ret (2, 2) = det * ((*this) (0, 0) * A1313 - (*this) (0, 1) * A0313 + (*this) (0, 3) * A0113);
    ret (3, 2) = det * -((*this) (0, 0) * A1213 - (*this) (0, 1) * A0213 + (*this) (0, 2) * A0113);

    ret (0, 3) = det * -((*this) (0, 1) * A2312 - (*this) (0, 2) * A1312 + (*this) (0, 3) * A1212);
    ret (1, 3) = det * ((*this) (0, 0) * A2312 - (*this) (0, 2) * A0312 + (*this) (0, 3) * A0212);
    ret (2, 3) = det * -((*this) (0, 0) * A1312 - (*this) (0, 1) * A0312 + (*this) (0, 3) * A0112);
    ret (3, 3) = det * ((*this) (0, 0) * A1212 - (*this) (0, 1) * A0212 + (*this) (0, 2) * A0112);
    return ret;
  }
};
}
#endif
