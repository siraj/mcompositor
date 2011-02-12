#include "windowframe.h"


QtWindowFrame::QtWindowFrame(QWidget *parent) :
    QWidget(parent)
{
  mDeco = new QtDeco(this);
}

QtWindowFrame::~QtWindowFrame()
{
}

bool QtWindowFrame::x11Event(XEvent *event)
{

}
