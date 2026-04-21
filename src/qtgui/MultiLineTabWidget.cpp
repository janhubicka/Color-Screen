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

    m_tabBarWidget = new QWidget();
    m_tabLayout = new FlowLayout(m_tabBarWidget, 2, 2, 2);
    mainLayout->addWidget(m_tabBarWidget);

    m_stack = new QStackedWidget();
    mainLayout->addWidget(m_stack);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    connect(m_group, QOverload<int>::of(&QButtonGroup::idClicked), this, &MultiLineTabWidget::onTabClicked);

    // Styling for tabs
    m_tabBarWidget->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid transparent;"
        "  border-bottom: 2px solid #333;"
        "  padding: 6px 12px;"
        "  background: transparent;"
        "  color: #aaa;"
        "  font-weight: 500;"
        "  border-radius: 2px;"
        "}"
        "QPushButton:hover {"
        "  background: #2a2a2a;"
        "  color: #fff;"
        "}"
        "QPushButton:checked {"
        "  color: #fff;"
        "  border-bottom: 2px solid #3d8af7;"
        "  background: #333;"
        "}"
    );
}

int MultiLineTabWidget::addTab(QWidget *page, const QString &label)
{
    QPushButton *btn = new QPushButton(label);
    btn->setCheckable(true);
    
    int id = m_tabs.size();
    m_tabs.append({btn, page});
    m_group->addButton(btn, id);
    m_tabLayout->addWidget(btn);
    m_stack->addWidget(page);

    if (id == 0) {
        btn->setChecked(true);
        m_stack->setCurrentWidget(page);
    }

    return id;
}

void MultiLineTabWidget::setTabVisible(int index, bool visible)
{
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].button->setVisible(visible);
        // If the current tab becomes hidden, switch to the first visible one
        if (!visible && m_stack->currentIndex() == index) {
            for (int i = 0; i < m_tabs.size(); ++i) {
                if (m_tabs[i].button->isVisible()) {
                    setCurrentIndex(i);
                    break;
                }
            }
        }
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
    if (index >= 0 && index < m_tabs.size()) {
        m_tabs[index].button->setChecked(true);
        m_stack->setCurrentIndex(index);
        emit currentChanged(index);
    }
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

void MultiLineTabWidget::onTabClicked(int id)
{
    if (id >= 0 && id < m_tabs.size()) {
        m_stack->setCurrentIndex(id);
        emit currentChanged(id);
    }
}
