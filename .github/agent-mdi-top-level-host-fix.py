from pathlib import Path

main = Path('src/qtgui/main.cpp')
text = main.read_text()
if 'QPointer<QWidget> guardedMtfSection(mtfSection);' not in text:
    old = '''        // Reproduce the MTF-detach failure with the source and specialized
        // reference visible as MDI tiles.  The reference owns its Sharpness
        // panel, so the detached chart must be adopted by a dock belonging to
        // that view rather than disappearing from the detachable section.
        workspace->tileDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        SharpnessPanel *sharpness =
            reference->workspaceInspectorWidget()->findChild<SharpnessPanel *>();
        QWidget *mtfChart = sharpness ? sharpness->getMTFChartWidget() : nullptr;
        QWidget *mtfSection = mtfChart ? mtfChart->parentWidget() : nullptr;
        QPushButton *detachMtf = nullptr;
        if (mtfSection) {
          for (QPushButton *button : mtfSection->findChildren<QPushButton *>()) {
            if (button && button->text() == QStringLiteral("Detach")) {
              detachMtf = button;
              break;
            }
          }
        }
        if (!sharpness || !mtfChart || !detachMtf) {
          qCritical() << "Could not locate reference MTF detachable section";
          app.exit(15);
          return;
        }
        detachMtf->click();
        QCoreApplication::processEvents();
        QMainWindow *mtfHost =
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
        if (!mtfDock || !mtfDock->isVisible() || !mtfDock->isFloating() ||
            !mtfDock->widget() || !mtfDock->isAncestorOf(mtfChart)) {
          qCritical() << "Reference MTF chart disappeared instead of detaching";
          app.exit(15);
          return;
        }
        QPointer<QDockWidget> guardedMtfDock(mtfDock);
        mtfDock->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        if (guardedMtfDock && guardedMtfDock->widget()) {
          qCritical() << "Generic MTF dock retained its content after close";
          app.exit(15);
          return;
        }
        if (!reference->workspaceInspectorWidget()->isAncestorOf(mtfChart) ||
            detachMtf->text() != QStringLiteral("Detach")) {
          qCritical() << "Reference MTF chart did not reattach after dock close";
          app.exit(15);
          return;
        }
'''
    new = '''        // Reproduce reference-panel detachment while source and reference are
        // visible as MDI tiles.  The section must use the actual top-level
        // presentation host, exactly like every other ParameterPanel section.
        workspace->tileDocuments();
        workspace->activateView(reference);
        QCoreApplication::processEvents();
        SharpnessPanel *sharpness =
            reference->workspaceInspectorWidget()->findChild<SharpnessPanel *>();
        QWidget *mtfChart = sharpness ? sharpness->getMTFChartWidget() : nullptr;
        QWidget *mtfSection = mtfChart ? mtfChart->parentWidget() : nullptr;
        QPushButton *detachMtf = nullptr;
        if (mtfSection) {
          for (QPushButton *button : mtfSection->findChildren<QPushButton *>()) {
            if (button && button->text() == QStringLiteral("Detach")) {
              detachMtf = button;
              break;
            }
          }
        }
        if (!sharpness || !mtfChart || !mtfSection || !detachMtf) {
          qCritical() << "Could not locate reference MTF detachable section";
          app.exit(15);
          return;
        }

        // processEvents() may run the overall smoke shutdown timer on very slow
        // sanitizer builds. Never retain raw child pointers across that turn.
        QPointer<SharpnessPanel> guardedSharpness(sharpness);
        QPointer<QWidget> guardedMtfChart(mtfChart);
        QPointer<QWidget> guardedMtfSection(mtfSection);
        QPointer<QPushButton> guardedDetachMtf(detachMtf);
        guardedDetachMtf->click();
        QCoreApplication::processEvents();
        if (!guardedReference || !guardedSharpness || !guardedMtfChart ||
            !guardedMtfSection || !guardedDetachMtf) {
          qCritical() << "Reference MTF section disappeared during detach";
          app.exit(15);
          return;
        }

        QMainWindow *mtfHost =
            qobject_cast<QMainWindow *>(guardedMtfSection->window());
        QDockWidget *mtfDock = nullptr;
        if (mtfHost) {
          for (QDockWidget *candidate : mtfHost->findChildren<QDockWidget *>()) {
            if (candidate && candidate->property("detachablePanel").toBool() &&
                candidate->property("detachableTitle").toString() ==
                    QStringLiteral("MTF Chart") &&
                candidate->isAncestorOf(guardedMtfChart.data())) {
              mtfDock = candidate;
              break;
            }
          }
        }
        if (!mtfDock || !mtfDock->isVisible() || !mtfDock->isFloating() ||
            !mtfDock->widget() ||
            !mtfDock->isAncestorOf(guardedMtfChart.data())) {
          qCritical() << "Reference MTF chart disappeared instead of detaching";
          app.exit(15);
          return;
        }
        QPointer<QDockWidget> guardedMtfDock(mtfDock);
        mtfDock->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        if (!guardedReference || !guardedMtfChart || !guardedDetachMtf) {
          qCritical() << "Reference MTF section disappeared during reattach";
          app.exit(15);
          return;
        }
        if (guardedMtfDock && guardedMtfDock->widget()) {
          qCritical() << "Generic MTF dock retained its content after close";
          app.exit(15);
          return;
        }
        QWidget *referenceInspector =
            guardedReference->workspaceInspectorWidget();
        if (!referenceInspector ||
            !referenceInspector->isAncestorOf(guardedMtfChart.data()) ||
            guardedDetachMtf->text() != QStringLiteral("Detach")) {
          qCritical() << "Reference MTF chart did not reattach after dock close";
          app.exit(15);
          return;
        }
'''
    if old not in text:
        raise SystemExit('reference MTF smoke block not found')
    main.write_text(text.replace(old, new, 1))

workflow = Path('.github/workflows/build-ubuntu.yml')
text = workflow.read_text()
if '--smoke-test 60000 --smoke-test-expect-windows 2' not in text:
    old = '''          --smoke-test 30000 --smoke-test-expect-windows 2 \\
          --smoke-test-tile-activation-stable --smoke-test-menu-order \\
          --smoke-test-new-view --smoke-test-slanted-reference \\
'''
    new = '''          --smoke-test 60000 --smoke-test-expect-windows 2 \\
          --smoke-test-tile-activation-stable --smoke-test-menu-order \\
          --smoke-test-new-view --smoke-test-slanted-reference \\
'''
    if old not in text:
        raise SystemExit('Ubuntu checking smoke timeout block not found')
    workflow.write_text(text.replace(old, new, 1))
