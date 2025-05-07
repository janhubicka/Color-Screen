#include "analyze-base-worker.h"
#include "analyze-strips.h"
#include "screen.h"
namespace colorscreen
{
analyze_strips::~analyze_strips ()
{
}
bool
analyze_strips::dump_patch_density (FILE *out)
{
  fprintf (out, "Paget dimenstion: %i %i\n", m_width, m_height);
  fprintf (out, "Strip1 %i %i\n", m_width , m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", red (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Strip2 %i %i\n", m_width , m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", green (x, y));
      fprintf (out, "\n");
    }
  fprintf (out, "Strip3 %i %i\n", m_width, m_height);
  for (int y = 0; y < m_height; y++)
    {
      for (int x = 0; x < m_width; x++)
	fprintf (out, "  %f", blue (x, y));
      fprintf (out, "\n");
    }
  return true;
}
}
