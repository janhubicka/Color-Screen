#ifndef COLOR_PANEL_H
#define COLOR_PANEL_H

#include "../libcolorscreen/include/render-parameters.h"
#include "TilePreviewPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QWidget>
#include <vector>

class CIEChartWidget; // Forward declaration
class SpectraChartWidget;

class ColorPanel : public TilePreviewPanel {
  Q_OBJECT
public:
  explicit ColorPanel(StateGetter stateGetter, StateSetter stateSetter,
                      ImageGetter imageGetter, QWidget *parent = nullptr);
  ~ColorPanel() override;

  // Accessors for Dock Widgets
  QWidget *getSpectraChartWidget() const;
  void reattachSpectraChart(QWidget *widget);
  void reattachCorrectedTiles(QWidget *widget);

  QWidget *getGamutChartWidget() const;
  void reattachGamutChart(QWidget *widget);
  QWidget *getCorrectedGamutChartWidget() const;
  void reattachCorrectedGamutChart(QWidget *widget);

signals:
  void detachSpectraChartRequested(QWidget *widget);
  void detachCorrectedTilesRequested(QWidget *widget);
  void detachGamutChartRequested(QWidget *widget);
  void detachCorrectedGamutChartRequested(QWidget *widget);

protected:
  // TilePreviewPanel overrides
  std::vector<std::pair<colorscreen::render_screen_tile_type, QString>>
  getTileTypes() const override;
  bool shouldUpdateTiles(const ParameterState &state) override;
  void onTileUpdateScheduled() override;
  bool isTileRenderingEnabled(const ParameterState &state) const override;
  bool requiresScan() const override { return false; }
  void resizeEvent(QResizeEvent* event) override;

private:
  void setupUi();
  void updateSpectraChart();
  void applyChange(std::function<void(ParameterState &)> modifier, const QString &description = QString()) override;

  // Cached parameters for change detection
  colorscreen::render_parameters m_lastRParams;
  int m_lastScrType = -1;

  QWidget *m_spectraSection = nullptr;
  SpectraChartWidget *m_spectraChart = nullptr;
  QVBoxLayout *m_spectraContainer = nullptr;
  QComboBox *m_spectraMode = nullptr;
  TilePreviewPanel *m_correctedPreview = nullptr;

  struct GamutChartGroup {
    CIEChartWidget *chart = nullptr;
    QComboBox *referenceCombo = nullptr;
    QWidget *section = nullptr;
    QVBoxLayout *container = nullptr;
    QString name;
    bool corrected;
  };

  void initGamutGroup(GamutChartGroup &group, const QString &name, bool corrected, 
                      std::function<void(QWidget*)> detachSignalEmitter);
  void updateGamutGroup(GamutChartGroup &group);
  void updateGamutReference(GamutChartGroup &group);
  void reattachGamutGroup(GamutChartGroup &group, QWidget *widget,
                          std::function<void(QWidget*)> detachSignalEmitter);

  GamutChartGroup m_gamutGroup;
  GamutChartGroup m_correctedGamutGroup;
};

#endif // COLOR_PANEL_H
