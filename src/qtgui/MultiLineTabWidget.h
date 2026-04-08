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

    int addTab(QWidget *page, const QString &label);
    void setTabVisible(int index, bool visible);
    int indexOf(QWidget *page) const;
    void setCurrentIndex(int index);
    int currentIndex() const;
    QWidget *widget(int index) const;

signals:
    void currentChanged(int index);

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
    };
    QList<TabInfo> m_tabs;
};

#endif // MULTI_LINE_TAB_WIDGET_H
