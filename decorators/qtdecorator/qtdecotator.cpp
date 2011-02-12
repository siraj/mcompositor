#include "qtdecotator.h"
#include "windowframe.h"
#include <QDebug>.

//x headers

#include <QX11Info>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

QtDeco::QtDeco(QtWindowFrame *parent) :
    MAbstractDecorator(parent),
    window(parent)
{

}

QtDeco::~QtDeco()
{
}

void QtDeco::activateEvent()
{
    //does nothing o.O ?
}

void QtDeco::manageEvent(Qt::HANDLE window)
{
    XTextProperty p;
    QString title;

    if (XGetWMName(QX11Info::display(), window, &p)) {
        if (p.value) {
            title = (char *) p.value;
            XFree(p.value);
            qDebug() << Q_FUNC_INFO << title;
        }
    }
}
