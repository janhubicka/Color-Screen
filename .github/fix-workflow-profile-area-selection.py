#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected one match, found {count}\n--- pattern ---\n{old}"
        )
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/qtgui/MultiLineTabWidget.h",
    "    void setTabVisible(int index, bool visible);\n",
    "    void setTabVisible(int index, bool visible);\n"
    "    bool isTabVisible(int index) const;\n",
)

replace_once(
    "src/qtgui/MultiLineTabWidget.cpp",
    '''int MultiLineTabWidget::indexOf(QWidget *page) const
{''',
    '''bool MultiLineTabWidget::isTabVisible(int index) const
{
    return index >= 0 && index < m_tabs.size()
           && !m_tabs[index].button->isHidden();
}

int MultiLineTabWidget::indexOf(QWidget *page) const
{''',
)

replace_once(
    "src/qtgui/MainWindow.cpp",
    '''    workflowProfileLabel->setVisible(
        showProfile && m_workflowNextStepLabel->isVisible());''',
    '''    workflowProfileLabel->setVisible(
        showProfile && !m_workflowNextStepLabel->isHidden());''',
)
