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

    /** Add a tab with optional stable KEY used for application preferences. */
    int addTab(QWidget *page, const QString &label,
               const QString &key = QString());
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

signals:
    /** Emitted for every logical tab change, including programmatic changes. */
    void currentChanged(int index);

    /** Emitted only when the user activates a tab button. */
    void tabActivated(int index);

private slots:
    void onTabClicked(int id);

private:
    QWidget *m_tabBarWidget;
    FlowLayout *m_tabLayout;
    QStackedWidget *m_stack;
    QButtonGroup *m_group;

    struct TabInfo {
        QPushButton *button;
        QWidget *page;
        QString key;
    };
    QList<TabInfo> m_tabs;
};

#endif // MULTI_LINE_TAB_WIDGET_H
