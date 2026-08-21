from pathlib import Path
import re

root = Path('.')
fin = root/'src/libcolorscreen/finetune.C'
hdr = root/'src/libcolorscreen/include/finetune.h'
s = fin.read_text()
orig = s

def function_span(text, name):
    m = re.search(r'\n(?:[^\n]*\n){0,3}\s*' + re.escape(name) + r'\s*\(', text)
    if not m:
        raise RuntimeError(f'function {name} not found')
    start = m.start()+1
    brace = text.find('{', m.end())
    if brace < 0:
        raise RuntimeError(f'opening brace for {name} not found')
    depth = 0
    i = brace
    in_str = in_char = in_line = in_block = False
    esc = False
    while i < len(text):
        c = text[i]
        n = text[i+1] if i+1 < len(text) else ''
        if in_line:
            if c == '\n': in_line = False
        elif in_block:
            if c == '*' and n == '/': in_block = False; i += 1
        elif in_str:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == '"': in_str = False
        elif in_char:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == "'": in_char = False
        else:
            if c == '/' and n == '/': in_line = True; i += 1
            elif c == '/' and n == '*': in_block = True; i += 1
            elif c == '"': in_str = True
            elif c == "'": in_char = True
            elif c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return start, i+1
        i += 1
    raise RuntimeError(f'unclosed function {name}')

def replace_function(text, name, transform):
    a,b = function_span(text,name)
    old = text[a:b]
    new = transform(old)
    if old == new:
        raise RuntimeError(f'no change in {name}')
    return text[:a]+new+text[b:]

needle = '  double_rgbdata last_red, last_green, last_blue, last_color;'
if s.count(needle) != 1:
    raise RuntimeError('unexpected last_color declaration')
s = s.replace(needle, needle + '\n  std::array<double_rgbdata, max_tiles> last_tile_color;', 1)

def patch_alloc(f):
    old = '    else\n      matrixw = 1;'
    if f.count(old) != 1:
        raise RuntimeError('unexpected BW matrix width')
    return f.replace(old, '    else\n      matrixw = 3 * n_tiles;', 1)
s = replace_function(s, 'alloc_least_squares', patch_alloc)

def patch_init_ls(f):
    marker = '/* In BW mode there is only one equation to compute.  */'
    pos = f.find(marker)
    if pos < 0:
        raise RuntimeError('BW LS marker not found')
    tail = f[pos:]
    old = '        int e = 0;\n        for (int tileid = 0; tileid < n_tiles; tileid++)'
    if tail.count(old) != 1:
        raise RuntimeError('unexpected BW LS loop')
    tail = tail.replace(old,
        '        int e = 0;\n'
        '        gsl_matrix_set_zero (gsl_X.get ());\n'
        '        for (int tileid = 0; tileid < n_tiles; tileid++)', 1)
    for col in range(3):
        pat = re.compile(r'(gsl_matrix_set\s*\(gsl_X\.get\s*\(\),\s*e,\s*)' + str(col) + r'(\s*,)')
        tail, n = pat.subn(r'\g<1>3 * tileid + ' + str(col) + r'\g<2>', tail)
        if n != 1:
            raise RuntimeError(f'expected one BW matrix column {col}, got {n}')
    return f[:pos] + tail
s = replace_function(s, 'init_least_squares', patch_init_ls)

def patch_bw_ls(f):
    start = f.find('    double chisq;')
    if start < 0:
        raise RuntimeError('BW chisq not found')
    matches = list(re.finditer(r'\n\s*return\s+color\s*;', f[start:]))
    if len(matches) != 1:
        raise RuntimeError(f'unexpected BW color return count {len(matches)}')
    end = start + matches[0].end()
    repl = '''    double chisq;
    if (gsl_multifit_linear (gsl_X.get (), gsl_y[0].get (), gsl_c.get (),
                             gsl_cov.get (), &chisq, gsl_work.get ())
        != GSL_SUCCESS)
      {
        for (int tileid = 0; tileid < n_tiles; tileid++)
          last_tile_color[tileid] = { -1, -1, -1 };
        return last_tile_color[0];
      }
    for (int tileid = 0; tileid < n_tiles; tileid++)
      last_tile_color[tileid]
          = { (luminosity_t)gsl_vector_get (gsl_c.get (), 3 * tileid)
                  * (2 * maxgray),
              (luminosity_t)gsl_vector_get (gsl_c.get (), 3 * tileid + 1)
                  * (2 * maxgray),
              (luminosity_t)gsl_vector_get (gsl_c.get (), 3 * tileid + 2)
                  * (2 * maxgray) };
    return last_tile_color[0];'''
    return f[:start] + repl + f[end:]
s = replace_function(s, 'bw_determine_color_using_least_squares', patch_bw_ls)

def patch_init(f):
    lines = f.splitlines(True)
    idxs = [i for i,l in enumerate(lines)
            if re.search(r'\b(data_collection|least_squares)\s*=', l)]
    candidates = []
    for i in idxs:
        window = ''.join(lines[i:i+3])
        if 'finetune_no_data_collection' in window or 'finetune_no_least_squares' in window:
            candidates.append(i)
    if len(candidates) < 2:
        raise RuntimeError('flag assignments not found in init')
    insert_after = max(candidates[:2])
    text = ''.join(lines)
    offset = sum(len(x) for x in lines[:insert_after+1])
    ins = '''
    /* In BW/IR mode every explicitly supplied tile represents a different
       uniform image colour.  The collection estimator pools samples and is
       therefore meaningful only for one tile; joint fits use the exact
       block-diagonal least-squares projection below.  */
    if (n_tiles > 1 && tiles[0].color.empty ())
      data_collection = false;
'''
    return text[:offset] + ins + text[offset:]
s = replace_function(s, 'init', patch_init)

old = '''        if (!tiles[0].color.empty ())
          n_values += 9;
        else
          n_values += 3;'''
if s.count(old) != 1:
    raise RuntimeError('color parameter allocation block not found')
s = s.replace(old, '''        if (!tiles[0].color.empty ())
          n_values += 9;
        else
          n_values += 3 * n_tiles;''', 1)

old = '''        else
          {
            start[color_index] = 0;
            start[color_index + 1] = 0;
            start[color_index + 2] = 0;
          }'''
if s.count(old) != 1:
    raise RuntimeError('BW color start block not found')
s = s.replace(old, '''        else
          for (int tileid = 0; tileid < n_tiles; tileid++)
            {
              start[color_index + 3 * tileid] = 0;
              start[color_index + 3 * tileid + 1] = 0;
              start[color_index + 3 * tileid + 2] = 0;
            }''', 1)

def patch_bw_get(f):
    header_end = f.find('{') + 1
    return f[:header_end] + '''
    if (!least_squares && !data_collection)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        last_tile_color[tileid]
            = { v[color_index + 3 * tileid],
                v[color_index + 3 * tileid + 1],
                v[color_index + 3 * tileid + 2] };
    else if (data_collection)
      {
        assert (!colorscreen_checking || n_tiles == 1);
        last_tile_color[0] = bw_determine_color_using_data_collection (v);
      }
    else
      bw_determine_color_using_least_squares (v);
    last_color = last_tile_color[0];
    return last_color;
  }'''
s = replace_function(s, 'bw_get_color', patch_bw_get)

def patch_eval(f):
    n = f.count('last_color')
    if n < 1:
        raise RuntimeError('evaluate_pixel does not reference last_color')
    return f.replace('last_color', 'last_tile_color[tileid]')
s = replace_function(s, 'evaluate_pixel', patch_eval)

def patch_contrast(f):
    pat = re.compile(
        r'    if \(tiles\[0\]\.color\.empty \(\)\)\n'
        r'      contrast = get_positional_color_contrast \(.*?\);\n'
        r'    else\n', re.S)
    m = pat.search(f)
    if not m:
        raise RuntimeError('BW contrast branch not found')
    repl = '''    if (tiles[0].color.empty ())
      {
        contrast = 0;
        for (int tileid = 0; tileid < n_tiles; tileid++)
          contrast = std::max (
              contrast,
              get_positional_color_contrast (
                  type,
                  { (luminosity_t)last_tile_color[tileid].red,
                    (luminosity_t)last_tile_color[tileid].green,
                    (luminosity_t)last_tile_color[tileid].blue },
                  optimize_coordinates == 2));
      }
    else
'''
    return f[:m.start()] + repl + f[m.end():]
s = replace_function(s, 'compute_contrast', patch_contrast)

needle = '    last_color = { 0, 0, 0 };'
if s.count(needle) != 1:
    raise RuntimeError('last_color initialization not found')
s = s.replace(needle, needle + '''
    for (int tileid = 0; tileid < max_tiles; tileid++)
      last_tile_color[tileid] = { 0, 0, 0 };''', 1)

def patch_results(f):
    pat = re.compile(r'(\n\s*else\n\s*)(ret\.color\s*=\s*bw_get_color\s*\(start\.data \(\)\)\s*;)')
    m = pat.search(f)
    if not m:
        raise RuntimeError('BW result assignment not found')
    repl = m.group(1) + '''{
        ret.color = bw_get_color (start.data ());
        ret.tile_colors.resize (n_tiles);
        for (int tileid = 0; tileid < n_tiles; tileid++)
          ret.tile_colors[tileid]
              = { (luminosity_t)last_tile_color[tileid].red,
                  (luminosity_t)last_tile_color[tileid].green,
                  (luminosity_t)last_tile_color[tileid].blue };
      }'''
    return f[:m.start()] + repl + f[m.end():]
s = replace_function(s, 'set_results', patch_results)

if s == orig:
    raise RuntimeError('finetune.C unchanged')
fin.write_text(s)

h = hdr.read_text()
old = '''  /* Fitted BW patch intensities or legacy color summary.  */
  rgbdata color = { -1, -1, -1 };'''
if h.count(old) != 1:
    raise RuntimeError('finetune_result color field not found')
h = h.replace(old, '''  /* Fitted BW patch intensities for the first tile, retained for source and
     ABI compatibility with the historical single-tile result.  */
  rgbdata color = { -1, -1, -1 };
  /* Uniform image-layer primary intensities fitted independently for every
     BW/IR input tile.  Empty in RGB mode and on failure.  */
  std::vector<rgbdata> tile_colors;''', 1)
if '#include <vector>' not in h:
    marker='#include <string>\n'
    if h.count(marker) != 1:
        raise RuntimeError('string include not found')
    h=h.replace(marker, marker+'#include <vector>\n',1)
hdr.write_text(h)
