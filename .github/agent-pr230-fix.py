from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if text.count(old) < count:
        raise SystemExit(f"{path}: expected replacement not found: {old[:100]!r}")
    p.write_text(text.replace(old, new, count))


# Keep one DetachableSection implementation, but let specialized inspectors pin
# the logical QMainWindow that owns their detached diagnostics.
replace(
    "src/qtgui/ParameterPanel.h",
    "#include <QComboBox>\n#include <QString>\n",
    "#include <QComboBox>\n#include <QPointer>\n#include <QString>\n",
)
replace(
    "src/qtgui/ParameterPanel.h",
    "class QVBoxLayout;\nclass QFormLayout;\n",
    "class QMainWindow;\nclass QVBoxLayout;\nclass QFormLayout;\n",
)
replace(
    "src/qtgui/ParameterPanel.h",
    "  // Called when the external state changes (Undo/Redo, Code Load)\n"
    "  virtual void updateUI();\n\n"
    "protected:\n",
    "  // Called when the external state changes (Undo/Redo, Code Load)\n"
    "  virtual void updateUI();\n\n"
    "  /** Pin detachable sections to HOST instead of following this panel's current\n"
    "      top-level window. Passing nullptr restores dynamic host selection. */\n"
    "  void setDetachableHost(QMainWindow *host);\n\n"
    "protected:\n",
)
replace(
    "src/qtgui/ParameterPanel.h",
    "  ImageGetter m_imageGetter;\n"
    "  QFormLayout *m_currentGroupForm = nullptr;\n",
    "  ImageGetter m_imageGetter;\n"
    "  QPointer<QMainWindow> m_detachableHost;\n"
    "  QFormLayout *m_currentGroupForm = nullptr;\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "  DetachableSection(const QString &title, QWidget *content,\n"
    "                    std::function<void()> beforeDetach,\n"
    "                    QWidget *parent = nullptr)\n"
    "      : QWidget(parent), m_title(title), m_content(content),\n"
    "        m_beforeDetach(std::move(beforeDetach)) {\n",
    "  DetachableSection(const QString &title, QWidget *content,\n"
    "                    std::function<void()> beforeDetach, QMainWindow *host,\n"
    "                    QWidget *parent = nullptr)\n"
    "      : QWidget(parent), m_title(title), m_content(content),\n"
    "        m_beforeDetach(std::move(beforeDetach)), m_pinnedHost(host) {\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "  ~DetachableSection() override { reattach(false); }\n\n"
    "protected:\n",
    "  ~DetachableSection() override { reattach(false); }\n\n"
    "  /** Pin this section to HOST, or resume following its containing window. */\n"
    "  void setHost(QMainWindow *host) {\n"
    "    m_pinnedHost = host;\n"
    "    migrateDockToCurrentHost();\n"
    "  }\n\n"
    "protected:\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "  QMainWindow *currentHost() const {\n"
    "    return qobject_cast<QMainWindow *>(window());\n"
    "  }\n",
    "  QMainWindow *currentHost() const {\n"
    "    if (m_pinnedHost)\n"
    "      return m_pinnedHost.data();\n"
    "    return qobject_cast<QMainWindow *>(window());\n"
    "  }\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "  std::function<void()> m_beforeDetach;\n"
    "  QVBoxLayout *m_layout = nullptr;\n",
    "  std::function<void()> m_beforeDetach;\n"
    "  QPointer<QMainWindow> m_pinnedHost;\n"
    "  QVBoxLayout *m_layout = nullptr;\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "ParameterPanel::~ParameterPanel() = default;\n\n"
    "void ParameterPanel::updateUI() {\n",
    "ParameterPanel::~ParameterPanel() = default;\n\n"
    "/** Override the generic dynamic dock host for specialized panel owners. */\n"
    "void ParameterPanel::setDetachableHost(QMainWindow *host) {\n"
    "  m_detachableHost = host;\n"
    "  for (DetachableSection *section : findChildren<DetachableSection *>())\n"
    "    section->setHost(host);\n"
    "}\n\n"
    "void ParameterPanel::updateUI() {\n",
)
replace(
    "src/qtgui/ParameterPanel.cpp",
    "  return new DetachableSection(title, content, std::move(beforeDetach));\n",
    "  return new DetachableSection(title, content, std::move(beforeDetach),\n"
    "                               m_detachableHost.data());\n",
)

# Reference Sharpness diagnostics use the same generic section lifecycle, but
# their logical floating-dock owner is the specialized reference presentation.
replace(
    "src/qtgui/ImageViewWindow.cpp",
    "  m_referenceTabs->addTab(m_sharpnessPanel, tr(\"Sharpness\"));\n\n"
    "  connect(m_sharpnessPanel,\n",
    "  m_sharpnessPanel->setDetachableHost(this);\n"
    "  m_referenceTabs->addTab(m_sharpnessPanel, tr(\"Sharpness\"));\n\n"
    "  connect(m_sharpnessPanel,\n",
)

# Mirror the source document's File commands in every secondary presentation,
# substituting only Close View for the primary presentation's Close Window.
replace(
    "src/qtgui/ImageViewWindow.cpp",
    "  QMenu *fileMenu = menuBar()->addMenu(tr(\"&File\"));\n"
    "  QAction *closeView = fileMenu->addAction(tr(\"&Close View\"));\n"
    "  closeView->setShortcut(QKeySequence::Close);\n",
    "  QMenu *fileMenu = menuBar()->addMenu(tr(\"&File\"));\n"
    "  QAction *closeView = new QAction(tr(\"&Close View\"), this);\n"
    "  closeView->setShortcut(QKeySequence::Close);\n",
)
replace(
    "src/qtgui/ImageViewWindow.cpp",
    "  connect(closeView, &QAction::triggered, this, [this]() {\n"
    "    if (auto *application =\n"
    "            dynamic_cast<ColorScreenApplication *>(QApplication::instance()))\n"
    "      application->closeView(this);\n"
    "    else\n"
    "      close();\n"
    "  });\n\n"
    "  if (!m_slantedEdgeReference && m_document)\n",
    "  connect(closeView, &QAction::triggered, this, [this]() {\n"
    "    if (auto *application =\n"
    "            dynamic_cast<ColorScreenApplication *>(QApplication::instance()))\n"
    "      application->closeView(this);\n"
    "    else\n"
    "      close();\n"
    "  });\n\n"
    "  QMenu *documentFileMenu = nullptr;\n"
    "  if (m_document) {\n"
    "    for (QAction *menuAction : m_document->menuBar()->actions()) {\n"
    "      if (menuAction && QString(menuAction->text()).remove('&') ==\n"
    "                            QStringLiteral(\"File\")) {\n"
    "        documentFileMenu = menuAction->menu();\n"
    "        break;\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  bool addedCloseView = false;\n"
    "  if (documentFileMenu) {\n"
    "    for (QAction *action : documentFileMenu->actions()) {\n"
    "      if (!action)\n"
    "        continue;\n"
    "      if (QString(action->text()).remove('&') ==\n"
    "          QStringLiteral(\"Close Window\")) {\n"
    "        fileMenu->addAction(closeView);\n"
    "        addedCloseView = true;\n"
    "      } else {\n"
    "        fileMenu->addAction(action);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  if (!addedCloseView)\n"
    "    fileMenu->addAction(closeView);\n\n"
    "  if (!m_slantedEdgeReference && m_document)\n",
)

# Extend New View smoke coverage to the complete File menu.
replace(
    "src/qtgui/main.cpp",
    "#include <QMenuBar>\n",
    "#include <QMenu>\n#include <QMenuBar>\n",
)
menu_marker = '''      if (viewMenus != expectedViewMenus) {
        qCritical() << "New View menu chrome differs from an ordinary document"
                    << viewMenus;
        app.exit(14);
        return;
      }
'''
replace(
    "src/qtgui/main.cpp",
    menu_marker,
    menu_marker
    + '''
      QMenu *viewFileMenu = nullptr;
      for (QAction *action : view->menuBar()->actions()) {
        if (QString(action->text()).remove('&') == QStringLiteral("File")) {
          viewFileMenu = action->menu();
          break;
        }
      }
      QStringList viewFileActions;
      if (viewFileMenu) {
        for (QAction *action : viewFileMenu->actions())
          if (action && !action->isSeparator())
            viewFileActions << QString(action->text()).remove('&');
      }
      if (!viewFileMenu ||
          !viewFileActions.contains(QStringLiteral("Save Parameters")) ||
          !viewFileActions.contains(QStringLiteral("Exit")) ||
          !viewFileActions.contains(QStringLiteral("Close View")) ||
          viewFileActions.contains(QStringLiteral("Close Window"))) {
        qCritical() << "New View File menu does not mirror document commands"
                    << viewFileActions;
        app.exit(14);
        return;
      }
''',
)

# The specialized dock must be owned by the reference view, not the workspace.
replace(
    "src/qtgui/main.cpp",
    "        for (QDockWidget *candidate : workspace->findChildren<QDockWidget *>()) {\n"
    "          if (candidate && candidate->property(\"detachablePanel\").toBool() &&\n"
    "              candidate->property(\"detachableTitle\").toString() ==\n"
    "                  QStringLiteral(\"MTF Chart\") &&\n",
    "        for (QDockWidget *candidate : reference->findChildren<QDockWidget *>()) {\n"
    "          if (candidate && candidate->property(\"detachablePanel\").toBool() &&\n"
    "              candidate->property(\"detachableTitle\").toString() ==\n"
    "                  QStringLiteral(\"MTF Chart\") &&\n",
    1,
)

# Keep architecture documentation aligned with the implementation.
replace(
    ".agents/qtgui.md",
    "A detached\n"
    "section follows the top-level window currently presenting its inspector and always\n"
    "returns its content when closed. Sharpness diagnostics in a slanted-edge reference\n"
    "therefore remain presentation-owned by that reference inspector without any\n"
    "special-case dock wiring.",
    "By default a detached\n"
    "section follows the top-level window currently presenting its inspector and always\n"
    "returns its content when closed. Specialized inspectors may pin that same generic\n"
    "dock lifecycle to their logical presentation; slanted-edge Sharpness sections use\n"
    "the reference `ImageViewWindow` as that host. This keeps reference diagnostics\n"
    "presentation-owned without reintroducing panel-specific dock wiring.",
)
replace(
    ".agents/qtgui.md",
    "Ordinary views also expose the document's Edit\n"
    "and Registration menus and the same canvas-tool toolbar actions as the primary\n",
    "Ordinary views also expose the document's complete File commands, Edit\n"
    "and Registration menus and the same canvas-tool toolbar actions as the primary\n",
)
