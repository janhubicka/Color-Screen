#ifndef MATRIX_H
#define MATRIX_H
/* Basic template for square matrix.  */
#include <algorithm>
#include <cstring>
#include <stdio.h>

#define flatten_attr __attribute__ ((__flatten__))

/* Square matrix template.  */
template <typename T, size_t dim>
class matrix
{
public:
  static const int m_dim = dim;
  T m_elements[m_dim][m_dim];

  /* Default constructor: build identity matrix.  */
  inline
  matrix()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=(T) (i==j);
  }

  /* Make matrix random (for unit testing).  */
  void
  randomize ()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=rand()%100;
  }

  /* Copy constructor.  */
  inline
  matrix(const matrix &m)
  {
    memcpy(m_elements,m.m_elements,sizeof (m_elements));
  }

  /* Usual matrix operations.  */
  inline matrix<T, dim>
  operator+ (const matrix<T, dim>& rhs) const
  {
    matrix<T, dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	ret.m_elements[j][i] = m_elements[j][i] + rhs.m_elements[j][i];
    return ret;
  }

  inline matrix<T, dim>
  operator* (const matrix<T, dim>& rhs) const
  {
    matrix<T, dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	{
	  T a = 0;
	  /*for (int k = 0; k < m_dim; k++)
	    a += m_elements[j][k] * rhs.m_elements[k][i];
	  ret.m_elements[j][i] = a;*/
	  for (int k = 0; k < m_dim; k++)
	    a += m_elements[k][i] * rhs.m_elements[j][k];
	  ret.m_elements[j][i] = a;
	}
    return ret;
  }

  inline matrix<T, dim>
  operator* (const T rhs) const
  {
    matrix<T, dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	ret.m_elements[j][i] = m_elements[j][i] * rhs;
    return ret;
  }

  inline void
  transpose()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < j; i++)
	std::swap (m_elements[j][i], m_elements[i][j]);
  }

  void
  print (FILE *f)
  {
    for (int j = 0; j < m_dim; j++)
      {
        for (int i = 0; i < m_dim; i++)
	  fprintf (f, " %f", (double) m_elements[i][j]);
	fprintf (f, "\n");
      }
  }
};

/* 2x2 matrix with inverse opration.  */
class matrix2x2 : public matrix<double, 2>
{
public:
  typedef double T;
  inline
  matrix2x2 (T m00, T m10,
	     T m01, T m11)
  {
    m_elements[0][0]=m00;
    m_elements[0][1]=m01;
    m_elements[1][0]=m10;
    m_elements[1][1]=m11;
  }

  inline
  matrix2x2 ()
  {
  }

  inline
  matrix2x2& operator=(const matrix<T, 2>rhs)
  {
    memcpy(m_elements,rhs.m_elements,sizeof (m_elements));
    return *this;
  }

  /* Matrix-vector multiplication.  */
  inline void
  apply_to_vector (T x, T y, T *xx, T *yy)
  {
    *xx = x * m_elements[0][0] + y * m_elements[1][0];
    *yy = x * m_elements[0][1] + y * m_elements[1][1];
  }

  /* Compute inversion.  */
  inline matrix2x2
  invert ()
  {
    T a = m_elements[0][0];
    T b = m_elements[0][1];
    T c = m_elements[1][0];
    T d = m_elements[1][1];
    T det_rec = 1 / (a * d - b * c);
    matrix2x2 ret (d * det_rec, -b * det_rec, -c * det_rec, a * det_rec);
    return ret;
  }
};
/* 4x4 matrix with perspective projection and its inverse.  */
class matrix4x4 : public matrix<double, 4>
{
public:
  typedef double T;
  matrix4x4 () { }

  inline
  matrix4x4 (T e00, T e10, T e20, T e30,
	     T e01, T e11, T e21, T e31,
	     T e02, T e12, T e22, T e32,
	     T e03, T e13, T e23, T e33)
  {
    m_elements[0][0]=e00; m_elements[1][0]=e10; m_elements[2][0]=e20; m_elements[3][0]=e30;
    m_elements[0][1]=e01; m_elements[1][1]=e11; m_elements[2][1]=e21; m_elements[3][1]=e31;
    m_elements[0][2]=e02; m_elements[1][2]=e12; m_elements[2][2]=e22; m_elements[3][2]=e32;
    m_elements[0][3]=e03; m_elements[1][3]=e13; m_elements[2][3]=e23; m_elements[3][3]=e33;
  }

  inline
  matrix4x4& operator=(const matrix<T, 4>rhs)
  {
    memcpy(m_elements,rhs.m_elements,sizeof (m_elements));
    return *this;
  }

  /* Apply matrix to 2 dimensional coordinates and do perspective projetion.  */
  inline void
  perspective_transform (T x, T y, T &xr, T &yr)
  {
    xr = (x * m_elements[0][0] + y * m_elements[0][1] + m_elements[0][2] + m_elements[0][3])
	 / (x * m_elements[2][0] + y * m_elements[2][1] + m_elements[2][2] + m_elements[2][3]);
    yr = (x * m_elements[1][0] + y * m_elements[1][1] + m_elements[1][2] + m_elements[1][3])
	 / (x * m_elements[3][0] + y * m_elements[3][1] + m_elements[3][2] + m_elements[3][3]);
  }

  /* Inverse transform for the operation above.  */
  inline void
  inverse_perspective_transform (T x, T y, T &xr, T &yr)
  {
    matrix2x2 m (m_elements[0][0] - m_elements[2][0] * x,
		 m_elements[0][1] - m_elements[2][1] * x,
		 m_elements[1][0] - m_elements[3][0] * y,
		 m_elements[1][1] - m_elements[3][1] * y);
    matrix2x2 m2 = m.invert ();
    T xx = (m_elements[2][2]+m_elements[2][3])*x - m_elements[0][2] - m_elements[0][3];
    T yy = (m_elements[3][2]+m_elements[3][3])*y - m_elements[1][2] - m_elements[1][3];

    xr = m2.m_elements[0][0] * xx + m2.m_elements[0][1] * yy;
    yr = m2.m_elements[1][0] * xx + m2.m_elements[1][1] * yy;
  }

  /* Matrix-vector multiplication used for RGB values.  */
  inline void
  apply_to_rgb (T r, T g, T b, T *rr, T *gg, T *bb)
  {
    *rr = r * m_elements[0][0] + g * m_elements[1][0] + b * m_elements[2][0] + m_elements[3][0];
    *gg = r * m_elements[0][1] + g * m_elements[1][1] + b * m_elements[2][1] + m_elements[3][1];
    *bb = r * m_elements[0][2] + g * m_elements[1][2] + b * m_elements[2][2] + m_elements[3][2];
  }
  /* This adjust the profile so grayscale has given r,g,b values.  */
  inline void
  normalize_grayscale (T r = 1, T g = 1, T b = 1)
  {
    T scale =  r / (m_elements[0][0] + m_elements[1][0] + m_elements[2][0] + m_elements[3][0]);
    for (int j = 0; j < 4; j++)
      m_elements[j][0] *= scale;
    scale =  g / (m_elements[0][1] + m_elements[1][1] + m_elements[2][1] + m_elements[3][1]);
    for (int j = 0; j < 4; j++)
      m_elements[j][1] *= scale;
    scale =  b / (m_elements[0][2] + m_elements[1][2] + m_elements[2][2] + m_elements[3][2]);
    for (int j = 0; j < 4; j++)
      m_elements[j][2] *= scale;
  }
};
#endif
