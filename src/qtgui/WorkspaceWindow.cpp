#include "WorkspaceWindow.h"

#include "ColorScreenApplication.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

/** Return the Color-Screen application-level document manager. */
ColorScreenApplication *documentApplication() {
  return dynamic_cast<ColorScreenApplication *>(QApplication::instance());
}

} // namespace

/** Construct the primary Photoshop/Krita-style tabbed workspace. */
WorkspaceWindow::WorkspaceWindow(QWidget *parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("workspaceWindow"));
  setWindowTitle(tr("Color-Screen"));

  m_tabs = new QTabWidget(this);
  m_tabs->setDocumentMode(true);
  m_tabs->setMovable(true);
  m_tabs->setTabsClosable(true);
  m_tabs->setElideMode(Qt::ElideMiddle);
  setCentralWidget(m_tabs);

  connect(m_tabs, &QTabWidget::tabCloseRequested, this,
          [this](int index) { closeTab(index); });
  connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
    if (MainWindow *document = currentDocument()) {
      document->refreshWindowMenu();
      setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
    } else {
      setWindowTitle(tr("Color-Screen"));
    }
  });

  m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested, this,
          [this](const QPoint &position) {
            const int index = m_tabs->tabBar()->tabAt(position);
            if (index < 0)
              return;
            QMenu menu(this);
            QAction *detach = menu.addAction(tr("Detach Image"));
            if (menu.exec(m_tabs->tabBar()->mapToGlobal(position)) == detach)
              detachTab(index);
          });
  connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this,
          [this](int index) {
            if (index >= 0)
              detachTab(index);
          });

  restoreWorkspaceGeometry();
}

/** Embed DOCUMENT as a live tab without recreating any document state. */
void WorkspaceWindow::addDocument(MainWindow *document) {
  if (!document)
    return;
  if (containsDocument(document)) {
    activateDocument(document);
    return;
  }

  document->hide();
  document->setWindowFlags(Qt::Widget);
  document->setParent(m_tabs);
  const int index = m_tabs->addTab(document, document->documentDisplayName());
  m_tabs->setTabToolTip(index, document->currentImageFile());
  m_tabs->setCurrentIndex(index);
  document->show();
  show();
  raise();
  activateWindow();
}

/** Remove DOCUMENT from the tab widget without closing or deleting it. */
void WorkspaceWindow::removeDocument(MainWindow *document) {
  if (!document)
    return;
  const int index = m_tabs->indexOf(document);
  if (index >= 0)
    m_tabs->removeTab(index);
}

/** Return the MainWindow embedded in the active tab. */
MainWindow *WorkspaceWindow::currentDocument() const {
  return qobject_cast<MainWindow *>(m_tabs->currentWidget());
}

/** Return the number of attached documents. */
int WorkspaceWindow::tabCount() const { return m_tabs->count(); }

/** Return whether DOCUMENT is one of this workspace's tabs. */
bool WorkspaceWindow::containsDocument(MainWindow *document) const {
  return document && m_tabs->indexOf(document) >= 0;
}

/** Select DOCUMENT when it is tabbed. */
void WorkspaceWindow::activateDocument(MainWindow *document) {
  const int index = document ? m_tabs->indexOf(document) : -1;
  if (index < 0)
    return;
  m_tabs->setCurrentIndex(index);
  show();
  if (isMinimized())
    showNormal();
  raise();
  activateWindow();
  document->setFocus();
}

/** Update the visible title for DOCUMENT after filename/dirty-state changes. */
void WorkspaceWindow::refreshDocument(MainWindow *document) {
  const int index = document ? m_tabs->indexOf(document) : -1;
  if (index < 0)
    return;
  m_tabs->setTabText(index, document->documentDisplayName());
  m_tabs->setTabToolTip(index, document->currentImageFile());
  if (index == m_tabs->currentIndex())
    setWindowTitle(document->documentDisplayName() + tr(" — Color-Screen"));
}

/** Restore the workspace geometry only when it still fits the current desktop. */
void WorkspaceWindow::restoreWorkspaceGeometry() {
  QSettings settings;
  bool compatibleDesktop = true;
  if (QScreen *screen = QApplication::primaryScreen()) {
    const QSize saved = settings.value("workspaceDesktopSize").toSize();
    const QSize current = screen->availableGeometry().size();
    if (saved.isValid() &&
        (qAbs(saved.width() - current.width()) > 100 ||
         qAbs(saved.height() - current.height()) > 100))
      compatibleDesktop = false;
  }

  if (compatibleDesktop && settings.contains("workspaceGeometry")) {
    restoreGeometry(settings.value("workspaceGeometry").toByteArray());
  } else if (QScreen *screen = QApplication::primaryScreen()) {
    const QRect area = screen->availableGeometry();
    resize(qMin(1400, qMax(900, area.width() * 4 / 5)),
           qMin(950, qMax(650, area.height() * 4 / 5)));
    move(area.center() - rect().center());
  } else {
    resize(1200, 800);
  }
}

/** Persist the outer workspace geometry independently of document dock state. */
void WorkspaceWindow::saveWorkspaceGeometry() const {
  QSettings settings;
  settings.setValue("workspaceGeometry", saveGeometry());
  if (QScreen *screen = QApplication::primaryScreen())
    settings.setValue("workspaceDesktopSize", screen->availableGeometry().size());
}

/** Close one tab through MainWindow::closeEvent and its save policy. */
void WorkspaceWindow::closeTab(int index) {
  if (auto *document = qobject_cast<MainWindow *>(m_tabs->widget(index)))
    document->close();
}

/** Move a tab into a normal top-level MainWindow without copying its state. */
void WorkspaceWindow::detachTab(int index) {
  auto *document = qobject_cast<MainWindow *>(m_tabs->widget(index));
  if (!document)
    return;
  if (ColorScreenApplication *application = documentApplication())
    application->detachDocument(document);
}

/** Close the whole session, respecting every document's close veto. */
void WorkspaceWindow::closeEvent(QCloseEvent *event) {
  if (m_closing) {
    event->accept();
    return;
  }

  m_closing = true;
  bool accepted = true;
  if (ColorScreenApplication *application = documentApplication())
    accepted = application->closeAllDocumentsForWorkspace();
  if (!accepted) {
    m_closing = false;
    event->ignore();
    return;
  }

  saveWorkspaceGeometry();
  event->accept();
}
