# Color-Screen documentation — wiki sources

These Markdown files are the reviewed sources for user-facing
[Color-Screen wiki](https://github.com/janhubicka/Color-Screen/wiki) pages.

- [Home.md](Home.md) — current 2.0 landing page template
- [_Sidebar.md](_Sidebar.md) — current navigation block
- [2.0 user manual](Color-Screen-2.0-User-Manual.md)
- [2.0 parameter-file format](Color-Screen-2.0-Parameter-Format.md)
- [2.0 stitching-project JSON format](Color-Screen-2.0-Project-Format.md)
- [Historical 1.0 documentation](Color-Screen-1.0-Documentation.md)

The GitHub Actions `publish-wiki.yml` workflow copies the four versioned
manual/reference pages to the wiki's separate Git repository. It uses
`publish_navigation.py` to replace controlled navigation blocks in
`Home.md` and `_Sidebar.md` while preserving manual edits outside them.
The original wiki Home is saved once as
`Color-Screen-1.0-Original-Home.md`, and old articles are enumerated in
the 1.0 historical index. **Old wiki files are never deleted or renamed.**

To run the navigation regression suite without GitHub access:

```sh
python3 -m unittest discover -s doc/wiki/tests -v
```

Pull requests run those tests without publishing. On pushes to `main`,
the publisher tests again, clones the existing wiki, modifies only explicitly
listed pages, and rejects any staged deletion. Existing code/docs and
release archives are unaffected by documentation-only changes.
