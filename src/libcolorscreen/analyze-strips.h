#ifndef ANALYZE_STRIPS_H
#define ANALYZE_STRIPS__H
#include "include/strips.h"
#include "render-to-scr.h"
#include "analyze-base.h"
namespace colorscreen
{
template class analyze_base_worker <strips_geometry>;

class analyze_strips : public analyze_base_worker <strips_geometry>
{
public:
  analyze_strips()
  : analyze_base_worker (0, 0, 0, 0, 0, 0)
  {
  }
  ~analyze_strips();
private:
};
}
#endif
