#include <QVBoxLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QButtonGroup>
#include <QStyle>

#include "MultiLineTabWidget.h"
#include "FlowLayout.h"

MultiLineTabWidget::MultiLineTabWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_stageBarWidget = new QWidget();
    m_stageBarWidget->setObjectName(QStringLiteral("WorkflowStageBar"));
    m_stageLayout = new FlowLayout(m_stageBarWidget, 2, 2, 2);
    m_stageBarWidget->hide();
    mainLayout->addWidget(m_stageBarWidget);

    m_tabBarWidget = new QWidget();
    m_tabBarWidget->setObjectName(QStringLiteral("DetailedPanelTabBar"));
    m_tabLayout = new FlowLayout(m_tabBarWidget, 2, 2, 2);
    mainLayout->addWidget(m_tabBarWidget);

    m_stack = new QStackedWidget();
    mainLayout->addWidget(m_stack);

    m_stageGroup = new QButtonGroup(this);
    m_stageGroup->setExclusive(true);
    connect(m_stageGroup, QOverload<int>::of(&QButtonGroup::idClicked), this,
            &MultiLineTabWidget::onStageClicked);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    connect(m_group, QOverload<int>::of(&QButtonGroup::idClicked), this,
            &MultiLineTabWidget::onTabClicked);

    // Primary stages and secondary panel tabs are one navigation system.
    // Use the same palette-aware underline style for both instead of a
    // hard-coded dark pill bar over a differently styled detailed tab bar.
    const QString tabButtonStyle = QStringLiteral(
        "QPushButton {"
        "  border: 1px solid transparent;"
        "  border-bottom: 2px solid palette(mid);"
        "  padding: 5px 10px;"
        "  background: transparent;"
        "  color: palette(window-text);"
        "  font-weight: 600;"
        "  border-radius: 2px;"
        "}"
        "QPushButton:hover {"
        "  background: palette(alternate-base);"
        "}"
        "QPushButton:checked {"
        "  background: palette(alternate-base);"
        "  border-bottom: 2px solid palette(highlight);"
        "}"
        "QPushButton:focus {"
        "  border: 1px dotted palette(highlight);"
        "  border-bottom: 2px solid palette(highlight);"
        "}");
    m_stageBarWidget->setStyleSheet(tabButtonStyle);
    m_tabBarWidget->setStyleSheet(tabButtonStyle);
}

/** Add one presentation-only workflow stage. */
int MultiLineTabWidget::addStage(const QString &label, const QString &key)
{
    Q_ASSERT(!key.isEmpty());
    Q_ASSERT(indexOfStageKey(key) < 0);

    auto *button = new QPushButton(label);
    button->setCheckable(true);
    button->setObjectName(QStringLiteral("WorkflowStageButton"));
    button->setProperty("stageKey", key);

    const int id = m_stages.size();
    m_stages.append({button, key});
    m_stageGroup->addButton(button, id);
    m_stageLayout->addWidget(button);

    if (m_currentStage < 0) {
        m_currentStage = id;
        button->setChecked(true);
    }

    m_stageBarWidget->show();
    refreshNavigationVisibility();
    return id;
}

/** Configure the stable default panel when entering an available stage. */
void MultiLineTabWidget::setStageDefaultPanel(
    const QString &stageKey, const QString &panelKey)
{
    const int stageIndex = indexOfStageKey(stageKey);
    const int panelIndex = indexOfKey(panelKey);
    if (stageIndex < 0 || panelIndex < 0 ||
        m_tabs[panelIndex].stageKey != stageKey)
        return;

    m_stages[stageIndex].defaultPanelKey = panelKey;
}

int MultiLineTabWidget::addTab(QWidget *page, const QString &label,
                               const QString &key, const QString &stageKey)
{
    Q_ASSERT(key.isEmpty() || indexOfKey(key) < 0);
    Q_ASSERT(stageKey.isEmpty() || indexOfStageKey(stageKey) >= 0);

    QPushButton *btn = new QPushButton(label);
    btn->setCheckable(true);
    if (!key.isEmpty())
        btn->setProperty("tabKey", key);
    if (!stageKey.isEmpty())
        btn->setProperty("stageKey", stageKey);

    int id = m_tabs.size();
    m_tabs.append({btn, page, key, stageKey, true});
    m_group->addButton(btn, id);
    m_tabLayout->addWidget(btn);
    m_stack->addWidget(page);

    if (id == 0) {
        btn->setChecked(true);
        m_stack->setCurrentWidget(page);
        const int stageIndex = stageIndexForTab(id);
        if (stageIndex >= 0)
            setCurrentStageIndex(stageIndex);
    }

    refreshNavigationVisibility();
    return id;
}

void MultiLineTabWidget::setTabVisible(int index, bool visible)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    m_tabs[index].visible = visible;
    refreshNavigationVisibility();

    if (!visible && m_stack->currentIndex() == index) {
        int fallback = firstVisibleTabInStage(m_currentStage);
        if (fallback < 0) {
            for (int i = 0; i < m_tabs.size(); ++i)
                if (m_tabs[i].visible) {
                    fallback = i;
                    break;
                }
        }
        if (fallback >= 0)
            setCurrentIndex(fallback);
    }
}

int MultiLineTabWidget::indexOf(QWidget *page) const
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].page == page)
            return i;
    }
    return -1;
}

void MultiLineTabWidget::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size() || !m_tabs[index].visible)
        return;

    const int stageIndex = stageIndexForTab(index);
    if (stageIndex >= 0)
        setCurrentStageIndex(stageIndex);

    if (m_stack->currentIndex() == index) {
        m_tabs[index].button->setChecked(true);
        return;
    }

    m_tabs[index].button->setChecked(true);
    m_stack->setCurrentIndex(index);
    emit currentChanged(index);
}

int MultiLineTabWidget::currentIndex() const
{
    return m_stack->currentIndex();
}

QWidget *MultiLineTabWidget::widget(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs[index].page;
    return nullptr;
}

void MultiLineTabWidget::setTabToolTip(int index, const QString &tooltip)
{
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].button->setToolTip(tooltip);
    }
}

int MultiLineTabWidget::count() const
{
    return m_tabs.size();
}

QString MultiLineTabWidget::tabText(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs[index].button->text();
    return QString();
}

/** Return stable application-preference key for INDEX. */
QString MultiLineTabWidget::tabKey(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs[index].key;
    return QString();
}

/** Resolve stable application-preference KEY to its current tab index. */
int MultiLineTabWidget::indexOfKey(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].key == key)
            return i;
    return -1;
}

/** Return the number of registered workflow stages. */
int MultiLineTabWidget::stageCount() const
{
    return m_stages.size();
}

/** Return the user-visible label of workflow stage INDEX. */
QString MultiLineTabWidget::stageText(int index) const
{
    if (index >= 0 && index < m_stages.size())
        return m_stages[index].button->text();
    return QString();
}

/** Return the stable workflow-stage key for INDEX. */
QString MultiLineTabWidget::stageKey(int index) const
{
    if (index >= 0 && index < m_stages.size())
        return m_stages[index].key;
    return QString();
}

/** Resolve stable workflow-stage KEY to its current stage index. */
int MultiLineTabWidget::indexOfStageKey(const QString &key) const
{
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_stages.size(); ++i)
        if (m_stages[i].key == key)
            return i;
    return -1;
}

/** Return the stable key for the currently selected workflow stage. */
QString MultiLineTabWidget::currentStageKey() const
{
    return stageKey(m_currentStage);
}

/** Return the stage owning TABINDEX. */
int MultiLineTabWidget::stageIndexForTab(int tabIndex) const
{
    if (tabIndex < 0 || tabIndex >= m_tabs.size())
        return -1;
    return indexOfStageKey(m_tabs[tabIndex].stageKey);
}

/** Return the first logically visible detailed panel in STAGEINDEX. */
int MultiLineTabWidget::firstVisibleTabInStage(int stageIndex) const
{
    if (stageIndex < 0 || stageIndex >= m_stages.size())
        return -1;

    const QString key = m_stages[stageIndex].key;
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].visible && m_tabs[i].stageKey == key)
            return i;
    return -1;
}

/** Select STAGEINDEX and update button visibility without changing the page. */
void MultiLineTabWidget::setCurrentStageIndex(int stageIndex)
{
    if (stageIndex < 0 || stageIndex >= m_stages.size())
        return;
    if (m_currentStage == stageIndex) {
        m_stages[stageIndex].button->setChecked(true);
        refreshNavigationVisibility();
        return;
    }

    m_currentStage = stageIndex;
    m_stages[stageIndex].button->setChecked(true);
    refreshNavigationVisibility();
}

/** Compose application visibility with the selected workflow stage. */
void MultiLineTabWidget::refreshNavigationVisibility()
{
    if (m_stages.isEmpty()) {
        m_stageBarWidget->hide();
        int visibleCount = 0;
        for (TabInfo &tab : m_tabs) {
            tab.button->setVisible(tab.visible);
            visibleCount += tab.visible ? 1 : 0;
        }
        // In particular, the single Sharpness panel in a slanted-edge
        // reference view needs no second navigation row.
        m_tabBarWidget->setVisible(visibleCount > 1);
        return;
    }

    QList<bool> stageHasVisibleTabs;
    stageHasVisibleTabs.reserve(m_stages.size());
    for (const StageInfo &stage : m_stages) {
        bool hasVisible = false;
        for (const TabInfo &tab : m_tabs)
            if (tab.visible && tab.stageKey == stage.key) {
                hasVisible = true;
                break;
            }
        stageHasVisibleTabs.append(hasVisible);
    }

    if (m_currentStage < 0 || m_currentStage >= m_stages.size() ||
        !stageHasVisibleTabs[m_currentStage]) {
        m_currentStage = -1;
        for (int i = 0; i < stageHasVisibleTabs.size(); ++i)
            if (stageHasVisibleTabs[i]) {
                m_currentStage = i;
                break;
            }
    }

    for (int i = 0; i < m_stages.size(); ++i) {
        m_stages[i].button->setVisible(stageHasVisibleTabs[i]);
        m_stages[i].button->setChecked(i == m_currentStage);
    }

    const QString selectedStage = currentStageKey();
    int visibleDetailCount = 0;
    for (TabInfo &tab : m_tabs) {
        const bool belongsToSelectedStage =
            tab.stageKey.isEmpty() || tab.stageKey == selectedStage;
        const bool showDetail = tab.visible && belongsToSelectedStage;
        tab.button->setVisible(showDetail);
        visibleDetailCount += showDetail ? 1 : 0;
    }

    // A stage with only one available panel already names that panel: hide
    // the redundant second row, not its page or its stable tab identity.
    // Count logical availability, never effective ancestor visibility during
    // workspace inspector reparenting or detached-view transitions.
    m_tabBarWidget->setVisible(visibleDetailCount > 1);
    m_stageBarWidget->setVisible(!m_stages.isEmpty());
}

void MultiLineTabWidget::onTabClicked(int id)
{
    if (id < 0 || id >= m_tabs.size() || !m_tabs[id].visible)
        return;

    const int stageIndex = stageIndexForTab(id);
    if (stageIndex >= 0)
        setCurrentStageIndex(stageIndex);

    m_tabs[id].button->setChecked(true);
    m_stack->setCurrentIndex(id);
    emit currentChanged(id);
    emit tabActivated(id);
}

/** Switch workflow stage and expose its first available detailed panel. */
void MultiLineTabWidget::onStageClicked(int id)
{
    if (id < 0 || id >= m_stages.size() || m_stages[id].button->isHidden())
        return;

    const bool enteringStage = m_currentStage != id;
    setCurrentStageIndex(id);

    const int current = m_stack->currentIndex();
    // Clicking the already active stage must not undo an intentional
    // detailed-panel choice (for example Contact copy).
    if (!enteringStage && current >= 0 && current < m_tabs.size() &&
        m_tabs[current].visible && stageIndexForTab(current) == id)
        return;

    int destination = -1;
    if (enteringStage && !m_stages[id].defaultPanelKey.isEmpty()) {
        const int preferred = indexOfKey(m_stages[id].defaultPanelKey);
        if (preferred >= 0 && m_tabs[preferred].visible &&
            stageIndexForTab(preferred) == id)
            destination = preferred;
    }
    if (destination < 0)
        destination = firstVisibleTabInStage(id);
    if (destination < 0)
        return;

    setCurrentIndex(destination);
    if (m_stack->currentIndex() == destination)
        emit tabActivated(destination);
}
