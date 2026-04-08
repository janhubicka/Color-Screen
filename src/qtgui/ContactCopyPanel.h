#ifndef CONTACT_COPY_PANEL_H
#define CONTACT_COPY_PANEL_H

#include "ParameterPanel.h"
#include "TaskQueue.h"
#include <QThread>
#include <QVariant>

class HDCurveWidget;
class QDoubleSpinBox;
class QLabel;

class ContactCopyPanel : public ParameterPanel {
  Q_OBJECT
public:
  ContactCopyPanel(StateGetter stateGetter, StateSetter stateSetter,
                ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ContactCopyPanel() override;

signals:
  void detachHDCurveRequested(QWidget *widget);

public:
  void reattachHDCurve(QWidget *widget);

private slots:
  void onTriggerHistogram(int reqId, std::shared_ptr<colorscreen::progress_info> progress, const QVariant &userData);
  void onHistogramFinished(int reqId, std::vector<uint64_t> data, double minx, double maxx, bool success);

private:
  void setupUi();
  void updateSpinBoxes();

  HDCurveWidget *m_hdCurveWidget = nullptr;
  QVBoxLayout *m_hdCurveContainer = nullptr;
  
  QDoubleSpinBox *m_minXSpin = nullptr;
  QDoubleSpinBox *m_minYSpin = nullptr;
  QDoubleSpinBox *m_linear1XSpin = nullptr;
  QDoubleSpinBox *m_linear1YSpin = nullptr;
  QDoubleSpinBox *m_linear2XSpin = nullptr;
  QDoubleSpinBox *m_linear2YSpin = nullptr;
  QDoubleSpinBox *m_maxXSpin = nullptr;
  QDoubleSpinBox *m_maxYSpin = nullptr;

  bool m_updatingSpinBoxes = false;
  
  TaskQueue m_taskQueue;
  class HistogramWorker *m_worker = nullptr;
  QThread m_workerThread;
  int m_lastHistogramReqId = 0;

  QLabel *m_gammaLabel = nullptr;
};

struct HistogramRequestData {
    colorscreen::render_parameters params;
    int steps;
    double minX;
    double maxX;
    colorscreen::hd_axis_type axisType;
};
Q_DECLARE_METATYPE(HistogramRequestData)

#endif // CONTACT_COPY_PANEL_H
