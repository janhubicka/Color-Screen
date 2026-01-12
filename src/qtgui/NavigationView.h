#pragma once
#include <QWidget>
#include <QImage>
#include <memory>

class NavigationView : public QWidget
{
    Q_OBJECT
public:
    explicit NavigationView(QWidget *parent = nullptr);
    
protected:
    void paintEvent(QPaintEvent *event) override;
};
