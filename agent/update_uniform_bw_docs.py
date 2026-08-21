from pathlib import Path
import re

solver=Path('doc/finetune-solver.md')
s=solver.read_text()
pat=re.compile(r'(A multi-tile BW fit.*?shared across tiles\.\n)',re.S)
m=pat.search(s)
if not m:
    raise RuntimeError('BW multi-tile paragraph not found')
pos=m.end()
para=('\nWithin a homogeneous BW/IR joint fit, every input location is assumed to be\n'
      'locally uniform but may represent a different image colour.  The capture\n'
      'blur/focus and screen geometry are shared, while three image-layer primary\n'
      'intensities are variable-projected independently for each tile.  This is\n'
      'the model used by the planned automatic focus-area workflow: differently\n'
      'coloured uniform regions constrain one common transfer without forcing\n'
      'their underlying image colours to agree.  The historical `color` result\n'
      'continues to report the first tile; `tile_colors` reports the complete\n'
      'ordered set.\n')
if 'The historical `color` result' not in s:
    s=s[:pos]+para+s[pos:]
s=s.replace('- grayscale primary intensities;\n',
            '- grayscale/IR primary intensities, independently for each uniform input tile;\n')
solver.write_text(s)

tracker=Path('doc/finetune-tracking.md')
t=tracker.read_text()
ids=[int(x) for x in re.findall(r'### FT-(\d+)',t)]
next_id=max(ids)+1
entry=f'''### FT-{next_id:03d} — joint BW/IR fitting pooled different uniform colours

**Severity:** high focus-model correctness

**Status:** fixed

The multi-location API accepts up to eight BW/IR tiles, and every tile is
assumed locally uniform.  The least-squares model nevertheless allocated one
coefficient while reading three, then returned one shared primary-intensity
triple for every tile.  Besides the out-of-bounds coefficient access, this
forced differently coloured image regions to impersonate one common colour and
removed the intended multi-tile constraint on blur/focus.

Joint BW/IR fitting now uses a block-diagonal linear model with three
coefficients per tile.  Blur/focus and the simulated screen stay shared, while
each tile receives independent uniform image-layer primary intensities.  The
single-tile collection estimator is unchanged; genuine joint fits use the exact
least-squares projection.  `finetune_result::color` remains the first-tile
compatibility value and `tile_colors` returns all fitted triples.

A synthetic regression includes a colour that is exactly compatible with both
a reference and a competing transfer.  Repeating that colour remains
ambiguous, while adding independent green and blue tiles rejects the competing
transfer.  It also checks coefficient recovery and invariance under tile
permutation.

'''
if 'joint BW/IR fitting pooled different uniform colours' not in t:
    marker='## Open correctness and numerical issues'
    idx=t.find(marker)
    if idx<0: raise RuntimeError('open correctness marker not found')
    t=t[:idx]+entry+t[idx:]

pat=re.compile(r'(### FT-033 — multi-tile result fields describe only the first tile\n\n'
               r'\*\*Severity:\*\*.*?\n\n\*\*Status:\*\*) open',re.S)
t,n=pat.subn(r'\1 partially fixed',t,count=1)
if n==1:
    marker='### FT-033 — multi-tile result fields describe only the first tile'
    start=t.index(marker)
    nextsec=t.find('\n### FT-',start+len(marker))
    if nextsec<0: nextsec=len(t)
    section=t[start:nextsec]
    add=('\nBW/IR fits now return all per-tile uniform primary intensities in\n'
         '`finetune_result::tile_colors`, while preserving `color` for the first\n'
         'tile.  Per-tile phase/geometry and the remaining multi-tile result\n'
         'fields are still not represented, so the broader API limitation remains.\n')
    if '`finetune_result::tile_colors`' not in section:
        t=t[:nextsec]+add+t[nextsec:]
elif '### FT-033' in t and '`finetune_result::tile_colors`' not in t:
    raise RuntimeError('FT-033 status form unexpected')
tracker.write_text(t)
