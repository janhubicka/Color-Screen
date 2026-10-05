#ifndef MULTI_LINE_TAB_WIDGET_H
#define MULTI_LINE_TAB_WIDGET_H

#include <QWidget>
#include <QList>
#include <QString>
#include <QButtonGroup>

class FlowLayout;
class QStackedWidget;
class QPushButton;

class MultiLineTabWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiLineTabWidget(QWidget *parent = nullptr);

    /** Add a workflow stage with stable KEY used only for presentation. */
    int addStage(const QString &label, const QString &key);

    /** Add a tab with optional stable KEY and workflow STAGEKEY. */
    int addTab(QWidget *page, const QString &label,
               const QString &key = QString(),
               const QString &stageKey = QString());
    void setTabVisible(int index, bool visible);
    int indexOf(QWidget *page) const;
    void setCurrentIndex(int index);
    int currentIndex() const;
    QWidget *widget(int index) const;
    void setTabToolTip(int index, const QString &tooltip);

    /** Return the number of registered tabs, including currently hidden tabs. */
    int count() const;

    /** Return the label of tab INDEX, or an empty string for an invalid index. */
    QString tabText(int index) const;

    /** Return the stable key of tab INDEX, or an empty string if unavailable. */
    QString tabKey(int index) const;

    /** Return the tab index for stable KEY, or -1 when no tab has that key. */
    int indexOfKey(const QString &key) const;

    /** Return the number of registered workflow stages. */
    int stageCount() const;

    /** Return the label of workflow stage INDEX, or empty for an invalid index. */
    QString stageText(int index) const;

    /** Return the stable key of workflow stage INDEX, or empty if unavailable. */
    QString stageKey(int index) const;

    /** Return the workflow-stage index for stable KEY, or -1 if unavailable. */
    int indexOfStageKey(const QString &key) const;

    /** Return the stable key of the currently selected workflow stage. */
    QString currentStageKey() const;

signals:
    /** Emitted for every logical tab change, including programmatic changes. */
    void currentChanged(int index);

    /** Emitted only when the user activates a tab button or workflow stage. */
    void tabActivated(int index);

private slots:
    void onTabClicked(int id);
    void onStageClicked(int id);

private:
    /** Recompute which stage and panel buttons are logically available. */
    void refreshNavigationVisibility();

    /** Select STAGEINDEX without choosing a different panel. */
    void setCurrentStageIndex(int stageIndex);

    /** Return the stage index owning TABINDEX, or -1 for an ungrouped tab. */
    int stageIndexForTab(int tabIndex) const;

    /** Return the first logically visible tab in STAGEINDEX, or -1. */
    int firstVisibleTabInStage(int stageIndex) const;

    QWidget *m_stageBarWidget;
    FlowLayout *m_stageLayout;
    QButtonGroup *m_stageGroup;
    QWidget *m_tabBarWidget;
    FlowLayout *m_tabLayout;
    QStackedWidget *m_stack;
    QButtonGroup *m_group;

    struct StageInfo {
        QPushButton *button;
        QString key;
    };
    QList<StageInfo> m_stages;
    int m_currentStage = -1;

    struct TabInfo {
        QPushButton *button;
        QWidget *page;
        QString key;
        QString stageKey;
        bool visible = true;
    };
    QList<TabInfo> m_tabs;
};

#endif // MULTI_LINE_TAB_WIDGET_H
