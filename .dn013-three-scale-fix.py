from pathlib import Path
p = Path('src/libcolorscreen/unittests.C')
s = p.read_text()
replacements = [
    ('"Three-scale noise estimator mismatch: ratios %g %g paired %zu\n",',
     '"Three-scale noise estimator mismatch: ratios %g %g paired %zu\\n",'),
    ('"RGB three-scale estimator mismatch: ratios %g %g paired %zu\n",',
     '"RGB three-scale estimator mismatch: ratios %g %g paired %zu\\n",'),
    ('"Paget three-scale estimator mismatch: ratios %g %g paired %zu\n",',
     '"Paget three-scale estimator mismatch: ratios %g %g paired %zu\\n",'),
    ('"noise fraction %g paired %zu\n",',
     '"noise fraction %g paired %zu\\n",'),
]
for old, new in replacements:
    assert old in s, repr(old)
    s = s.replace(old, new, 1)
p.write_text(s)
print('three-scale diagnostic format strings fixed')
