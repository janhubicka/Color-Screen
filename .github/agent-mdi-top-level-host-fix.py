from pathlib import Path

view = Path('src/qtgui/ImageViewWindow.cpp')
text = view.read_text()
old = '  m_sharpnessPanel->setDetachableHost(this);\n'
if old not in text:
    raise SystemExit('reference detachable host pin not found')
view.write_text(text.replace(old, '', 1))

main = Path('src/qtgui/main.cpp')
text = main.read_text()
old = '''        QDockWidget *mtfDock = nullptr;
        for (QDockWidget *candidate : reference->findChildren<QDockWidget *>()) {
          if (candidate && candidate->property("detachablePanel").toBool() &&
              candidate->property("detachableTitle").toString() ==
                  QStringLiteral("MTF Chart") &&
              candidate->isAncestorOf(mtfChart)) {
            mtfDock = candidate;
            break;
          }
        }
'''
new = '''        QMainWindow *mtfHost =
            qobject_cast<QMainWindow *>(mtfSection->window());
        QDockWidget *mtfDock = nullptr;
        if (mtfHost) {
          for (QDockWidget *candidate : mtfHost->findChildren<QDockWidget *>()) {
            if (candidate && candidate->property("detachablePanel").toBool() &&
                candidate->property("detachableTitle").toString() ==
                    QStringLiteral("MTF Chart") &&
                candidate->isAncestorOf(mtfChart)) {
              mtfDock = candidate;
              break;
            }
          }
        }
'''
if old not in text:
    raise SystemExit('reference MTF dock lookup not found')
main.write_text(text.replace(old, new, 1))

doc = Path('.agents/qtgui.md')
text = doc.read_text()
old = '''By default a detached
section follows the top-level window currently presenting its inspector and always
returns its content when closed. Specialized inspectors may pin that same generic
dock lifecycle to their logical presentation; slanted-edge Sharpness sections use
the reference `ImageViewWindow` as that host. This keeps reference diagnostics
presentation-owned without reintroducing panel-specific dock wiring.'''
new = '''A detached section follows the actual top-level window currently presenting its
inspector and always returns its content when closed. This rule applies uniformly
to document, ordinary-view, and slanted-edge reference inspectors: while embedded,
the workspace owns their floating docks; after detaching a presentation, that
presentation window becomes the dock host. This avoids nested `QMainWindow` dock
ownership without reintroducing panel-specific wiring.'''
if old not in text:
    raise SystemExit('detachable host documentation not found')
doc.write_text(text.replace(old, new, 1))
