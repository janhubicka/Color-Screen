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
template<typename T>
class matrix2x2 : public matrix<T, 2>
{
  typedef matrix<T, 2> B;
public:
  inline
  matrix2x2 (T m00, T m10,
	     T m01, T m11)
  {
    B::m_elements[0][0]=m00;
    B::m_elements[0][1]=m01;
    B::m_elements[1][0]=m10;
    B::m_elements[1][1]=m11;
  }

  inline
  matrix2x2 ()
  {
  }

  inline
  matrix2x2& operator=(const matrix<T, 2>rhs)
  {
    memcpy(B::m_elements,rhs.B::m_elements,sizeof (B::m_elements));
    return *this;
  }

  /* Matrix-vector multiplication.  */
  inline void
  apply_to_vector (T x, T y, T *xx, T *yy)
  {
    *xx = x * B::m_elements[0][0] + y * B::m_elements[1][0];
    *yy = x * B::m_elements[0][1] + y * B::m_elements[1][1];
  }

  /* Compute inversion.  */
  inline matrix2x2
  invert ()
  {
    T a = B::m_elements[0][0];
    T b = B::m_elements[0][1];
    T c = B::m_elements[1][0];
    T d = B::m_elements[1][1];
    T det_rec = 1 / (a * d - b * c);
    matrix2x2 ret (d * det_rec, -b * det_rec, -c * det_rec, a * det_rec);
    return ret;
  }
};

/* 3x3 matrix with inverse operation.  */
template<typename T>
class matrix3x3 : public matrix<T, 3>
{
  typedef matrix<T, 3> B;
public:
  matrix3x3<T> () { }

  inline
  matrix3x3<T> (T e00, T e10, T e20,
	        T e01, T e11, T e21,
	        T e02, T e12, T e22)
  {
    B::m_elements[0][0]=e00; B::m_elements[1][0]=e10; B::m_elements[2][0]=e20;
    B::m_elements[0][1]=e01; B::m_elements[1][1]=e11; B::m_elements[2][1]=e21;
    B::m_elements[0][2]=e02; B::m_elements[1][2]=e12; B::m_elements[2][2]=e22;
  }

  inline
  matrix3x3<T>& operator=(const matrix<T, 3>rhs)
  {
    memcpy(B::m_elements,rhs.B::m_elements,sizeof (B::m_elements));
    return *this;
  }

  /* Apply matrix to vector (x,y,1) and return its two elements.  */
  inline void
  apply (T x, T y, T *xx, T *yy)
  {
    *xx = x * B::m_elements[0][0] + y * B::m_elements[1][0] + B::m_elements[2][0];
    *yy = x * B::m_elements[0][1] + y * B::m_elements[1][1] + B::m_elements[2][1];
  }

  /* Compute inversion.  */
  inline matrix3x3
  invert ()
  {
    T determinant = 0;
    matrix3x3 ret;
    for(int i=0;i<3;i++)
      determinant = determinant + (B::m_elements[0][i]*(B::m_elements[1][(i+1)%3]*B::m_elements[2][(i+2)%3] - B::m_elements[1][(i+2)%3]*B::m_elements[2][(i+1)%3]));
    for(int i=0;i<3;i++)
      for(int j=0;j<3;j++)
	ret.m_elements[j][i] = ((B::m_elements[(i+1)%3][(j+1)%3] * B::m_elements[(i+2)%3][(j+2)%3]) - (B::m_elements[(i+1)%3][(j+2)%3]*B::m_elements[(i+2)%3][(j+1)%3]))/ determinant;
    return ret;
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
template<typename T>
class matrix4x4 : public matrix<T, 4>
{
  typedef matrix<T, 4> B;
public:
  matrix4x4<T> () { }

  inline
  matrix4x4<T> (T e00, T e10, T e20, T e30,
	        T e01, T e11, T e21, T e31,
	        T e02, T e12, T e22, T e32,
	        T e03, T e13, T e23, T e33)
  {
    B::m_elements[0][0]=e00; B::m_elements[1][0]=e10; B::m_elements[2][0]=e20; B::m_elements[3][0]=e30;
    B::m_elements[0][1]=e01; B::m_elements[1][1]=e11; B::m_elements[2][1]=e21; B::m_elements[3][1]=e31;
    B::m_elements[0][2]=e02; B::m_elements[1][2]=e12; B::m_elements[2][2]=e22; B::m_elements[3][2]=e32;
    B::m_elements[0][3]=e03; B::m_elements[1][3]=e13; B::m_elements[2][3]=e23; B::m_elements[3][3]=e33;
  }

  inline
  matrix4x4<T>& operator=(const matrix<T, 4>rhs)
  {
    memcpy(B::m_elements,rhs.B::m_elements,sizeof (B::m_elements));
    return *this;
  }

  /* Apply matrix to 2 dimensional coordinates and do perspective projetion.
     TODO: function is transposed  */
  inline void
  perspective_transform (T x, T y, T &xr, T &yr)
  {
    xr =   (x * B::m_elements[0][0] + y * B::m_elements[1][0] + B::m_elements[2][0] + B::m_elements[3][0])
	 / (x * B::m_elements[0][2] + y * B::m_elements[1][2] + B::m_elements[2][2] + B::m_elements[3][2]);
    yr =   (x * B::m_elements[0][1] + y * B::m_elements[1][1] + B::m_elements[2][1] + B::m_elements[3][1])
	 / (x * B::m_elements[0][3] + y * B::m_elements[1][3] + B::m_elements[2][3] + B::m_elements[3][3]);
  }

  /* Inverse transform for the operation above.  */
  inline void
  inverse_perspective_transform (T x, T y, T &xr, T &yr)
  {
    matrix2x2<T> m (B::m_elements[0][0] - B::m_elements[0][2] * x,
	            B::m_elements[1][0] - B::m_elements[1][2] * x,
	            B::m_elements[0][1] - B::m_elements[0][3] * y,
	            B::m_elements[1][1] - B::m_elements[1][3] * y);
    matrix2x2<T> m2 = m.invert ();
    T xx = (B::m_elements[2][2]+B::m_elements[3][2])*x - B::m_elements[2][0] - B::m_elements[3][0];
    T yy = (B::m_elements[2][3]+B::m_elements[3][3])*y - B::m_elements[2][1] - B::m_elements[3][1];

    xr = m2.m_elements[0][0] * xx + m2.m_elements[0][1] * yy;
    yr = m2.m_elements[1][0] * xx + m2.m_elements[1][1] * yy;

#if 0
    T ix, iy;
    perspective_transform (xr,yr, ix, iy);
    if (fabs (ix-x) + fabs (iy-y) > 1)
	    printf ("Inverse broken\n");
#endif
  }

  /* Matrix-vector multiplication used for RGB values.  */
  inline void
  apply_to_rgb (T r, T g, T b, T *rr, T *gg, T *bb)
  {
    *rr = r * B::m_elements[0][0] + g * B::m_elements[1][0] + b * B::m_elements[2][0] + B::m_elements[3][0];
    *gg = r * B::m_elements[0][1] + g * B::m_elements[1][1] + b * B::m_elements[2][1] + B::m_elements[3][1];
    *bb = r * B::m_elements[0][2] + g * B::m_elements[1][2] + b * B::m_elements[2][2] + B::m_elements[3][2];
  }
  /* This adjust the profile so grayscale has given r,g,b values.  */
  inline void
  normalize_grayscale (T r = 1, T g = 1, T b = 1)
  {
    matrix4x4 inv = invert ();
    T ivec[4] = {r, g, b, 1};
    T vec[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
	vec[i] += ivec[j] * inv.m_elements[j][i];
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
	B::m_elements[i][j] *= vec[i];
  }
  /* Compute inversion.  */
  inline matrix4x4
  invert ()
  {
    T A2323 = B::m_elements[2][2] * B::m_elements[3][3] - B::m_elements[2][3] * B::m_elements[3][2];
    T A1323 = B::m_elements[2][1] * B::m_elements[3][3] - B::m_elements[2][3] * B::m_elements[3][1];
    T A1223 = B::m_elements[2][1] * B::m_elements[3][2] - B::m_elements[2][2] * B::m_elements[3][1];
    T A0323 = B::m_elements[2][0] * B::m_elements[3][3] - B::m_elements[2][3] * B::m_elements[3][0];
    T A0223 = B::m_elements[2][0] * B::m_elements[3][2] - B::m_elements[2][2] * B::m_elements[3][0];
    T A0123 = B::m_elements[2][0] * B::m_elements[3][1] - B::m_elements[2][1] * B::m_elements[3][0];
    T A2313 = B::m_elements[1][2] * B::m_elements[3][3] - B::m_elements[1][3] * B::m_elements[3][2];
    T A1313 = B::m_elements[1][1] * B::m_elements[3][3] - B::m_elements[1][3] * B::m_elements[3][1];
    T A1213 = B::m_elements[1][1] * B::m_elements[3][2] - B::m_elements[1][2] * B::m_elements[3][1];
    T A2312 = B::m_elements[1][2] * B::m_elements[2][3] - B::m_elements[1][3] * B::m_elements[2][2];
    T A1312 = B::m_elements[1][1] * B::m_elements[2][3] - B::m_elements[1][3] * B::m_elements[2][1];
    T A1212 = B::m_elements[1][1] * B::m_elements[2][2] - B::m_elements[1][2] * B::m_elements[2][1];
    T A0313 = B::m_elements[1][0] * B::m_elements[3][3] - B::m_elements[1][3] * B::m_elements[3][0];
    T A0213 = B::m_elements[1][0] * B::m_elements[3][2] - B::m_elements[1][2] * B::m_elements[3][0];
    T A0312 = B::m_elements[1][0] * B::m_elements[2][3] - B::m_elements[1][3] * B::m_elements[2][0];
    T A0212 = B::m_elements[1][0] * B::m_elements[2][2] - B::m_elements[1][2] * B::m_elements[2][0];
    T A0113 = B::m_elements[1][0] * B::m_elements[3][1] - B::m_elements[1][1] * B::m_elements[3][0];
    T A0112 = B::m_elements[1][0] * B::m_elements[2][1] - B::m_elements[1][1] * B::m_elements[2][0];

    T det = B::m_elements[0][0] * ( B::m_elements[1][1] * A2323 - B::m_elements[1][2] * A1323 + B::m_elements[1][3] * A1223)
	    - B::m_elements[0][1] * ( B::m_elements[1][0] * A2323 - B::m_elements[1][2] * A0323 + B::m_elements[1][3] * A0223)
	    + B::m_elements[0][2] * ( B::m_elements[1][0] * A1323 - B::m_elements[1][1] * A0323 + B::m_elements[1][3] * A0123)
	    - B::m_elements[0][3] * ( B::m_elements[1][0] * A1223 - B::m_elements[1][1] * A0223 + B::m_elements[1][2] * A0123);
    det = 1 / det;

    matrix4x4 ret
      (det *  (B::m_elements[1][1] * A2323 - B::m_elements[1][2] * A1323 + B::m_elements[1][3] * A1223), /*00*/
       det * -(B::m_elements[1][0] * A2323 - B::m_elements[1][2] * A0323 + B::m_elements[1][3] * A0223), /*10*/
       det *  (B::m_elements[1][0] * A1323 - B::m_elements[1][1] * A0323 + B::m_elements[1][3] * A0123), /*20*/
       det * -(B::m_elements[1][0] * A1223 - B::m_elements[1][1] * A0223 + B::m_elements[1][2] * A0123), /*30*/

       det * -(B::m_elements[0][1] * A2323 - B::m_elements[0][2] * A1323 + B::m_elements[0][3] * A1223), /*01*/
       det *  (B::m_elements[0][0] * A2323 - B::m_elements[0][2] * A0323 + B::m_elements[0][3] * A0223), /*11*/
       det * -(B::m_elements[0][0] * A1323 - B::m_elements[0][1] * A0323 + B::m_elements[0][3] * A0123), /*21*/
       det *  (B::m_elements[0][0] * A1223 - B::m_elements[0][1] * A0223 + B::m_elements[0][2] * A0123), /*31*/

       det *  (B::m_elements[0][1] * A2313 - B::m_elements[0][2] * A1313 + B::m_elements[0][3] * A1213), /*02*/
       det * -(B::m_elements[0][0] * A2313 - B::m_elements[0][2] * A0313 + B::m_elements[0][3] * A0213), /*12*/
       det *  (B::m_elements[0][0] * A1313 - B::m_elements[0][1] * A0313 + B::m_elements[0][3] * A0113), /*22*/
       det * -(B::m_elements[0][0] * A1213 - B::m_elements[0][1] * A0213 + B::m_elements[0][2] * A0113), /*32*/

       det * -(B::m_elements[0][1] * A2312 - B::m_elements[0][2] * A1312 + B::m_elements[0][3] * A1212), /*03*/
       det *  (B::m_elements[0][0] * A2312 - B::m_elements[0][2] * A0312 + B::m_elements[0][3] * A0212), /*13*/
       det * -(B::m_elements[0][0] * A1312 - B::m_elements[0][1] * A0312 + B::m_elements[0][3] * A0112), /*23*/
       det *  (B::m_elements[0][0] * A1212 - B::m_elements[0][1] * A0212 + B::m_elements[0][2] * A0112));/*33*/
   return ret;
  }
};
#endif
