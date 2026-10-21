"""Unit tests for non-destructive GitHub Wiki navigation migration."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publish_navigation import publish, START, END, ORIGINAL_HOME


class WikiNavigationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.src = self.root / 'source'
        self.wiki = self.root / 'wiki'
        self.src.mkdir()
        self.wiki.mkdir()
        self.home = START + '\n# Current 2.0\n[manual](Color-Screen-2.0-User-Manual)\n' + END + '\n'
        (self.src / 'Home.md').write_text(self.home, encoding='utf-8')
        (self.src / '_Sidebar.md').write_text(START + '\n[manual](Color-Screen-2.0-User-Manual)\n' + END + '\n', encoding='utf-8')
        for name in ('Color-Screen-2.0-User-Manual.md',
                     'Color-Screen-2.0-Parameter-Format.md',
                     'Color-Screen-2.0-Project-Format.md',
                     'Color-Screen-1.0-Documentation.md'):
            (self.wiki / name).write_text('Published reference\n', encoding='utf-8')

    def test_archive_preserves_old_home_and_other_articles(self):
        old = '# Color-Screen 1.0\n\n[old rendering](rendering)\n'
        (self.wiki / 'Home.md').write_text(old, encoding='utf-8')
        (self.wiki / 'rendering.md').write_text('Old rendering tutorial\n', encoding='utf-8')
        (self.wiki / '_Sidebar.md').write_text('[old page](rendering)\n', encoding='utf-8')
        changed = publish(self.src, self.wiki)
        self.assertIn('Home.md', changed)
        archive = (self.wiki / ORIGINAL_HOME).read_text(encoding='utf-8')
        self.assertIn(old, archive)
        self.assertEqual((self.wiki / 'rendering.md').read_text(), 'Old rendering tutorial\n')
        self.assertIn('Current 2.0', (self.wiki / 'Home.md').read_text())
        self.assertIn('[old page](rendering)', (self.wiki / '_Sidebar.md').read_text())
        self.assertIn('[rendering](rendering)', (self.wiki / 'Color-Screen-1.0-Documentation.md').read_text())
        # A subsequent publisher run should not alter a single file.
        self.assertEqual(publish(self.src, self.wiki), [])

    def test_manual_edits_outside_managed_sidebar_block_survive(self):
        (self.wiki / 'Home.md').write_text('# Preexisting landing\n')
        publish(self.src, self.wiki)
        sidebar = self.wiki / '_Sidebar.md'
        sidebar.write_text(sidebar.read_text() + '\nHand-edited sidebar note\n')
        publish(self.src, self.wiki)
        self.assertIn('Hand-edited sidebar note', sidebar.read_text())

    def test_broken_markers_raise_without_trashing_old_home(self):
        old = START + '\nUnfinished managed section\n'
        (self.wiki / 'Home.md').write_text(old)
        with self.assertRaises(ValueError):
            publish(self.src, self.wiki)
        self.assertEqual((self.wiki / 'Home.md').read_text(), old)

    def test_no_existing_home_creates_placeholder_archive(self):
        publish(self.src, self.wiki)
        self.assertTrue((self.wiki / ORIGINAL_HOME).exists())
        self.assertEqual(publish(self.src, self.wiki), [])


if __name__ == '__main__':
    unittest.main()
