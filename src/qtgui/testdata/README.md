# Legacy parameter import fixture

`parameter-v1.cspar` is an immutable ZIP-v1 reader fixture generated with
Python's `zipfile`, independently of the retired application writer. It has
`manifest.json` and `state/legacy.par`: gamma 1.7, one profile spot (0.25, 0.5),
final-frame angle 108.375 and ratio 0.803158, photographic bounds (2,3,16,17),
ignore-IR, nearest scaling and D65 observer whitepoint. Historical XYZ/gamma/
gamut fields are deliberately nondefault: they must be parsed but not applied
as saved document parameters. Its ZIP-entry timestamps are fixed.

The Qt resource embeds this tiny input so document lifecycle smoke tests run
in installed/package binaries without access to the source tree. There is no
product ZIP encoder or test-only reintroduction of that encoder.
