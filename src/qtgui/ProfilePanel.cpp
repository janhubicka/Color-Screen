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

  m_optimizeBtn = new QPushButton(tr("Optimize color"), this);
  m_optimizeBtn->setObjectName(QStringLiteral("ProfileOptimizeButton"));
  m_optimizeBtn->setToolTip(
      tr("Fit the scanner/process color profile from at least four spots."));
  m_form->addRow(m_optimizeBtn);

  // Freshness is owned by MainWindow because it must survive panel refreshes
  // and gate asynchronous optimizer publication.  The panel only presents it.
  m_statusLabel = new QLabel(tr("Profile: no calibration spots"), this);
  m_statusLabel->setObjectName(QStringLiteral("ProfileCalibrationStatus"));
  m_statusLabel->setWordWrap(true);
  m_form->addRow(tr("Status:"), m_statusLabel);

  // Result summary label
  m_resultLabel = new QLabel(tr("—"), this);
  m_resultLabel->setWordWrap(true);
  m_form->addRow(tr("Result:"), m_resultLabel);

  connect(m_optimizeBtn, &QPushButton::clicked, this, [this]() {
    emit optimizeColorRequested(m_autoCheck->isChecked());
  });
  connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (!checked)
      return;

    // Record the current spot set before starting.  The optimizer itself
    // changes render parameters and therefore refreshes every panel; without
    // this snapshot that refresh would look like another auto trigger.
    m_lastAutoSpots = m_stateGetter().profileSpots;
    if (m_lastAutoSpots.size() >= 4)
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
  if (m_optimizeBtn)
    m_optimizeBtn->setEnabled(n >= 4);

  // Auto-trigger only when the profile-spot set itself changed.  Parameter
  // refreshes also happen when an optimization result is applied; treating
  // every refresh as a spot change creates a self-triggering optimization
  // loop and needless cancellation/restart churn.
  bool spotsChanged = state.profileSpots.size() != m_lastAutoSpots.size();
  if (!spotsChanged) {
    for (std::size_t i = 0; i < state.profileSpots.size(); ++i) {
      if (state.profileSpots[i].x != m_lastAutoSpots[i].x ||
          state.profileSpots[i].y != m_lastAutoSpots[i].y) {
        spotsChanged = true;
        break;
      }
    }
  }
  m_lastAutoSpots = state.profileSpots;
  if (isAutoEnabled() && spotsChanged && n >= 4)
    emit optimizeColorRequested(true);
}

/** Present document-owned profile fit freshness without owning that state. */
void ProfilePanel::setCalibrationStatus(const QString &status)
{
  if (m_statusLabel)
    m_statusLabel->setText(status);
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
