#include "ProfilePanel.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ProfilePanel::ProfilePanel(StateGetter stateGetter, StateSetter stateSetter,
                           ImageGetter imageGetter, QWidget *parent)
    : ParameterPanel(stateGetter, stateSetter, imageGetter, parent)
{
  setupUi();
}

ProfilePanel::~ProfilePanel() = default;

void ProfilePanel::setupUi()
{
  // ── Spots section ─────────────────────────────────────────────────────────
  addSeparator("Profile spots");

  // Spot count label
  m_spotCountLabel = new QLabel(tr("No spots"), this);
  m_form->addRow(tr("Spots:"), m_spotCountLabel);

  // Add spot toggle button + Clear all side by side
  {
    auto *row = new QWidget(this);
    auto *hlay = new QHBoxLayout(row);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_addSpotBtn = new QPushButton(tr("Add spot"), row);
    m_addSpotBtn->setCheckable(true);
    m_addSpotBtn->setToolTip(tr("Click image to add profile spots (right-click to remove)"));
    hlay->addWidget(m_addSpotBtn, 1);

    auto *clearBtn = new QPushButton(tr("Clear all"), row);
    hlay->addWidget(clearBtn, 1);

    m_form->addRow(row);

    connect(m_addSpotBtn, &QPushButton::toggled, this, [this](bool checked) {
      m_addSpotActive = checked;
      emit addSpotModeRequested(checked);
    });

    connect(clearBtn, &QPushButton::clicked, this, [this]() {
      applyChange([](ParameterState &s) {
        s.profileSpots.clear();
      }, "Clear profile spots");
    });
  }

  // Show profile spots checkbox
  m_showProfileSpotsCheck = new QCheckBox(tr("Show profile spots"), this);
  m_form->addRow(m_showProfileSpotsCheck);
  m_showProfileSpotsCheck->setChecked(true); // on by default
  connect(m_showProfileSpotsCheck, &QCheckBox::toggled,
          this, &ProfilePanel::showProfileSpotsChanged);

  // ── Optimization section ───────────────────────────────────────────────────
  addSeparator("Color optimization");

  m_autoCheck = new QCheckBox(tr("Auto optimize"), this);
  m_autoCheck->setObjectName("autoColorOptBox");
  m_form->addRow(m_autoCheck);

  auto *optimizeBtn = new QPushButton(tr("Optimize color"), this);
  m_form->addRow(optimizeBtn);

  // Result summary label
  m_resultLabel = new QLabel(tr("—"), this);
  m_resultLabel->setWordWrap(true);
  m_form->addRow(tr("Result:"), m_resultLabel);

  connect(optimizeBtn, &QPushButton::clicked, this, [this]() {
    emit optimizeColorRequested(m_autoCheck->isChecked());
  });
  connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked)
      emit optimizeColorRequested(true);
  });
}

bool ProfilePanel::isAutoEnabled() const
{
  return m_autoCheck && m_autoCheck->isChecked();
}

void ProfilePanel::onParametersRefreshed(const ParameterState &state)
{
  // (m_showProfileSpotsCheck is kept in sync via MainWindow → setShowProfileSpots)

  // Update spot count label
  int n = (int)state.profileSpots.size();
  m_spotCountLabel->setText(n == 0 ? tr("No spots") :
                            n == 1 ? tr("1 spot")    :
                            tr("%1 spots").arg(n));

  // Auto-trigger optimizer if auto mode and spots changed
  if (isAutoEnabled() && n > 0)
    emit optimizeColorRequested(true);
}

void ProfilePanel::setSpotResults(const std::vector<colorscreen::color_match> &results)
{
  if (results.empty()) {
    m_resultLabel->setText(tr("—"));
    return;
  }
  // Average deltaE across all spots
  double total = 0;
  for (const auto &m : results)
    total += m.deltaE;
  double avg = total / results.size();
  m_resultLabel->setText(tr("%1 spots, avg ΔE₂₀₀₀ = %2")
                           .arg(results.size())
                           .arg(avg, 0, 'f', 2));
}
