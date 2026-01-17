#include "ScreenPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "TilePreviewPanel.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <map>
#include <vector>

using namespace colorscreen;

namespace {
class ScreenComboBox : public QComboBox {
public:
  using QComboBox::QComboBox;

protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentIcon = QIcon();
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
  }
};

class ScreenPreviewPanel : public TilePreviewPanel {
public:
  ScreenPreviewPanel(StateGetter stateGetter, StateSetter stateSetter,
                     ImageGetter imageGetter, QWidget *parent = nullptr)
      : TilePreviewPanel(stateGetter, stateSetter, imageGetter, parent, false) {
  }

  void init(const QString &title) { setupTiles(title); }

protected:
  std::vector<std::pair<render_screen_tile_type, QString>>
  getTileTypes() const override {
    return {{original_screen, "Screen"}};
  }

  bool shouldUpdateTiles(const ParameterState &state) override {
    if ((int)state.scrToImg.type != m_lastScrType)
      return true;
    return false;
  }

  void onTileUpdateScheduled() override {
    ParameterState state = m_stateGetter();
    m_lastScrType = (int)state.scrToImg.type;
  }

  bool requiresScan() const override { return false; }

private:
  int m_lastScrType = -1;
};
} // namespace

ScreenPanel::ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
                         ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ScreenPanel::~ScreenPanel() = default;

void ScreenPanel::reattachPreview(QWidget *widget) {
  if (m_previewPanel)
    m_previewPanel->reattachTiles(widget);
}

void ScreenPanel::setupUi() {
  // Screen Type Selector
  QComboBox *screenCombo = new ScreenComboBox();
  screenCombo->view()->setIconSize(QSize(64, 64));

  for (int i = 0; i < max_scr_type; ++i) {
    if (!scr_names[i].name)
      continue;

    scr_type type = (scr_type)i;
    QString name = QString::fromUtf8(scr_names[i].pretty_name);

    if (type == Random) {
      screenCombo->addItem(name, i);
    } else {
      // Render Preview
      int w = 64;
      int h = 64;
      std::vector<uint8_t> buffer(w * h * 3);

      tile_parameters tile;
      tile.pixels = buffer.data();
      tile.rowstride = w * 3;
      tile.pixelbytes = 3;
      tile.width = w;
      tile.height = h;
      tile.pos = {0.0, 0.0};
      tile.step = 1.0;

      render_parameters rparams;

      bool ok = render_screen_tile(tile, type, rparams, 1.0, original_screen,
                                   nullptr);

      if (ok) {
        QImage img(buffer.data(), w, h, w * 3, QImage::Format_RGB888);
        QIcon icon(QPixmap::fromImage(img.copy()));
        screenCombo->addItem(icon, name, i);
      } else {
        screenCombo->addItem(name, i);
      }

    }
  }

  addEnumTooltips(screenCombo, scr_names, max_scr_type);

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow("Screen type", screenCombo);
  } else {
    m_form->addRow("Screen type", screenCombo);
  }

  // Connect: UI -> State
  connect(screenCombo, QOverload<int>::of(&QComboBox::activated), this,
          [this, screenCombo](int index) {
            int val = screenCombo->itemData(index).toInt();
            applyChange(
                [val](ParameterState &s) { s.scrToImg.type = (scr_type)val; });
          });

  // Updater: State -> UI
  m_paramUpdaters.push_back([screenCombo](const ParameterState &state) {
    int val = (int)state.scrToImg.type;
    int idx = screenCombo->findData(val);
    if (idx != -1) {
      screenCombo->blockSignals(true);
      screenCombo->setCurrentIndex(idx);
      screenCombo->blockSignals(false);
    }
  });

  addSeparator("Regular screen");

  ScreenPreviewPanel *preview =
      new ScreenPreviewPanel(m_stateGetter, m_stateSetter, m_imageGetter);
  m_previewPanel = preview;
  preview->init("");

  connect(preview, &TilePreviewPanel::detachTilesRequested, this,
          &ScreenPanel::detachPreviewRequested);

  m_widgetStateUpdaters.push_back([this, preview]() {
    preview->updateUI();
    bool visible = m_stateGetter().scrToImg.type != Random;
    preview->setVisible(visible);
  });

  if (m_currentGroupForm) {
    m_currentGroupForm->addRow(preview);
  } else {
    m_form->addRow(preview);
  }

  // Placeholder for future parameters (scr_to_img_parameters,
  // scr_detect_parameters, solver_parameters)

  updateUI();
}
