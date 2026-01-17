#include "ScreenPanel.h"
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
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
} // namespace

ScreenPanel::ScreenPanel(StateGetter stateGetter, StateSetter stateSetter,
                         ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent) {
  setupUi();
}

ScreenPanel::~ScreenPanel() = default;

void ScreenPanel::setupUi() {
  // Screen Type Selector
  QComboBox *screenCombo = new ScreenComboBox();
  screenCombo->view()->setIconSize(QSize(64, 64));

  for (int i = 0; i < max_scr_type; ++i) {
    if (!scr_names[i])
      continue;

    scr_type type = (scr_type)i;
    QString name = QString::fromUtf8(scr_names[i]);

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

  // Placeholder for future parameters (scr_to_img_parameters,
  // scr_detect_parameters, solver_parameters)

  updateUI();
}
