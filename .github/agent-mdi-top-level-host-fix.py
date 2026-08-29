from pathlib import Path


def replace_once(path, old, new, label):
    text = path.read_text()
    if old not in text:
        raise SystemExit(f'{label} not found in {path}')
    path.write_text(text.replace(old, new, 1))


# The final detachable-panel model has one rule: use the actual top-level
# QMainWindow presenting the section. Remove the intermediate host-pinning API.
header = Path('src/qtgui/ParameterPanel.h')
text = header.read_text()
for old, label in [
    ('#include <QPointer>\n', 'ParameterPanel QPointer include'),
    ('class QMainWindow;\n', 'ParameterPanel QMainWindow declaration'),
    ('''  /** Pin detachable sections to HOST instead of following this panel's current\n      top-level window. Passing nullptr restores dynamic host selection. */\n  void setDetachableHost(QMainWindow *host);\n\n''', 'setDetachableHost declaration'),
    ('  QPointer<QMainWindow> m_detachableHost;\n', 'm_detachableHost member'),
    ('''  // Host propagation must not depend on QObject parenting: layouts can reparent\n  // detachable sections as inspectors move between presentations.\n  std::vector<std::function<void(QMainWindow *)>> m_detachableHostUpdaters;\n\n''', 'detachable host updater member'),
]:
    if old not in text:
        raise SystemExit(f'{label} not found in {header}')
    text = text.replace(old, '', 1)
header.write_text(text)

cpp = Path('src/qtgui/ParameterPanel.cpp')
text = cpp.read_text()
replacements = [
    ('''  DetachableSection(const QString &title, QWidget *content,\n                    std::function<void()> beforeDetach, QMainWindow *host,\n                    QWidget *parent = nullptr)\n      : QWidget(parent), m_title(title), m_content(content),\n        m_beforeDetach(std::move(beforeDetach)), m_pinnedHost(host) {''',
     '''  DetachableSection(const QString &title, QWidget *content,\n                    std::function<void()> beforeDetach,\n                    QWidget *parent = nullptr)\n      : QWidget(parent), m_title(title), m_content(content),\n        m_beforeDetach(std::move(beforeDetach)) {''',
     'DetachableSection constructor'),
    ('''  /** Pin this section to HOST, or resume following its containing window. */\n  void setHost(QMainWindow *host) {\n    m_pinnedHost = host;\n    migrateDockToCurrentHost();\n  }\n\n''', '', 'DetachableSection setHost'),
    ('''  QMainWindow *currentHost() const {\n    if (m_pinnedHost)\n      return m_pinnedHost.data();\n    return qobject_cast<QMainWindow *>(window());\n  }''',
     '''  QMainWindow *currentHost() const {\n    return qobject_cast<QMainWindow *>(window());\n  }''',
     'DetachableSection currentHost'),
    ('  QPointer<QMainWindow> m_pinnedHost;\n', '', 'DetachableSection pinned host member'),
    ('''/** Override the generic dynamic dock host for specialized panel owners. */\nvoid ParameterPanel::setDetachableHost(QMainWindow *host) {\n  m_detachableHost = host;\n  for (const auto &updateHost : m_detachableHostUpdaters)\n    updateHost(host);\n}\n\n''', '', 'ParameterPanel setDetachableHost definition'),
    ('''  auto *section = new DetachableSection(title, content, std::move(beforeDetach),\n                                        m_detachableHost.data(), this);\n  QPointer<DetachableSection> guardedSection(section);\n  m_detachableHostUpdaters.push_back(\n      [guardedSection](QMainWindow *host) {\n        if (guardedSection)\n          guardedSection->setHost(host);\n      });\n  return section;''',
     '''  return new DetachableSection(title, content, std::move(beforeDetach), this);''',
     'createDetachableSection host registry'),
]
for old, new, label in replacements:
    if old not in text:
        raise SystemExit(f'{label} not found in {cpp}')
    text = text.replace(old, new, 1)
cpp.write_text(text)

# Keep progress signal wiring for the lifetime of a logical document, separately
# from whether that document currently has a presentation in this workspace.
workspace_header = Path('src/qtgui/WorkspaceWindow.h')
text = workspace_header.read_text()
old = '''  QList<QPointer<MainWindow>> m_progressDocuments;\n  QPointer<MainWindow> m_displayedProgressDocument;\n'''
new = '''  QList<QPointer<MainWindow>> m_progressDocuments;\n  QList<QPointer<MainWindow>> m_progressSignalDocuments;\n  QPointer<MainWindow> m_displayedProgressDocument;\n'''
if old not in text:
    raise SystemExit('workspace progress member block not found')
workspace_header.write_text(text.replace(old, new, 1))

workspace = Path('src/qtgui/WorkspaceWindow.cpp')
text = workspace.read_text()
old = '''/** Permanently attach one logical document's progress to the workspace shell. */\nvoid WorkspaceWindow::attachDocumentProgress(MainWindow *document) {\n  if (!document || !m_workspaceProgressStack)\n    return;\n\n  bool alreadyAttached = false;\n  for (const QPointer<MainWindow> &candidate : std::as_const(m_progressDocuments)) {\n    if (candidate == document) {\n      alreadyAttached = true;\n      break;\n    }\n  }\n\n  if (!alreadyAttached) {\n    QWidget *progress = document->takeWorkspaceStatusWidget();\n    if (progress) {\n      progress->setParent(m_workspaceProgressStack);\n      m_workspaceProgressStack->addWidget(progress);\n      m_progressDocuments.append(document);\n      const int stableHeight = qMax(\n          statusBar()->minimumHeight(),\n          qMax(progress->minimumHeight(), progress->sizeHint().height()));\n      statusBar()->setMinimumHeight(stableHeight);\n    }\n\n    QPointer<MainWindow> guardedDocument(document);\n    connect(document, &MainWindow::transientProgressVisibilityChanged, this,\n            [this, guardedDocument](bool visible) {\n              if (visible)\n                m_displayedProgressDocument = guardedDocument;\n              updateWorkspaceProgressPresentation();\n            });\n    connect(document, &MainWindow::userVisibleProgressVisibilityChanged, this,\n            [this](bool) { updateUserVisibleProgressDockVisibility(); });\n    connect(document, &QWidget::windowTitleChanged, this,\n            [this, guardedDocument](const QString &) {\n              if (guardedDocument == m_displayedProgressDocument)\n                updateWorkspaceProgressPresentation();\n            });\n    connect(document, &QObject::destroyed, this, [this]() {\n      for (auto it = m_progressDocuments.begin();\n           it != m_progressDocuments.end();) {\n        if (it->isNull())\n          it = m_progressDocuments.erase(it);\n        else\n          ++it;\n      }\n      if (!m_displayedProgressDocument)\n        m_displayedProgressDocument.clear();\n      updateWorkspaceProgressPresentation();\n      updateUserVisibleProgressDockVisibility();\n    });\n  }\n\n  attachUserVisibleProgress(document);\n  if (document->hasVisibleTransientProgress())\n    m_displayedProgressDocument = document;\n  updateWorkspaceProgressPresentation();\n}\n'''
new = '''/** Permanently attach one logical document's progress to the workspace shell. */\nvoid WorkspaceWindow::attachDocumentProgress(MainWindow *document) {\n  if (!document || !m_workspaceProgressStack)\n    return;\n\n  bool alreadyAttached = false;\n  for (const QPointer<MainWindow> &candidate : std::as_const(m_progressDocuments)) {\n    if (candidate == document) {\n      alreadyAttached = true;\n      break;\n    }\n  }\n\n  if (!alreadyAttached) {\n    QWidget *progress = document->takeWorkspaceStatusWidget();\n    if (progress) {\n      progress->setParent(m_workspaceProgressStack);\n      m_workspaceProgressStack->addWidget(progress);\n      m_progressDocuments.append(document);\n      const int stableHeight = qMax(\n          statusBar()->minimumHeight(),\n          qMax(progress->minimumHeight(), progress->sizeHint().height()));\n      statusBar()->setMinimumHeight(stableHeight);\n    }\n  }\n\n  bool signalsConnected = false;\n  for (const QPointer<MainWindow> &candidate :\n       std::as_const(m_progressSignalDocuments)) {\n    if (candidate == document) {\n      signalsConnected = true;\n      break;\n    }\n  }\n\n  if (!signalsConnected) {\n    m_progressSignalDocuments.append(document);\n    QPointer<MainWindow> guardedDocument(document);\n    connect(document, &MainWindow::transientProgressVisibilityChanged, this,\n            [this, guardedDocument](bool visible) {\n              if (visible)\n                m_displayedProgressDocument = guardedDocument;\n              updateWorkspaceProgressPresentation();\n            });\n    connect(document, &MainWindow::userVisibleProgressVisibilityChanged, this,\n            [this](bool) { updateUserVisibleProgressDockVisibility(); });\n    connect(document, &QWidget::windowTitleChanged, this,\n            [this, guardedDocument](const QString &) {\n              if (guardedDocument == m_displayedProgressDocument)\n                updateWorkspaceProgressPresentation();\n            });\n    connect(document, &QObject::destroyed, this, [this]() {\n      for (auto it = m_progressDocuments.begin();\n           it != m_progressDocuments.end();) {\n        if (it->isNull())\n          it = m_progressDocuments.erase(it);\n        else\n          ++it;\n      }\n      for (auto it = m_progressSignalDocuments.begin();\n           it != m_progressSignalDocuments.end();) {\n        if (it->isNull())\n          it = m_progressSignalDocuments.erase(it);\n        else\n          ++it;\n      }\n      if (!m_displayedProgressDocument)\n        m_displayedProgressDocument.clear();\n      updateWorkspaceProgressPresentation();\n      updateUserVisibleProgressDockVisibility();\n    });\n  }\n\n  attachUserVisibleProgress(document);\n  if (document->hasVisibleTransientProgress())\n    m_displayedProgressDocument = document;\n  updateWorkspaceProgressPresentation();\n}\n'''
if old not in text:
    raise SystemExit('attachDocumentProgress block not found')
workspace.write_text(text.replace(old, new, 1))

# Make structured smoke tests completion-driven. The --smoke-test duration is a
# watchdog; it must never destroy widgets while assertions are inside processEvents().
main = Path('src/qtgui/main.cpp')
text = main.read_text()
old = '''  const auto newViewSmokeDone =\n      std::make_shared<bool>(!parser.isSet(newViewOption));\n\n  if (parser.isSet(newViewOption)) {\n    QTimer::singleShot(300, &app, [&app, newViewSmokeDone]() {\n'''
new = '''  const auto newViewSmokeDone =\n      std::make_shared<bool>(!parser.isSet(newViewOption));\n  const auto slantedReferenceSmokeDone =\n      std::make_shared<bool>(!parser.isSet(slantedReferenceOption));\n  const bool completionManagedSmoke =\n      parser.isSet(smokeTestOption) &&\n      (parser.isSet(newViewOption) || parser.isSet(slantedReferenceOption)) &&\n      !parser.isSet(userVisibleProgressOption) &&\n      !parser.isSet(windowLifetimeOption) &&\n      !parser.isSet(closeToEmptyTabOption);\n  const auto maybeFinishStructuredSmoke =\n      std::make_shared<std::function<void()>>();\n  *maybeFinishStructuredSmoke =\n      [&app, newViewSmokeDone, slantedReferenceSmokeDone,\n       completionManagedSmoke]() {\n        if (completionManagedSmoke && *newViewSmokeDone &&\n            *slantedReferenceSmokeDone)\n          QTimer::singleShot(0, &app, [&app]() { app.quit(); });\n      };\n\n  if (parser.isSet(newViewOption)) {\n    QTimer::singleShot(300, &app,\n                       [&app, newViewSmokeDone, maybeFinishStructuredSmoke]() {\n'''
if old not in text:
    raise SystemExit('structured smoke setup block not found')
text = text.replace(old, new, 1)

old = '''          [&app, guardedSource, guardedView, documentCount, newViewSmokeDone,\n           weakCheckFinalPeerClose](int attemptsLeft) {'''
new = '''          [&app, guardedSource, guardedView, documentCount, newViewSmokeDone,\n           maybeFinishStructuredSmoke, weakCheckFinalPeerClose](int attemptsLeft) {'''
if old not in text:
    raise SystemExit('new-view final close capture not found')
text = text.replace(old, new, 1)
old = '''            *newViewSmokeDone = true;\n          };'''
new = '''            *newViewSmokeDone = true;\n            (*maybeFinishStructuredSmoke)();\n          };'''
if old not in text:
    raise SystemExit('new-view completion marker not found')
text = text.replace(old, new, 1)

old = '''    *startReferenceSmoke =\n        [&app, newViewSmokeDone, weakStartReferenceSmoke](int attemptsLeft) {'''
new = '''    *startReferenceSmoke =\n        [&app, newViewSmokeDone, slantedReferenceSmokeDone,\n         maybeFinishStructuredSmoke, weakStartReferenceSmoke](int attemptsLeft) {'''
if old not in text:
    raise SystemExit('reference smoke start capture not found')
text = text.replace(old, new, 1)
old = '''      *checkReference = [&app, guardedSource, guardedReference, documentCount,\n                         tabCount, weakCheckReference](int attemptsLeft) {'''
new = '''      *checkReference = [&app, guardedSource, guardedReference, documentCount,\n                         tabCount, slantedReferenceSmokeDone,\n                         maybeFinishStructuredSmoke,\n                         weakCheckReference](int attemptsLeft) {'''
if old not in text:
    raise SystemExit('reference check capture not found')
text = text.replace(old, new, 1)
old = '''        *checkReload = [&app, guardedSource, guardedReference, beforeReload,\n                        weakCheckReload](int attemptsLeft) {'''
new = '''        *checkReload = [&app, guardedSource, guardedReference, beforeReload,\n                        slantedReferenceSmokeDone, maybeFinishStructuredSmoke,\n                        weakCheckReload](int attemptsLeft) {'''
if old not in text:
    raise SystemExit('reference reload capture not found')
text = text.replace(old, new, 1)
old = '''          for (ImageViewWindow *view : references)\n            app.closeView(view);\n        };'''
new = '''          for (ImageViewWindow *view : references)\n            app.closeView(view);\n          *slantedReferenceSmokeDone = true;\n          (*maybeFinishStructuredSmoke)();\n        };'''
if old not in text:
    raise SystemExit('reference completion block not found')
text = text.replace(old, new, 1)

old = '''        // processEvents() may run the overall smoke shutdown timer on very slow\n        // sanitizer builds. Never retain raw child pointers across that turn.\n'''
new = '''        // Teardown can be queued by another presentation operation while this\n        // check yields to Qt. Never retain raw child pointers across that turn.\n'''
if old not in text:
    raise SystemExit('reference QPointer comment not found')
text = text.replace(old, new, 1)

old = '''  if (parser.isSet(smokeTestOption)) {\n    bool converted = false;\n    int duration = parser.value(smokeTestOption).toInt(&converted);\n    if (!converted || duration <= 0)\n      duration = 5000;\n    // New View and slanted-reference checks deliberately run serially because\n    // both manipulate shared document presentation. Sanitizer builds,\n    // especially ARM64 ASan, need more than the ordinary 30-second smoke\n    // window; do not let the cleanup timer destroy their widgets mid-check.\n    if (parser.isSet(newViewOption) && parser.isSet(slantedReferenceOption))\n      duration = qMax(duration, 60000);\n    qDebug() << "Smoke Test Mode: Will exit in" << duration << "ms...";\n    QTimer::singleShot(duration, &app, [&app]() {\n      app.closeAllDocumentWindows();\n      app.quit();\n    });\n  }\n'''
new = '''  if (parser.isSet(smokeTestOption)) {\n    bool converted = false;\n    int duration = parser.value(smokeTestOption).toInt(&converted);\n    if (!converted || duration <= 0)\n      duration = 5000;\n    // New View and slanted-reference checks manipulate shared presentation\n    // serially. Give their watchdog enough room under instrumentation, but\n    // successful structured checks quit immediately when they are complete.\n    if (completionManagedSmoke && parser.isSet(newViewOption) &&\n        parser.isSet(slantedReferenceOption))\n      duration = qMax(duration, 60000);\n    qDebug() << "Smoke Test Mode: watchdog is" << duration << "ms";\n    QTimer::singleShot(\n        duration, &app,\n        [&app, completionManagedSmoke, newViewSmokeDone,\n         slantedReferenceSmokeDone]() {\n          if (completionManagedSmoke &&\n              (!*newViewSmokeDone || !*slantedReferenceSmokeDone)) {\n            qCritical() << "Structured GUI smoke test timed out before completion";\n            app.exit(17);\n            return;\n          }\n          app.quit();\n        });\n  }\n'''
if old not in text:
    raise SystemExit('smoke watchdog block not found')
text = text.replace(old, new, 1)
main.write_text(text)
