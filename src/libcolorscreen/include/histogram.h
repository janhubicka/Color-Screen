#ifndef HISTOGRAM_H
#define HISTOGRAM_H
#include <limits>

/* Basic datastructure to hols various value histograms.  */
class histogram
{
public:
  histogram()
  : m_minval (std::numeric_limits<luminosity_t>::max ()), m_maxval (std::numeric_limits<luminosity_t>::min ()), m_inv (0), m_entries (), m_total (-1)
  {
  }

  /* Adjust range so VAL will fit in.  */
  inline void
  pre_account (luminosity_t val)
  {
    m_minval = std::min (m_minval, val);
    m_maxval = std::max (m_maxval, val);
  }

  /* Once range is known allocate the histogram.  */
  inline void finalize_range (int nvals)
  {
    if (m_minval == m_maxval)
      m_maxval = m_minval + 0.00001;
    m_inv = nvals / (m_maxval - m_minval);
    m_entries.resize (nvals, 0);
  }

  /* Alternative way to initialize histogram if range is known.  */
  inline void set_range (luminosity_t minval, luminosity_t maxval, int nvals)
  {
    if (m_minval != m_maxval)
      abort ();
    m_minval = minval;
    m_maxval = maxval;
    finalize_range (nvals);
    //std::fill (m_entries.begin (), m_entries.end (), 0);
  }


  /* Turn value intro index of entry in the value histogram.  */
  inline int
  val_to_index (luminosity_t val)
  {
    if (val >= m_minval && val <= m_maxval)
      {
	int entry = (val - m_minval) * m_inv;
	if (entry == (int)m_entries.size ())
	  entry--;
	return entry;
      }
    return -1;
  }

  /* turn in histogram to typical value.  */
  inline luminosity_t
  index_to_val (luminosity_t entry)
  {
    return m_minval + (entry + 0.5) * ((m_maxval - m_minval) / m_entries.size ());
  }

  /* Account value to histogram. Return true if it fits in the range and was accounted.  */
  inline bool
  account (luminosity_t val)
  {
    if (m_total != -1)
      abort ();
    int idx;
    if ((idx = val_to_index (val)) >= 0)
      {
        m_entries[idx]++;
	return true;
      }
    return false;
  }

  /* Finalize data collection.  */
  void
  finalize ()
  {
    if (m_total != -1)
      abort ();
    int sum = 0;
    for (auto n : m_entries)
      sum += n;
    m_total = sum;
  }

  /* Find minimal value after cutting off total*skip elements.  */
  luminosity_t
  find_min (luminosity_t skip)
  {
    if (m_total == -1)
      abort ();
    int threshold = (m_total * skip) + 0.5;
    int sum = 0;
    //printf ("Threshold %i total %i\n", threshold, m_total);
    for (int i = 0; i < (int)m_entries.size (); i++)
      {
	sum += m_entries[i];
	//printf ("%i %i %i %f\n",i,sum, m_entries[i], index_to_val (i));
	if (sum > threshold)
	  return index_to_val (i);
      }
    return m_minval;
  }
  /* Find maximal value after cutting off total*skip elements.  */
  luminosity_t
  find_max (luminosity_t skip)
  {
    if (m_total == -1)
      abort ();
    int threshold = (m_total * skip) + 0.5;
    int sum = 0;
    for (int i = (int)m_entries.size () - 1; i >= 0; i--)
      {
	sum += m_entries[i];
	if (sum > threshold)
	  return index_to_val (i);
      }
    return m_maxval;
  }
  /* return robust average skiping skip_min & skip_max ratio of extreme points.  */
  luminosity_t
  find_avg (luminosity_t skip_min = 0, luminosity_t skip_max = 0)
  {
    if (m_total == -1)
      abort ();
    int wsum = 0;
    int mini, maxi;
    int sum1 = 0;
    int threshold = (m_total * skip_min) + 0.5;
    for (mini = 0; mini < (int)m_entries.size (); mini++)
      {
	sum1 += m_entries[mini];
	if (sum1 > threshold)
	  break;
      }
    int sum2 = 0;
    threshold = (m_total * skip_max) + 0.5;
    for (maxi = (int)m_entries.size () - 1; maxi >= 0; maxi--)
      {
	sum2 += m_entries[maxi];
	if (sum2 > threshold)
	  break;
      }
    if (sum1 + sum2 >= m_total)
      abort ();
    if (mini > maxi)
      abort ();
    for (int i = mini; i <= maxi; i++)
      wsum += m_entries[i] * i;
    return index_to_val (wsum / ((luminosity_t)m_total - sum1 - sum2));
  }

  int num_samples ()
  {
    return m_total;
  }

private:

  luminosity_t m_minval, m_maxval;
  luminosity_t m_inv;
  std::vector<unsigned int> m_entries;
  int m_total;
};

class
rgb_histogram
{
public:
  rgb_histogram()
  { }
  inline void
  pre_account (rgbdata val)
  {
    m_histogram[0].pre_account (val[0]);
    m_histogram[1].pre_account (val[1]);
    m_histogram[2].pre_account (val[2]);
  }
  inline void
  finalize_range (int nvals)
  {
    m_histogram[0].finalize_range (nvals);
    m_histogram[1].finalize_range (nvals);
    m_histogram[2].finalize_range (nvals);
  }
  inline void
  set_range (rgbdata minval, rgbdata maxval, int nvals)
  {
    m_histogram[0].set_range (minval[0], maxval[0], nvals);
    m_histogram[1].set_range (minval[1], maxval[1], nvals);
    m_histogram[2].set_range (minval[2], maxval[2], nvals);
  }
  void
  finalize ()
  {
    m_histogram[0].finalize ();
    m_histogram[1].finalize ();
    m_histogram[2].finalize ();
  }
  inline void
  account (rgbdata val)
  {
    m_histogram[0].account (val[0]);
    m_histogram[1].account (val[1]);
    m_histogram[2].account (val[2]);
  }
  rgbdata
  find_min (luminosity_t skip)
  {
    return {m_histogram[0].find_min (skip),
            m_histogram[1].find_min (skip),
            m_histogram[2].find_min (skip)};
  }
  rgbdata
  find_max (luminosity_t skip)
  {
    return {m_histogram[0].find_max (skip),
            m_histogram[1].find_max (skip),
            m_histogram[2].find_max (skip)};
  }
  rgbdata
  find_avg (luminosity_t skipmin = 0, luminosity_t skipmax = 0)
  {
    return {m_histogram[0].find_avg (skipmin,skipmax),
            m_histogram[1].find_avg (skipmin,skipmax),
            m_histogram[2].find_avg (skipmin,skipmax)};
  }

  int
  num_samples ()
  {
    return std::min (std::min (m_histogram[0].num_samples (),
			       m_histogram[1].num_samples ()),
			       m_histogram[2].num_samples ());

  }
private:
  histogram m_histogram[3];
};
#endif
