#ifndef HISTOGRAM_H
#define HISTOGRAM_H
#include <limits>
namespace colorscreen
{

/* Basic datastructure for value histograms.  */
class histogram
{
public:
  histogram ()
      : m_minval (std::numeric_limits<luminosity_t>::max ()),
        m_maxval (std::numeric_limits<luminosity_t>::min ()), m_inv (0),
        m_entries (), m_total (-1)
  {
  }

  /* Adjust range so VAL will fit in.  */
  inline void
  pre_account (luminosity_t val)
  {
    /* Check that pre_account is not done after finalizing.  */
    assert (!colorscreen_checking || m_inv == 0);
    m_minval = std::min (m_minval, val);
    m_maxval = std::max (m_maxval, val);
  }

  /* Once range is known allocate the histogram.  */
  inline void
  finalize_range (int nvals)
  {
    m_inv = nvals / (m_maxval - m_minval);
    if (!(m_inv > 0))
      m_inv = 1;
    m_entries.resize (nvals, 0);
  }

  /* Alternative way to initialize histogram if range is known.  */
  inline void
  set_range (luminosity_t minval, luminosity_t maxval, int nvals)
  {
    if (m_minval != m_maxval)
      abort ();
    m_minval = minval;
    m_maxval = maxval;
    finalize_range (nvals);
    // std::fill (m_entries.begin (), m_entries.end (), 0);
  }

  /* Turn value intro index of entry in the value histogram.  */
  pure_attr inline int
  val_to_index (luminosity_t val) const
  {
    assert (!colorscreen_checking || m_inv > 0);
    if (val >= m_minval && val <= m_maxval)
      {
        int entry = (val - m_minval) * m_inv;
        if (entry == (int)m_entries.size ())
          entry--;
        return entry;
      }
    return -1;
  }

  /* Same but always return value in range.  */
  pure_attr inline int
  val_to_index_force (luminosity_t val) const
  {
    assert (!colorscreen_checking || m_inv > 0);
    int entry = (val - m_minval) * m_inv;
    if (entry < 0)
      return 0;
    if (entry >= (int)m_entries.size ())
      return (int)m_entries.size ()-1;
    return entry;
  }

  /* turn in histogram to typical value.  */
  pure_attr inline luminosity_t 
  index_to_val (luminosity_t entry) const
  {
    assert (!colorscreen_checking || m_inv > 0);
    return m_minval
           + (entry + 0.5) * ((m_maxval - m_minval) / m_entries.size ());
  }

  /* Account value to histogram. Return true if it fits in the range and was
     accounted.  */
  inline bool
  account_if_in_range (luminosity_t val)
  {
    if (colorscreen_checking && m_total != -1)
      abort ();
    int idx;
    if ((idx = val_to_index (val)) >= 0)
      {
        m_entries[idx]++;
        return true;
      }
    return false;
  }
  /* Account value to histogram.  */
  inline void
  account (luminosity_t val)
  {
    if (colorscreen_checking && m_total != -1)
      abort ();
    m_entries[val_to_index_force (val)]++;
  }

  /* Finalize data collection.  */
  void
  finalize ()
  {
    if (m_total != -1)
      abort ();
    long sum = 0;
    for (auto n : m_entries)
      sum += n;
    m_total = sum;
  }
  void
  find_min_max (luminosity_t skip_min, luminosity_t skip_max, int &mini, int &maxi) const
  {
    unsigned long sum1 = 0;
    unsigned long threshold = (m_total * skip_min) + 0.5;
    for (mini = 0; mini < (int)m_entries.size (); mini++)
      {
        if (sum1 + m_entries[mini] > threshold)
          break;
        sum1 += m_entries[mini];
      }
    unsigned long sum2 = 0;
    threshold = (m_total * skip_max) + 0.5;
    for (maxi = (int)m_entries.size () - 1; maxi >= 0; maxi--)
      {
        if (sum2 + m_entries[maxi] > threshold)
          break;
        sum2 += m_entries[maxi];
      }
    if (sum1 + sum2 >= (unsigned)m_total)
      abort ();
    if (mini > maxi)
      abort ();
  }

  void
  dump (FILE *out, luminosity_t skip_min = 0, luminosity_t skip_max = 0)
  {
    if (m_total == -1)
      {
	fprintf (out, "Histogram is not finalized\n");
      }
    if (m_total == 0)
      {
	fprintf (out, "Histogram is empty\n");
      }
    fprintf (out, "Histogram entries\n");
    int mini, maxi;
    find_min_max (skip_min, skip_max, mini, maxi);
    long sum = 0;
    for (int i = 0; i < (int)m_entries.size (); i++)
      {
	if (i == mini)
	  fprintf (out, "First entry accounted\n");
	sum += m_entries[i];
        fprintf (out, "  entry %i count %li %2.2f%% cummulative %li %2.2f%% range [%f,%f)\n",i, m_entries[i], m_entries[i] * 100.0 / m_total, sum, sum * 100.0 / m_total, index_to_val (i),index_to_val (i+1));
	if (i == maxi)
	  fprintf (out, "Last entry accounted\n");

      }
  }

  /* Find minimal value after cutting off total*skip elements.  */
  pure_attr luminosity_t
  find_min (luminosity_t skip) const
  {
    if (m_total <= 0)
      abort ();
    if (!skip)
      return m_minval;
    long threshold = (m_total * skip) + 0.5;
    long sum = 0;
    // printf ("Threshold %i total %i\n", threshold, m_total);
    for (int i = 0; i < (int)m_entries.size (); i++)
      {
        sum += m_entries[i];
        // printf ("%i %i %i %f\n",i,sum, m_entries[i], index_to_val (i));
        if (sum > threshold)
          return i ? index_to_val (i - 1) : m_minval;
      }
    return m_minval;
  }
  /* Find maximal value after cutting off total*skip elements.  */
  pure_attr luminosity_t
  find_max (luminosity_t skip) const
  {
    if (m_total <= 0)
      abort ();
    if (!skip)
      return m_maxval;
    long threshold = (m_total * skip) + 0.5;
    long sum = 0;
    for (int i = (int)m_entries.size () - 1; i >= 0; i--)
      {
        sum += m_entries[i];
        if (sum > threshold)
          return i == (int)m_entries.size () - 1 ? m_maxval
                                                 : index_to_val (i + 1);
      }
    return m_maxval;
  }
  /* return robust average skiping skip_min & skip_max ratio of extreme points.
   */
  pure_attr luminosity_t
  find_avg (luminosity_t skip_min = 0, luminosity_t skip_max = 0) const
  {
    if (m_total <= 0)
      abort ();
    double wsum = 0;
    int mini, maxi;
    find_min_max (skip_min, skip_max, mini, maxi);
    long sum = 0;
    for (int i = mini; i <= maxi; i++)
      {
        wsum += m_entries[i] * (double)i;
        sum += m_entries[i];
      }
    return index_to_val (wsum / ((luminosity_t)sum));
  }

  int
  num_samples () const
  {
    return m_total;
  }

private:
  luminosity_t m_minval, m_maxval;
  luminosity_t m_inv;
  std::vector<unsigned long> m_entries;
  long m_total;
};

class rgb_histogram
{
public:
  rgb_histogram () {}
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
  inline bool
  account_if_in_range (rgbdata val)
  {
    if (m_histogram[0].val_to_index (val[0]) < 0
        || m_histogram[1].val_to_index (val[1]) < 0
        || m_histogram[2].val_to_index (val[2]) < 0)
      return false;
    m_histogram[0].account_if_in_range (val[0]);
    m_histogram[1].account_if_in_range (val[1]);
    m_histogram[2].account_if_in_range (val[2]);
    return true;
  }
  pure_attr rgbdata
  find_min (luminosity_t skip) const
  {
    return { m_histogram[0].find_min (skip), m_histogram[1].find_min (skip),
             m_histogram[2].find_min (skip) };
  }
  pure_attr rgbdata
  find_max (luminosity_t skip) const
  {
    return { m_histogram[0].find_max (skip), m_histogram[1].find_max (skip),
             m_histogram[2].find_max (skip) };
  }
  pure_attr rgbdata
  find_avg (luminosity_t skipmin = 0, luminosity_t skipmax = 0) const
  {
    return { m_histogram[0].find_avg (skipmin, skipmax),
             m_histogram[1].find_avg (skipmin, skipmax),
             m_histogram[2].find_avg (skipmin, skipmax) };
  }

  pure_attr int
  num_samples () const
  {
    return std::min (std::min (m_histogram[0].num_samples (),
                               m_histogram[1].num_samples ()),
                     m_histogram[2].num_samples ());
  }

private:
  histogram m_histogram[3];
};
}
#endif
