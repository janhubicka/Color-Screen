#ifndef MATRIX_H
#define MATRIX_H
/* Basic template for square matrix.  */
#include <cstring>
#include <stdio.h>

/* Square matrix template.  */
template <size_t dim>
class matrix
{
public:
  static const int m_dim = dim;
  double m_elements[m_dim][m_dim];

  matrix()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=(double) (i==j);
  }

  void
  randomize ()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=rand()%100;
  }

  matrix(const matrix &m)
  {
    memcpy(m_elements,m.m_elements,sizeof (m_elements));
  }

  matrix<dim> operator+ (const matrix<dim>& rhs) const
  {
    matrix<dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	ret.m_elements[j][i] = m_elements[j][i] + rhs.m_elements[j][i];
    return ret;
  }

  matrix<dim> operator* (const matrix<dim>& rhs) const
  {
    matrix<dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	{
	  double a = 0;
	  for (int k = 0; k < m_dim; k++)
	    a += m_elements[j][k] * rhs.m_elements[k][i];
	  ret.m_elements[j][i] = a;
	}
    return ret;
  }

  void
  print (FILE *f)
  {
    for (int j = 0; j < m_dim; j++)
      {
        for (int i = 0; i < m_dim; i++)
	  fprintf (f, " %f", m_elements[i][j]);
	fprintf (f, "\n");
      }
  }
};

/* 2x2 matrix with inverse opration.  */
class matrix2x2 : public matrix<2>
{
public:
  matrix2x2 (double m00, double m01, double m10, double m11)
  {
    m_elements[0][0]=m00;
    m_elements[0][1]=m01;
    m_elements[1][0]=m10;
    m_elements[1][1]=m11;
  }

  matrix2x2 ()
  {
  }

  inline matrix2x2
  invert ()
  {
    double a = m_elements[0][0];
    double b = m_elements[0][1];
    double c = m_elements[1][0];
    double d = m_elements[1][1];
    double det = a * d - b * c;
    matrix2x2 ret (d / det, -b / det, -c / det, a / det);
    return ret;
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
class matrix4x4 : public matrix<4>
{
public:
  matrix4x4 () { }

  matrix4x4& operator=(const matrix<4>rhs)
  {
    memcpy(m_elements,rhs.m_elements,sizeof (m_elements));
    return *this;
  }

  inline void
  perspective_transform (double x, double y, double &xr, double &yr)
  {
    xr = (x * m_elements[0][0] + y * m_elements[0][1] + m_elements[0][2] + m_elements[0][3])
	 / (x * m_elements[2][0] + y * m_elements[2][1] + m_elements[2][2] + m_elements[2][3]);
    yr = (x * m_elements[1][0] + y * m_elements[1][1] + m_elements[1][2] + m_elements[1][3])
	 / (x * m_elements[3][0] + y * m_elements[3][1] + m_elements[3][2] + m_elements[3][3]);
  }

  inline void
  inverse_perspective_transform (double x, double y, double &xr, double &yr)
  {
    matrix2x2 m (m_elements[0][0] - m_elements[2][0] * x,
		 m_elements[0][1] - m_elements[2][1] * x,
		 m_elements[1][0] - m_elements[3][0] * y,
		 m_elements[1][1] - m_elements[3][1] * y);
    matrix2x2 m2 = m.invert ();
    double xx = (m_elements[2][2]+m_elements[2][3])*x - m_elements[0][2] - m_elements[0][3];
    double yy = (m_elements[3][2]+m_elements[3][3])*y - m_elements[1][2] - m_elements[1][3];

    xr = m2.m_elements[0][0] * xx + m2.m_elements[0][1] * yy;
    yr = m2.m_elements[1][0] * xx + m2.m_elements[1][1] * yy;
  }
};
#endif
