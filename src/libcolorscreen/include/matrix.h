#ifndef MATRIX_H
#define MATRIX_H
/* Basic template for square matrix.  */
#include <cstring>
#include <stdio.h>

#define flatten_attr __attribute__ ((__flatten__))

/* Square matrix template.  */
template <size_t dim>
class matrix
{
public:
  static const int m_dim = dim;
  double m_elements[m_dim][m_dim];

  /* Default constructor: build identity matrix.  */
  inline
  matrix()
  {
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	m_elements[j][i]=(double) (i==j);
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
  inline matrix<dim>
  operator+ (const matrix<dim>& rhs) const
  {
    matrix<dim> ret;
    for (int j = 0; j < m_dim; j++)
      for (int i = 0; i < m_dim; i++)
	ret.m_elements[j][i] = m_elements[j][i] + rhs.m_elements[j][i];
    return ret;
  }

  inline matrix<dim>
  operator* (const matrix<dim>& rhs) const
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

  inline matrix<dim>
  operator* (const double rhs) const
  {
    matrix<dim> ret;
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
	  fprintf (f, " %f", m_elements[i][j]);
	fprintf (f, "\n");
      }
  }
};

/* 2x2 matrix with inverse opration.  */
class matrix2x2 : public matrix<2>
{
public:
  inline
  matrix2x2 (double m00, double m10,
	     double m01, double m11)
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

  /* Matrix-vector multiplication.  */
  inline void
  apply_to_vector (double x, double y, double *xx, double *yy)
  {
    *xx = x * m_elements[0][0] + y * m_elements[1][0];
    *yy = x * m_elements[0][1] + y * m_elements[1][1];
  }

  /* Compute inversion.  */
  inline matrix2x2
  invert ()
  {
    double a = m_elements[0][0];
    double b = m_elements[0][1];
    double c = m_elements[1][0];
    double d = m_elements[1][1];
    double det_rec = 1 / (a * d - b * c);
    matrix2x2 ret (d * det_rec, -b * det_rec, -c * det_rec, a * det_rec);
    return ret;
  }
};

/* 4x4 matrix with perspective projection and its inverse.  */
class matrix4x4 : public matrix<4>
{
public:
  matrix4x4 () { }

  inline
  matrix4x4 (double e00, double e10, double e20, double e30,
	     double e01, double e11, double e21, double e31,
	     double e02, double e12, double e22, double e32,
	     double e03, double e13, double e23, double e33)
  {
    m_elements[0][0]=e00; m_elements[1][0]=e10; m_elements[2][0]=e20; m_elements[3][0]=e30;
    m_elements[0][1]=e01; m_elements[1][1]=e11; m_elements[2][1]=e21; m_elements[3][1]=e31;
    m_elements[0][2]=e02; m_elements[1][2]=e12; m_elements[2][2]=e22; m_elements[3][2]=e32;
    m_elements[0][3]=e03; m_elements[1][3]=e13; m_elements[2][3]=e23; m_elements[3][3]=e33;
  }

  inline
  matrix4x4& operator=(const matrix<4>rhs)
  {
    memcpy(m_elements,rhs.m_elements,sizeof (m_elements));
    return *this;
  }

  /* Apply matrix to 2 dimensional coordinates and do perspective projetion.  */
  inline void
  perspective_transform (double x, double y, double &xr, double &yr)
  {
    xr = (x * m_elements[0][0] + y * m_elements[0][1] + m_elements[0][2] + m_elements[0][3])
	 / (x * m_elements[2][0] + y * m_elements[2][1] + m_elements[2][2] + m_elements[2][3]);
    yr = (x * m_elements[1][0] + y * m_elements[1][1] + m_elements[1][2] + m_elements[1][3])
	 / (x * m_elements[3][0] + y * m_elements[3][1] + m_elements[3][2] + m_elements[3][3]);
  }

  /* Inverse transform for the operation above.  */
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

  /* Matrix-vector multiplication used for RGB values.  */
  inline void
  apply_to_rgb (double r, double g, double b, double *rr, double *gg, double *bb)
  {
    *rr = r * m_elements[0][0] + g * m_elements[1][0] + b * m_elements[2][0] + m_elements[3][0];
    *gg = r * m_elements[0][1] + g * m_elements[1][1] + b * m_elements[2][1] + m_elements[3][1];
    *bb = r * m_elements[0][2] + g * m_elements[1][2] + b * m_elements[2][2] + m_elements[3][2];
  }
  /* This adjust the profile so grayscale has given r,g,b values.  */
  inline void
  normalize_grayscale (double r = 1, double g = 1, double b = 1)
  {
    double scale =  r / (m_elements[0][0] + m_elements[1][0] + m_elements[2][0] + m_elements[3][0]);
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

// http://www.graficaobscura.com/matrix/index.html
static const double rwght = 0.3086, gwght = 0.6094, bwght = 0.0820;

// http://www.graficaobscura.com/matrix/index.html
class saturation_matrix : public matrix4x4
{
public:
  inline
  saturation_matrix (double s)
  : matrix4x4 ((1-s)*rwght + s, (1-s)*gwght    , (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght + s, (1-s)*bwght    , 0,
	       (1-s)*rwght    , (1-s)*gwght    , (1-s)*bwght + s, 0,
	       0,             0,              0,                  0)
  {}
};
/* Matrix profile of Finlay taking screen
   Based on XYZ measurements of Finlay filter scan on eversmart dimmed to 50%.   */
class finlay_matrix : public matrix4x4
{
public:
  inline
  finlay_matrix ()
  : matrix4x4 (0.116325,0.148173,0.060772, 0,
	       0.059402,0.201094,0.028883, 0,
	       0.005753,0.030250,0.136011, 0,
	       0,             0,              0,                  0)
  { }
};
/* Matrix profile of dufay taken from Nikon steamroler.
   In XYZ.  */
class dufay_matrix : public matrix4x4
{
public:
  inline
  dufay_matrix ()
  : matrix4x4 (0.321001,0.205657,0.072222, 0,
	       0.178050,0.406124,0.071736, 0,
	       0.006007,0.040292,0.240037, 0,
	       0,             0,              0,                  0)
  { }
};
/* Matrix I decided works well for kimono picture (sRGB).  */
class grading_matrix : public matrix4x4
{
public:
  inline
  grading_matrix ()
  : matrix4x4 (1,-0.4,-0.1, 0,
	       0.25,1,-0.1, 0,
	       +0.05,-0.55,1.05, 0,
	       0,             0,              0,                  0)
  { normalize_grayscale (); }
};
/* sRGB->XYZ conversion matrix.  */
class srgb_xyz_matrix : public matrix4x4
{
public:
  inline
  srgb_xyz_matrix ()
  : matrix4x4 (0.4124564,  0.3575761,  0.1804375, 0,
 	       0.2126729,  0.7151522,  0.0721750, 0,
 	       0.0193339,  0.1191920,  0.9503041, 0,
	       0,             0,              0,                  0)
  {}
};
/* XYZ->sRGB conversion matrix.  */
class xyz_srgb_matrix : public matrix4x4
{
public:
  inline
  xyz_srgb_matrix ()
  : matrix4x4 (3.2404542, -1.5371385, -0.4985314, 0,
	      -0.9692660,  1.8760108,  0.0415560, 0,
	       0.0556434, -0.2040259,  1.0572252, 0,
	       0,             0,              0,                  0)
  {}
};
#endif
