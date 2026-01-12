#include "NavigationView.h"
#include <QPainter>

NavigationView::NavigationView(QWidget *parent) : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void NavigationView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawText(rect(), Qt::AlignCenter, "Navigation View");
}
