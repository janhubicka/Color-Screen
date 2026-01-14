#ifndef SHARPNESS_PANEL_H
#define SHARPNESS_PANEL_H

#include "ParameterPanel.h"

class MTFChartWidget;
class QLabel;
class QWidget;
class QImage;
class QTimer;
template<typename T> class QFutureWatcher;

namespace colorscreen {
    struct progress_info;
    struct sharpen_parameters;
}

class SharpnessPanel : public ParameterPanel
{
    Q_OBJECT
public:
    explicit SharpnessPanel(StateGetter stateGetter, StateSetter stateSetter, ImageGetter imageGetter, QWidget *parent = nullptr);
    ~SharpnessPanel() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void updateMTFChart();
    void updateScreenTiles();
    void scheduleTileUpdate();
    void performTileRender();
    void applyChange(std::function<void(ParameterState&)> modifier) override;
    void onParametersRefreshed(const ParameterState &state) override;
    
    MTFChartWidget *m_mtfChart = nullptr;
    QLabel *m_originalTileLabel = nullptr;
    QLabel *m_bluredTileLabel = nullptr;
    QLabel *m_sharpenedTileLabel = nullptr;
    QWidget *m_tilesContainer = nullptr;
    
    // Async tile rendering
    QFutureWatcher<struct TileRenderResult> *m_tileWatcher = nullptr;
    int m_tileGenerationCounter = 0;
    QTimer *m_updateTimer = nullptr;
    std::shared_ptr<colorscreen::progress_info> m_tileProgress;
    
    // Cached parameters for change detection
    int m_lastTileSize = 0;
    double m_lastPixelSize = 0.0;
    int m_lastScrType = -1;
    colorscreen::sharpen_parameters m_lastSharpen;
    double m_lastRedStripWidth = 0.0;
    double m_lastGreenStripWidth = 0.0;
};

#endif // SHARPNESS_PANEL_H
