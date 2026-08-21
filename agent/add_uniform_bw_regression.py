from pathlib import Path
import re
root=Path('.')
fin=root/'src/libcolorscreen/finetune.C'
inth=root/'src/libcolorscreen/finetune-int.h'
ut=root/'src/libcolorscreen/unittests.C'
s=fin.read_text()
class_marker='class finetune_solver\n'
pos=s.find(class_marker)
if pos<0:
    raise RuntimeError('finetune_solver class not found')
helper='''/* Solve the block-diagonal least-squares problem used by joint BW/IR
   fitting.  Each tile owns three consecutive columns describing its uniform
   image-layer response to the historical screen primaries.  Store the fitted
   coefficients in COLORS and the optional squared residual in CHISQ.  */
static bool
solve_uniform_bw_least_squares (gsl_matrix *x, gsl_vector *y, gsl_vector *c,
                                gsl_matrix *cov,
                                gsl_multifit_linear_workspace *work,
                                int n_tiles, luminosity_t maxgray,
                                double_rgbdata *colors, double *chisq)
{
  if (!x || !y || !c || !cov || !work || !colors || n_tiles < 1
      || !my_isfinite (maxgray) || maxgray <= 0
      || x->size2 != (size_t)(3 * n_tiles) || c->size != x->size2
      || cov->size1 != x->size2 || cov->size2 != x->size2)
    return false;

  double local_chisq;
  if (gsl_multifit_linear (x, y, c, cov, &local_chisq, work) != GSL_SUCCESS
      || !std::isfinite (local_chisq))
    return false;

  for (int tileid = 0; tileid < n_tiles; tileid++)
    {
      colors[tileid]
          = { (luminosity_t)gsl_vector_get (c, 3 * tileid)
                  * (2 * maxgray),
              (luminosity_t)gsl_vector_get (c, 3 * tileid + 1)
                  * (2 * maxgray),
              (luminosity_t)gsl_vector_get (c, 3 * tileid + 2)
                  * (2 * maxgray) };
      if (!my_isfinite (colors[tileid].red)
          || !my_isfinite (colors[tileid].green)
          || !my_isfinite (colors[tileid].blue))
        return false;
    }
  if (chisq)
    *chisq = local_chisq;
  return true;
}

'''
if 'solve_uniform_bw_least_squares (' in s:
    raise RuntimeError('helper already present')
s=s[:pos]+helper+s[pos:]

def span(text,name):
    m=re.search(r'\n(?:[^\n]*\n){0,3}\s*'+re.escape(name)+r'\s*\(',text)
    if not m: raise RuntimeError(name+' not found')
    a=m.start()+1; b0=text.find('{',m.end()); d=0; i=b0
    ins=inc=inl=inb=False; esc=False
    while i<len(text):
        ch=text[i]; nx=text[i+1] if i+1<len(text) else ''
        if inl:
            if ch=='\n': inl=False
        elif inb:
            if ch=='*' and nx=='/': inb=False; i+=1
        elif ins:
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch=='"': ins=False
        elif inc:
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch=="'": inc=False
        else:
            if ch=='/' and nx=='/': inl=True; i+=1
            elif ch=='/' and nx=='*': inb=True; i+=1
            elif ch=='"': ins=True
            elif ch=="'": inc=True
            elif ch=='{': d+=1
            elif ch=='}':
                d-=1
                if d==0:return a,i+1
        i+=1
    raise RuntimeError('unclosed '+name)

a,b=span(s,'bw_determine_color_using_least_squares')
f=s[a:b]
start=f.find('    double chisq;')
if start<0: raise RuntimeError('production BW solve body not found')
ret=list(re.finditer(r'\n\s*return\s+last_tile_color\[0\]\s*;',f[start:]))
if len(ret)!=1: raise RuntimeError('unexpected production return')
end=start+ret[0].end()
new='''    double chisq;
    if (!solve_uniform_bw_least_squares (
            gsl_X.get (), gsl_y[0].get (), gsl_c.get (), gsl_cov.get (),
            gsl_work.get (), n_tiles, maxgray, last_tile_color.data (),
            &chisq))
      {
        for (int tileid = 0; tileid < n_tiles; tileid++)
          last_tile_color[tileid] = { -1, -1, -1 };
      }
    return last_tile_color[0];'''
f=f[:start]+new+f[end:]
s=s[:a]+f+s[b:]

insert=s.rfind('} // namespace colorscreen')
if insert<0:
    insert=s.rfind('\n}')
if insert<0: raise RuntimeError('namespace end not found')
reg='''/* Verify the uniform-colour multi-tile profile independently of image
   loading and geometry.  A single red tile is deliberately compatible with
   both candidate transfers.  Adding independent green and blue tiles must
   select the correct transfer, recover one colour per tile, and remain
   invariant under tile permutation.  */
bool
finetune_test_uniform_bw_multitile_model ()
{
  constexpr int samples = 6;
  constexpr int max_tiles = 3;
  const double sharp[samples][3]
      = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
          { 0.5, 0.5, 0 }, { 0, 0.5, 0.5 }, { 0.5, 0, 0.5 } };
  const double competing[samples][3]
      = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
          { 0.5, 1, 0 }, { 0, 0, 1 }, { 0.5, 0, 0 } };
  const rgbdata diverse[max_tiles]
      = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  const rgbdata repeated[max_tiles]
      = { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } };

  const auto profile = [&] (const double basis[samples][3],
                            const rgbdata *truth, const int *order,
                            int n_tiles, double_rgbdata *fitted,
                            double *chisq) -> bool
  {
    const int rows = samples * n_tiles;
    const int columns = 3 * n_tiles;
    std::unique_ptr<gsl_matrix, gsl_matrix_deleter> x (
        gsl_matrix_calloc (rows, columns));
    std::unique_ptr<gsl_vector, gsl_vector_deleter> y (
        gsl_vector_alloc (rows));
    std::unique_ptr<gsl_vector, gsl_vector_deleter> c (
        gsl_vector_alloc (columns));
    std::unique_ptr<gsl_matrix, gsl_matrix_deleter> cov (
        gsl_matrix_alloc (columns, columns));
    std::unique_ptr<gsl_multifit_linear_workspace, gsl_work_deleter> work (
        gsl_multifit_linear_alloc (rows, columns));
    if (!x || !y || !c || !cov || !work)
      return false;

    for (int output_tile = 0; output_tile < n_tiles; output_tile++)
      {
        const int input_tile = order ? order[output_tile] : output_tile;
        for (int row = 0; row < samples; row++)
          {
            const int equation = output_tile * samples + row;
            for (int primary = 0; primary < 3; primary++)
              gsl_matrix_set (x.get (), equation,
                              3 * output_tile + primary,
                              basis[row][primary]);
            const rgbdata color = truth[input_tile];
            gsl_vector_set (y.get (), equation,
                            sharp[row][0] * color.red
                                + sharp[row][1] * color.green
                                + sharp[row][2] * color.blue);
          }
      }
    return solve_uniform_bw_least_squares (
        x.get (), y.get (), c.get (), cov.get (), work.get (), n_tiles,
        (luminosity_t)0.5, fitted, chisq);
  };

  double_rgbdata fitted[max_tiles];
  double sharp_error, competing_error, repeated_error, one_error;
  if (!profile (sharp, diverse, nullptr, max_tiles, fitted, &sharp_error)
      || sharp_error > 1e-20)
    return false;
  for (int tileid = 0; tileid < max_tiles; tileid++)
    for (int primary = 0; primary < 3; primary++)
      if (my_fabs (fitted[tileid][primary] - diverse[tileid][primary])
          > 1e-10)
        return false;

  if (!profile (competing, diverse, nullptr, 1, fitted, &one_error)
      || one_error > 1e-20)
    return false;
  if (!profile (competing, repeated, nullptr, max_tiles, fitted,
                &repeated_error)
      || repeated_error > 1e-20)
    return false;
  if (!profile (competing, diverse, nullptr, max_tiles, fitted,
                &competing_error)
      || competing_error <= 0.1)
    return false;

  const int permutation[max_tiles] = { 2, 0, 1 };
  if (!profile (sharp, diverse, permutation, max_tiles, fitted, &sharp_error)
      || sharp_error > 1e-20)
    return false;
  for (int tileid = 0; tileid < max_tiles; tileid++)
    for (int primary = 0; primary < 3; primary++)
      if (my_fabs (fitted[tileid][primary]
                   - diverse[permutation[tileid]][primary])
          > 1e-10)
        return false;
  return true;
}

'''
if 'finetune_test_uniform_bw_multitile_model' in s:
    raise RuntimeError('regression already present')
s=s[:insert]+reg+s[insert:]
fin.write_text(s)

h=inth.read_text()
idx=h.rfind('}')
if idx<0: raise RuntimeError('finetune-int namespace end not found')
decl='''
/* Internal regression for the profiled uniform-colour BW/IR multi-tile
   model.  */
DLL_PUBLIC bool finetune_test_uniform_bw_multitile_model ();

'''
if 'finetune_test_uniform_bw_multitile_model' in h:
    raise RuntimeError('declaration already present')
h=h[:idx]+decl+h[idx:]
inth.write_text(h)

u=ut.read_text()
marker='    { "denoising", "denoising tests"'
idx=u.find(marker)
if idx<0:
    raise RuntimeError('unit registry marker not found')
entry='''    { "finetune_uniform_bw",
      "uniform-colour multi-tile focus-model tests",
      [] () { return finetune_test_uniform_bw_multitile_model (); } },
'''
if '"finetune_uniform_bw"' in u:
    raise RuntimeError('unit already registered')
u=u[:idx]+entry+u[idx:]
ut.write_text(u)
