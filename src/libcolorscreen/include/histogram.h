#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <limits>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <omp.h>
#include "base.h"

namespace colorscreen
{

/* Basic datastructure for value histograms.  */
class histogram
{
public:
  /* Use double to avoid roundoff errors.  */
  using entry_t = double;
  /* Construct an empty histogram.  */
  histogram ()
      : m_minval (std::numeric_limits<entry_t>::max ()),
        m_maxval (std::numeric_limits<entry_t>::lowest ()), m_inv (0),
        m_total (-1)
  {
  }

  /* Adjust range so VAL will fit in.  */
  inline void
  pre_account (entry_t val)
  {
    /* Check that pre_account is not done after finalizing.  */
    assert (!colorscreen_checking || m_inv == 0);
    m_minval = std::min (m_minval, val);
    m_maxval = std::max (m_maxval, val);
  }

  /* Merge range of OTHER histogram into this one.  */
  inline void
  merge_range (const histogram &other)
  {
    m_minval = std::min (m_minval, other.m_minval);
    m_maxval = std::max (m_maxval, other.m_maxval);
  }

  /* Once range is known allocate the entries.  NVALS is the number of buckets.  */
  inline void
  finalize_range (int nvals)
  {
    if (m_maxval <= m_minval)
      m_inv = 1;
    else
      m_inv = nvals / (m_maxval - m_minval);
    if (!(m_inv > 0))
      m_inv = 1;
    m_entries.assign (nvals, 0);
  }

  /* Alternative way to initialize histogram if range is known.
     Initialize range to [MINVAL, MAXVAL] with NVALS buckets.  */
  inline void
  set_range (entry_t minval, entry_t maxval, int nvals)
  {
    m_minval = minval;
    m_maxval = maxval;
    finalize_range (nvals);
  }

  /* Returns a new histogram with the same range and configuration as this one
     but with zeroed entries. Useful for OpenMP reduction initializers.  */
  histogram
  clone_empty () const
  {
    histogram h;
    h.m_minval = m_minval;
    h.m_maxval = m_maxval;
    h.m_inv = m_inv;
    h.m_entries.assign (m_entries.size (), 0);
    h.m_total = -1;
    return h;
  }

  /* Turn value VAL intro index of entry in the value histogram.  */
  pure_attr inline int
  val_to_index (entry_t val) const
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

  /* Same as val_to_index but always return index in range [0, N_ENTRIES-1].  */
  pure_attr inline int
  val_to_index_force (entry_t val) const
  {
    assert (!colorscreen_checking || m_inv > 0);
    int entry = (val - m_minval) * m_inv;
    if (entry < 0)
      return 0;
    if (entry >= (int)m_entries.size ())
      return (int)m_entries.size () - 1;
    return entry;
  }

  /* Turn index ENTRY back into a typical value in its range.  */
  pure_attr inline entry_t
  index_to_val (size_t entry) const
  {
    assert (!colorscreen_checking || m_inv > 0);
    return m_minval
           + (entry + 0.5) * ((m_maxval - m_minval) / m_entries.size ());
  }

  /* Account value VAL to histogram. Return true if it fits in the range and was
     accounted.  */
  inline bool
  account_if_in_range (entry_t val)
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

  /* Account value VAL to histogram, clamping to range if necessary.  */
  inline void
  account (entry_t val)
  {
    if (colorscreen_checking && m_total != -1)
      abort ();
    m_entries[val_to_index_force (val)]++;
  }

  /* Merge entry counts from OTHER histogram into this one.  */
  inline void
  merge_entries (const histogram &other)
  {
    if (m_entries.size () != other.m_entries.size ())
      abort ();
    for (size_t i = 0; i < m_entries.size (); i++)
      m_entries[i] += other.m_entries[i];
  }

  /* Finalize data collection and compute total sample count.  */
  void
  finalize ()
  {
    if (m_total != -1)
      abort ();
    uint64_t sum = 0;
    for (auto n : m_entries)
      sum += n;
    m_total = (int64_t)sum;
  }

  /* Find bucket indices MINI and MAXI after skipping SKIP_MIN and SKIP_MAX
     ratios of samples.  */
  void
  find_min_max (entry_t skip_min, entry_t skip_max, int &mini,
                int &maxi) const
  {
    uint64_t sum1 = 0;
    uint64_t threshold = (m_total * skip_min) + 0.5;
    for (mini = 0; mini < (int)m_entries.size (); mini++)
      {
        if (sum1 + m_entries[mini] > threshold)
          break;
        sum1 += m_entries[mini];
      }
    uint64_t sum2 = 0;
    threshold = (m_total * skip_max) + 0.5;
    for (maxi = (int)m_entries.size () - 1; maxi >= 0; maxi--)
      {
        if (sum2 + m_entries[maxi] > threshold)
          break;
        sum2 += m_entries[maxi];
      }
    if (sum1 + sum2 >= (uint64_t)m_total && m_total > 0)
      abort ();
    if (mini > maxi && m_total > 0)
      maxi = mini;
  }

  /* Dump histogram content to file OUT, skipping SKIP_MIN and SKIP_MAX parts.  */
  void
  dump (FILE *out, entry_t skip_min = 0, entry_t skip_max = 0) const
  {
    if (m_total == -1)
      fprintf (out, "Histogram is not finalized\n");
    if (m_total == 0)
      fprintf (out, "Histogram is empty\n");

    fprintf (out, "Histogram entries\n");
    int mini, maxi;
    find_min_max (skip_min, skip_max, mini, maxi);
    uint64_t sum = 0;
    for (size_t i = 0; i < m_entries.size (); i++)
      {
        if ((int)i == mini)
          fprintf (out, "First entry accounted\n");
        sum += m_entries[i];
        fprintf (out,
                 "  entry %zu count %llu %2.2f%% cummulative %llu %2.2f%% range "
                 "[%f,%f)\n",
                 i, (unsigned long long)m_entries[i],
                 m_entries[i] * 100.0 / m_total, (unsigned long long)sum,
                 sum * 100.0 / m_total, (double)index_to_val (i),
                 (double)index_to_val (i + 1));
        if ((int)i == maxi)
          fprintf (out, "Last entry accounted\n");
      }
  }

  /* Find minimal value after cutting off SKIP ratio of samples.  */
  pure_attr entry_t
  find_min (entry_t skip) const
  {
    if (!skip)
      return m_minval;
    if (m_total <= 0)
      abort ();
    uint64_t threshold = (m_total * skip) + 0.5;
    uint64_t sum = 0;
    for (size_t i = 0; i < m_entries.size (); i++)
      {
        sum += m_entries[i];
        if (sum > threshold)
          return i ? index_to_val (i - 1) : m_minval;
      }
    return m_minval;
  }

  /* Find maximal value after cutting off SKIP ratio of samples.  */
  pure_attr entry_t
  find_max (entry_t skip) const
  {
    if (!skip)
      return m_maxval;
    if (m_total <= 0)
      abort ();
    uint64_t threshold = (m_total * skip) + 0.5;
    uint64_t sum = 0;
    for (int i = (int)m_entries.size () - 1; i >= 0; i--)
      {
        sum += m_entries[i];
        if (sum > threshold)
          return i == (int)m_entries.size () - 1 ? m_maxval
                                                 : index_to_val (i + 1);
      }
    return m_maxval;
  }

  /* Return average value skipping SKIP_MIN and SKIP_MAX extreme points.  */
  pure_attr entry_t
  find_avg (entry_t skip_min = 0, entry_t skip_max = 0) const
  {
    if (m_total <= 0)
      abort ();
    double wsum = 0;
    int mini, maxi;
    find_min_max (skip_min, skip_max, mini, maxi);
    uint64_t sum = 0;
    for (int i = mini; i <= maxi; i++)
      {
        wsum += m_entries[i] * (double)i;
        sum += m_entries[i];
      }
    if (!sum)
      return m_minval;
    return index_to_val (wsum / (double)sum);
  }

  /* Return total number of samples collected.  */
  pure_attr int
  num_samples () const
  {
    return (int)m_total;
  }

  /* Return number of buckets in the histogram.  */
  pure_attr size_t
  n_entries () const
  {
    return m_entries.size ();
  }

  /* Return count in bucket I.  */
  pure_attr uint64_t
  entry (size_t i) const
  {
    return m_entries[i];
  }

  /* Reset the histogram state.  */
  void
  reset ()
  {
    m_minval = std::numeric_limits<entry_t>::max ();
    m_maxval = std::numeric_limits<entry_t>::lowest ();
    m_inv = 0;
    m_entries.clear ();
    m_total = -1;
  }

  /* Default move semantics.  */
  histogram (histogram &&) = default;
  histogram &operator= (histogram &&) = default;

  /* Disable copying.  */
  histogram (const histogram &) = delete;
  histogram &operator= (const histogram &) = delete;

private:
  entry_t m_minval, m_maxval;
  entry_t m_inv;
  std::vector<uint64_t> m_entries;
  int64_t m_total;

  static constexpr const bool debug = colorscreen_checking;
};

/* RGB Histogram class.  */
class rgb_histogram
{
public:
  rgb_histogram () {}

  /* Accumulate range for value VAL.  */
  inline void
  pre_account (rgbdata val)
  {
    m_histogram[0].pre_account (val[0]);
    m_histogram[1].pre_account (val[1]);
    m_histogram[2].pre_account (val[2]);
  }

  /* Merge range for value OTHER.  */
  inline void
  merge_range (const rgb_histogram &other)
  {
    m_histogram[0].merge_range (other.m_histogram[0]);
    m_histogram[1].merge_range (other.m_histogram[1]);
    m_histogram[2].merge_range (other.m_histogram[2]);
  }

  /* Finalize range with NVALS buckets.  */
  inline void
  finalize_range (int nvals)
  {
    m_histogram[0].finalize_range (nvals);
    m_histogram[1].finalize_range (nvals);
    m_histogram[2].finalize_range (nvals);
  }

  /* Set range to [MINVAL, MAXVAL] with NVALS buckets.  */
  inline void
  set_range (rgbdata minval, rgbdata maxval, int nvals)
  {
    m_histogram[0].set_range (minval[0], maxval[0], nvals);
    m_histogram[1].set_range (minval[1], maxval[1], nvals);
    m_histogram[2].set_range (minval[2], maxval[2], nvals);
  }

  /* Return empty clone of the histogram.  */
  rgb_histogram
  clone_empty () const
  {
    rgb_histogram h;
    h.m_histogram[0] = m_histogram[0].clone_empty ();
    h.m_histogram[1] = m_histogram[1].clone_empty ();
    h.m_histogram[2] = m_histogram[2].clone_empty ();
    return h;
  }

  /* Finalize the histogram collection.  */
  void
  finalize ()
  {
    m_histogram[0].finalize ();
    m_histogram[1].finalize ();
    m_histogram[2].finalize ();
  }

  /* Account value VAL to histogram.  */
  inline void
  account (rgbdata val)
  {
    m_histogram[0].account (val[0]);
    m_histogram[1].account (val[1]);
    m_histogram[2].account (val[2]);
  }

  /* Merge counts from OTHER histogram.  */
  inline void
  merge_entries (const rgb_histogram &other)
  {
    m_histogram[0].merge_entries (other.m_histogram[0]);
    m_histogram[1].merge_entries (other.m_histogram[1]);
    m_histogram[2].merge_entries (other.m_histogram[2]);
  }

  /* Account value VAL if it fits in range.  */
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

  /* Find minimal values after skipping SKIP.  */
  pure_attr rgbdata
  find_min (histogram::entry_t skip) const
  {
    return { (luminosity_t)m_histogram[0].find_min (skip),
	     (luminosity_t)m_histogram[1].find_min (skip),
             (luminosity_t)m_histogram[2].find_min (skip) };
  }

  /* Find maximal values after skipping SKIP.  */
  pure_attr rgbdata
  find_max (histogram::entry_t skip) const
  {
    return { (luminosity_t)m_histogram[0].find_max (skip),
	     (luminosity_t)m_histogram[1].find_max (skip),
             (luminosity_t)m_histogram[2].find_max (skip) };
  }

  /* Find average values after skipping extreme points.  */
  pure_attr rgbdata
  find_avg (histogram::entry_t skipmin = 0, histogram::entry_t skipmax = 0) const
  {
    return { (luminosity_t)m_histogram[0].find_avg (skipmin, skipmax),
             (luminosity_t)m_histogram[1].find_avg (skipmin, skipmax),
             (luminosity_t)m_histogram[2].find_avg (skipmin, skipmax) };
  }

  /* Return number of samples.  */
  pure_attr int
  num_samples () const
  {
    return std::min ({ (luminosity_t)m_histogram[0].num_samples (),
                       (luminosity_t)m_histogram[1].num_samples (),
                       (luminosity_t)m_histogram[2].num_samples () });
  }

  /* Default move semantics.  */
  rgb_histogram (rgb_histogram &&) = default;
  rgb_histogram &operator= (rgb_histogram &&) = default;

  /* Disable copying.  */
  rgb_histogram (const rgb_histogram &) = delete;
  rgb_histogram &operator= (const rgb_histogram &) = delete;

private:
  histogram m_histogram[3];
};

/* OpenMP Reductions.  */
#pragma omp declare reduction(histogram_range : histogram : omp_out.merge_range(omp_in))
#pragma omp declare reduction(histogram_entries : histogram : omp_out.merge_entries(omp_in)) \
    initializer(omp_priv = omp_orig.clone_empty())

#pragma omp declare reduction(rgb_histogram_range : rgb_histogram : omp_out.merge_range(omp_in))
#pragma omp declare reduction(rgb_histogram_entries : rgb_histogram : omp_out.merge_entries(omp_in)) \
    initializer(omp_priv = omp_orig.clone_empty())

} // namespace colorscreen

#endif
