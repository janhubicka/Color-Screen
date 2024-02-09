/* Based on nmsimplex.c by Michael F. Hutt used by dcamprof.  */
template<typename T,typename C>
double
simplex (C &c)
{
  const T ALPHA = 1;
  const T BETA = 0.5;
  const T GAMMA = 2;
  const bool verbose = true;
  const int MAX_IT =100000;     /* maximum number of iterations */
  int vs;			/* vertex with smallest value */
  int vh;			/* vertex with next smallest value */
  int vg;			/* vertex with largest value */

  int i, j, m, row;
  int k;			/* track the number of function evaluations */
  int itr;			/* track the number of iterations */
  int n = c.num_values ();
  T EPSILON = c.epsilon ();
  T scale = c.scale ();

  T **v;			/* holds vertices of simplex */
  T pn, qn;		/* values used to create initial simplex */
  T *f;			/* value of function at each vertex */
  T fr;			/* value of function at reflection point */
  T fe;			/* value of function at expansion point */
  T fc;			/* value of function at contraction point */
  T *vr;			/* reflection - coordinates */
  T *ve;			/* expansion - coordinates */
  T *vc;			/* contraction - coordinates */
  T *vm;			/* centroid - coordinates */
  T min;

  T fsum, favg, s, cent;

  /* dynamically allocate arrays */

  /* allocate the rows of the arrays */
  v = (T **) malloc ((n + 1) * sizeof (T *));
  f = (T *) malloc ((n + 1) * sizeof (T));
  vr = (T *) malloc (n * sizeof (T));
  ve = (T *) malloc (n * sizeof (T));
  vc = (T *) malloc (n * sizeof (T));
  vm = (T *) malloc (n * sizeof (T));

  /* allocate the columns of the arrays */
  for (i = 0; i <= n; i++)
    {
      v[i] = (T *) malloc (n * sizeof (T));
    }

  /* create the initial simplex */
  /* assume one of the vertices is 0,0 */

  pn = scale * (my_sqrt ((T)n + 1) - 1 + n) / (n * my_sqrt ((T)2));
  qn = scale * (my_sqrt ((T)n + 1) - 1) / (n * my_sqrt ((T)2));

  for (i = 0; i < n; i++)
    {
      v[0][i] = c.start[i];
    }
  j = 0;			// avoid warning
  for (i = 1; i <= n; i++)
    {
      for (j = 0; j < n; j++)
	{
	  if (i - 1 == j)
	    {
	      v[i][j] = pn + c.start[j];
	    }
	  else
	    {
	      v[i][j] = qn + c.start[j];
	    }
	}
    }

  c.constrain (v[j]);
  /* find the initial function values */
  for (j = 0; j <= n; j++)
    f[j] = c.objfunc (v[j]);

  k = n + 1;

  /* print out the initial values */
  if (verbose)
  {
     printf("Initial Values\n");
     for (j=0;j<=n;j++) {
     for (i=0;i<n;i++) {
     printf("%f %f\n",v[j][i],f[j]);
     }
     }
  }


  /* begin the main loop of the minimization */
  for (itr = 1; itr <= MAX_IT; itr++)
    {
      /* find the index of the largest value */
      vg = 0;
      for (j = 0; j <= n; j++)
	{
	  if (f[j] > f[vg])
	    {
	      vg = j;
	    }
	}

      /* find the index of the smallest value */
      vs = 0;
      for (j = 0; j <= n; j++)
	{
	  if (f[j] < f[vs])
	    {
	      vs = j;
	    }
	}

      /* find the index of the second largest value */
      vh = vs;
      for (j = 0; j <= n; j++)
	{
	  if (f[j] > f[vh] && f[j] < f[vg])
	    {
	      vh = j;
	    }
	}

      /* calculate the centroid */
      for (j = 0; j <= n - 1; j++)
	{
	  cent = 0.0;
	  for (m = 0; m <= n; m++)
	    {
	      if (m != vg)
		{
		  cent += v[m][j];
		}
	    }
	  vm[j] = cent / n;
	}

      /* reflect vg to new vertex vr */
      for (j = 0; j <= n - 1; j++)
	{
	  /*vr[j] = (1+ALPHA)*vm[j] - ALPHA*v[vg][j]; */
	  vr[j] = vm[j] + ALPHA * (vm[j] - v[vg][j]);
	}
      c.constrain (vr);
      fr = c.objfunc (vr);
      k++;

      if (fr < f[vh] && fr >= f[vs])
	{
	  for (j = 0; j <= n - 1; j++)
	    {
	      v[vg][j] = vr[j];
	    }
	  f[vg] = fr;
	}

      /* investigate a step further in this direction */
      if (fr < f[vs])
	{
	  for (j = 0; j <= n - 1; j++)
	    {
	      /*ve[j] = GAMMA*vr[j] + (1-GAMMA)*vm[j]; */
	      ve[j] = vm[j] + GAMMA * (vr[j] - vm[j]);
	    }
	  c.constrain (ve);
	  fe = c.objfunc (ve);
	  k++;

	  /* by making fe < fr as opposed to fe < f[vs],                     
	     Rosenbrocks function takes 63 iterations as opposed 
	     to 64 when using T variables. */

	  if (fe < fr)
	    {
	      for (j = 0; j <= n - 1; j++)
		{
		  v[vg][j] = ve[j];
		}
	      f[vg] = fe;
	    }
	  else
	    {
	      for (j = 0; j <= n - 1; j++)
		{
		  v[vg][j] = vr[j];
		}
	      f[vg] = fr;
	    }
	}

      /* check to see if a contraction is necessary */
      if (fr >= f[vh])
	{
	  if (fr < f[vg] && fr >= f[vh])
	    {
	      /* perform outside contraction */
	      for (j = 0; j <= n - 1; j++)
		{
		  /*vc[j] = BETA*v[vg][j] + (1-BETA)*vm[j]; */
		  vc[j] = vm[j] + BETA * (vr[j] - vm[j]);
		}
	      c.constrain (vc);
	      fc = c.objfunc (vc);
	      k++;
	    }
	  else
	    {
	      /* perform inside contraction */
	      for (j = 0; j <= n - 1; j++)
		{
		  /*vc[j] = BETA*v[vg][j] + (1-BETA)*vm[j]; */
		  vc[j] = vm[j] - BETA * (vm[j] - v[vg][j]);
		}
	      c.constrain (vc);
	      fc = c.objfunc (vc);
	      k++;
	    }


	  if (fc < f[vg])
	    {
	      for (j = 0; j <= n - 1; j++)
		{
		  v[vg][j] = vc[j];
		}
	      f[vg] = fc;
	    }
	  /* at this point the contraction is not successful,
	     we must halve the distance from vs to all the 
	     vertices of the simplex and then continue.
	     10/31/97 - modified to account for ALL vertices. 
	   */
	  else
	    {
	      for (row = 0; row <= n; row++)
		{
		  if (row != vs)
		    {
		      for (j = 0; j <= n - 1; j++)
			{
			  v[row][j] = v[vs][j] + (v[row][j] - v[vs][j]) / 2.0;
			}
		    }
		}
	      c.constrain (v[vg]);
	      f[vg] = c.objfunc (v[vg]);
	      k++;
	      c.constrain (v[vh]);
	      f[vh] = c.objfunc (v[vh]);
	      k++;


	    }
	}

      /* print out the value at each iteration */
      if (verbose)
      {
         printf("Iteration %d\n",itr);
         for (j=0;j<=n;j++) {
         for (i=0;i<n;i++) {
         printf("%f %f\n",v[j][i],f[j]);
         }
         }
      }

      /* test for convergence */
      fsum = 0.0;
      for (j = 0; j <= n; j++)
	{
	  fsum += f[j];
	}
      favg = fsum / (n + 1);
      s = 0.0;
      for (j = 0; j <= n; j++)
	{
	  s += pow ((f[j] - favg), 2.0) / (n);
	}
      s = my_sqrt (s);
      if (s < EPSILON)
	break;
    }
  /* end main loop of the minimization */

  /* find the index of the smallest value */
  vs = 0;
  for (j = 0; j <= n; j++)
    {
      if (f[j] < f[vs])
	{
	  vs = j;
	}
    }


  if (verbose)
    printf("The minimum was found at\n"); 
  for (j = 0; j < n; j++)
    {
      if (verbose)
        printf("%i:%f\n",j,v[vs][j]);
      c.start[j] = v[vs][j];
    }

  min = c.objfunc (v[vs]);
  k++;
  if (verbose)
  {
     printf("%d Function Evaluations\n",k);
     printf("%d Iterations through program\n",itr);
  }

  free (f);
  free (vr);
  free (ve);
  free (vc);
  free (vm);
  for (i = 0; i <= n; i++)
    {
      free (v[i]);
    }
  free (v);
  return min;
}
