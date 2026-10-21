#!/usr/bin/env python3
"""Update Color-Screen Wiki navigation without deleting historical articles.

The wiki is cloned independently by the GitHub Actions workflow. Only the
controlled Home/Sidebar navigation and the 1.0 historical index are edited;
legacy pages retain their names and bodies. Run after copying the five 2.0
wiki pages from doc/wiki into the wiki checkout.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from urllib.parse import quote

START = '<!-- BEGIN COLORSCREEN 2.0 NAVIGATION -->'
END = '<!-- END COLORSCREEN 2.0 NAVIGATION -->'
LEGACY_START = '<!-- BEGIN COLORSCREEN LEGACY PAGE INDEX -->'
LEGACY_END = '<!-- END COLORSCREEN LEGACY PAGE INDEX -->'
ORIGINAL_HOME = 'Color-Screen-1.0-Original-Home.md'
HISTORICAL_INDEX = 'Color-Screen-1.0-Documentation.md'
PUBLISHED = (
    'Color-Screen-2.0-User-Manual.md',
    'Color-Screen-2.0-Parameter-Format.md',
    'Color-Screen-2.0-Parameter-Fields.md',
    'Color-Screen-2.0-Project-Format.md',
    HISTORICAL_INDEX,
)


def managed_template(template: str, path: str) -> str:
    """Require exactly one complete marked block in a source template."""
    if template.count(START) != 1 or template.count(END) != 1:
        raise ValueError(f'{path}: missing or repeated navigation marker')
    if template.index(START) >= template.index(END):
        raise ValueError(f'{path}: navigation markers out of order')
    return template.rstrip() + '\n'


def replace_navigation(previous: str, template: str, path: str) -> str:
    """Replace the generated block, preserving wiki edits outside it."""
    if START not in previous:
        raise ValueError(f'{path}: no existing managed navigation block')
    if previous.count(START) != 1 or previous.count(END) != 1:
        raise ValueError(f'{path}: incomplete or repeated navigation block')
    prefix, tail = previous.split(START, 1)
    _, suffix = tail.split(END, 1)
    return prefix + template.rstrip() + suffix


def write_changed(path: Path, text: str) -> bool:
    """Write one page iff its bytes differ, avoiding gratuitous wiki commits."""
    if path.exists() and path.read_text(encoding='utf-8') == text:
        return False
    path.write_text(text, encoding='utf-8')
    return True


def legacy_page_links(wiki: Path) -> str:
    """Link each preserved root-level historical Markdown article."""
    omitted = set(PUBLISHED) | {'Home.md', '_Sidebar.md', '_Footer.md'}
    candidates = sorted(
        (p for p in wiki.glob('*.md') if p.name not in omitted
         and not p.name.startswith('Color-Screen-2.0-')),
        key=lambda p: p.stem.casefold(),
    )
    if not candidates:
        return 'There are no earlier articles in this wiki.\n'
    lines = []
    for page in candidates:
        stem = page.stem
        url = quote(stem, safe='-_.~')
        label = stem.replace('-', ' ').replace('_', ' ')
        lines.append(f'- [{label}]({url})')
    return '\n'.join(lines) + '\n'


def publish(source: Path, wiki: Path) -> list[str]:
    """Archive old Home once and refresh current navigation and 1.0 index.

    The publisher stages only these known Markdown pages. Other wiki files
    and their original URLs are not renamed, deleted or modified.
    """
    source = source.resolve(strict=True)
    wiki = wiki.resolve(strict=True)
    if source == wiki:
        raise ValueError('source and wiki directories must differ')
    for filename in PUBLISHED:
        if not (wiki / filename).is_file():
            raise ValueError(f'missing already-copied wiki page: {filename}')

    home_template = managed_template(
        (source / 'Home.md').read_text(encoding='utf-8'), 'Home.md'
    )
    sidebar_template = managed_template(
        (source / '_Sidebar.md').read_text(encoding='utf-8'), '_Sidebar.md'
    )

    changed = []
    homepage = wiki / 'Home.md'
    original_home = wiki / ORIGINAL_HOME
    if homepage.exists():
        old_home = homepage.read_text(encoding='utf-8')
        if START in old_home:
            new_home = replace_navigation(old_home, home_template, 'Home.md')
        else:
            # The archival copy must remain a faithful copy of the original,
            # not the generated 2.0 landing page. Never overwrite it later.
            if not original_home.exists():
                archive = (
                    '# Color-Screen 1.0 — original wiki Home page\n\n'
                    'Historical content preserved when the 2.0 user manual '
                    'became the wiki landing page.\n\n---\n\n'
                    + old_home
                )
                if write_changed(original_home, archive):
                    changed.append(ORIGINAL_HOME)
            new_home = home_template
    else:
        if not original_home.exists():
            if write_changed(
                original_home,
                '# Color-Screen 1.0 — original wiki Home page\n\n'
                'No earlier Home page was present when 2.0 navigation was added.\n',
            ):
                changed.append(ORIGINAL_HOME)
        new_home = home_template
    if write_changed(homepage, new_home):
        changed.append('Home.md')

    sidebar = wiki / '_Sidebar.md'
    if sidebar.exists():
        previous_sidebar = sidebar.read_text(encoding='utf-8')
        if START in previous_sidebar:
            new_sidebar = replace_navigation(
                previous_sidebar, sidebar_template, '_Sidebar.md'
            )
        elif previous_sidebar.strip():
            new_sidebar = (
                sidebar_template + '\n---\n\n'
                '**Historical wiki navigation**\n\n' + previous_sidebar
            )
        else:
            new_sidebar = sidebar_template
    else:
        new_sidebar = sidebar_template
    if write_changed(sidebar, new_sidebar):
        changed.append('_Sidebar.md')

    index = wiki / HISTORICAL_INDEX
    previous_index = index.read_text(encoding='utf-8')
    generated = (
        LEGACY_START + '\n\n'
        '## Preserved historical wiki pages\n\n'
        + legacy_page_links(wiki) + '\n' + LEGACY_END
    )
    if LEGACY_START in previous_index or LEGACY_END in previous_index:
        if previous_index.count(LEGACY_START) != 1 or previous_index.count(LEGACY_END) != 1:
            raise ValueError('historical index has mismatched managed markers')
        head, remainder = previous_index.split(LEGACY_START, 1)
        _, tail = remainder.split(LEGACY_END, 1)
        new_index = head + generated + tail
    else:
        new_index = previous_index.rstrip() + '\n\n' + generated + '\n'
    if write_changed(index, new_index):
        changed.append(HISTORICAL_INDEX)

    for link in PUBLISHED + (ORIGINAL_HOME,):
        if not (wiki / link).is_file():
            raise ValueError('navigation links to missing wiki page: ' + link)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--wiki', type=Path, required=True)
    args = parser.parse_args()
    changed = publish(args.source, args.wiki)
    print('Wiki navigation updated: ' + (', '.join(changed) if changed else 'no changes'))


if __name__ == '__main__':
    main()
