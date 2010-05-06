/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include <QtDebug>

#include <MSceneManager>
#include <MScene>

#include <QApplication>
#include <QDesktopWidget>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>

#include "mdecoratorwindow.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/extensions/Xfixes.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <mabstractdecorator.h>


class MDecorator: public MAbstractDecorator
{
    Q_OBJECT
public:
    MDecorator(MDecoratorWindow *p)
        : MAbstractDecorator(p),
        decorwindow(p)
    {
        connect(this, SIGNAL(windowTitleChanged(const QString&)),
                p, SIGNAL(windowTitleChanged(const QString&)));
    }

    ~MDecorator() {
    }

    virtual void manageEvent(Qt::HANDLE window)
    {
        XTextProperty p;
        QString title;

        if(XGetWMName(QX11Info::display(), window, &p)) {
            if (p.value) {
                title = (char*) p.value;
                XFree(p.value);
            }
        }
        setAvailableGeometry(decorwindow->availableClientRect());
        
        emit windowTitleChanged(title);
    }

protected:
    virtual void activateEvent() {
    }

    virtual void setAutoRotation(bool mode) {
        decorwindow->setOrientationAngleLocked(!mode);
        if (!mode)
            decorwindow->setOrientationAngle(M::Angle0);
    }

    virtual void setOnlyStatusbar(bool mode) {
        emit onlyStatusbar(mode);
    }

signals:

    void windowTitleChanged(const QString&);
    void onlyStatusbar(bool mode);

private:

    MDecoratorWindow *decorwindow;
};

#if 0
static QRect windowRectFromGraphicsItem(const QGraphicsView &view,
                                        const QGraphicsItem &item)
{
    return view.mapFromScene(
               item.mapToScene(
                   item.boundingRect()
               )
           ).boundingRect();
}
#endif

MDecoratorWindow::MDecoratorWindow(QWidget *parent)
    : MWindow(parent)
{
    XSelectInput(QX11Info::display(), winId(), PropertyChangeMask);
    onlyStatusbarAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_ONLY_STATUSBAR", False);
    managedWindowAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_MANAGED_WINDOW", False);
    setTranslucentBackground(true);

    // default setting is not to rotate automatically
    setOrientationAngle(M::Angle0);
    setOrientationAngleLocked(true);

    homeButtonPanel = new MHomeButtonPanel();
    escapeButtonPanel = new MEscapeButtonPanel();
    navigationBar = new MNavigationBar();
    statusBar = new MStatusBar();

    connect(homeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(homeClicked()));
    connect(escapeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(escapeClicked()));

    connect(this, SIGNAL(windowTitleChanged(const QString&)),
            navigationBar, SLOT(setViewMenuDescription(const QString&)));

    sceneManager()->appearSceneWindowNow(statusBar);
    setOnlyStatusbar(false);

    d = new MDecorator(this);
    connect(this, SIGNAL(homeClicked()), d, SLOT(minimize()));
    connect(this, SIGNAL(escapeClicked()), d, SLOT(close()));
    connect(sceneManager(),
            SIGNAL(orientationChanged(M::Orientation)),
            this,
            SLOT(screenRotated(M::Orientation)));
    connect(d, SIGNAL(onlyStatusbar(bool)),
            this, SLOT(setOnlyStatusbar(bool)));

    setFocusPolicy(Qt::NoFocus);
    setSceneSize();
    setMDecoratorWindowProperty();

    setInputRegion();
}

MDecoratorWindow::~MDecoratorWindow()
{
}

bool MDecoratorWindow::x11Event(XEvent *e)
{
    Atom actual;
    int format, result;
    unsigned long n, left;
    unsigned char *data = 0;
    if (e->type == PropertyNotify
        && ((XPropertyEvent*)e)->atom == onlyStatusbarAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    onlyStatusbarAtom, 0, 1, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data)
            setOnlyStatusbar(*((long*)data));
        if (data)
            XFree(data);
        return true;
    } else if (e->type == PropertyNotify
               && ((XPropertyEvent*)e)->atom == managedWindowAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    managedWindowAtom, 0, 1, False,
                                    XA_WINDOW, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data)
            d->manageEvent(*((long*)data));
        if (data)
            XFree(data);
        return true;
    }
    return false;
}

void MDecoratorWindow::setOnlyStatusbar(bool mode)
{
    if (mode) {
        sceneManager()->disappearSceneWindowNow(navigationBar);
        sceneManager()->disappearSceneWindowNow(homeButtonPanel);
        sceneManager()->disappearSceneWindowNow(escapeButtonPanel);
    } else {
        sceneManager()->appearSceneWindowNow(navigationBar);
        sceneManager()->appearSceneWindowNow(homeButtonPanel);
        sceneManager()->appearSceneWindowNow(escapeButtonPanel);
    }
    only_statusbar = mode;
}

void MDecoratorWindow::screenRotated(const M::Orientation &orientation)
{
    Q_UNUSED(orientation);
    setInputRegion();
}

XRectangle MDecoratorWindow::itemRectToScreenRect(const QRect& r)
{
    XRectangle rect;
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    switch (sceneManager()->orientationAngle()) {
    case 0:
            rect.x = r.x();
            rect.y = r.y();
            rect.width = r.width();
            rect.height = r.height();
            break;
    case 90:
            rect.x = xres - r.height();
            rect.y = 0;
            rect.width = r.height();
            rect.height = r.width();
            break;
    case 270:
            rect.x = rect.y = 0;
            rect.width = r.height();
            rect.height = r.width();
            break;
    case 180:
            rect.x = 0;
            rect.y = yres - r.height();
            rect.width = r.width();
            rect.height = r.height();
            break;
    default:
            memset(&rect, 0, sizeof(rect));
            break;
    }
    return rect;
}

void MDecoratorWindow::setInputRegion()
{
    QRegion region;
    region += statusBar->boundingRect().toRect();
    if (!only_statusbar) {
        region += navigationBar->boundingRect().toRect();
        region += homeButtonPanel->boundingRect().toRect();
        region += escapeButtonPanel->boundingRect().toRect();
    }
    decoratorRect = region.boundingRect();

    XRectangle rect = itemRectToScreenRect(decoratorRect);

    Display *dpy = QX11Info::display();
    XserverRegion shapeRegion = XFixesCreateRegion(dpy, &rect, 1);
    XFixesSetWindowShapeRegion(dpy, winId(), ShapeBounding, 0, 0, 0);
    XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput, 0, 0, shapeRegion);

    XFixesDestroyRegion(dpy, shapeRegion);

    XSync(dpy, False);

    // selective compositing
    if (isVisible() && region.isEmpty()) {
        hide();
    } else if (!isVisible() && !region.isEmpty()) {
        show();
    }
}

void MDecoratorWindow::setSceneSize()
{
    // always keep landscape size
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    scene()->setSceneRect(0, 0, xres, yres);
    setMinimumSize(xres, yres);
    setMaximumSize(xres, yres);
}

void MDecoratorWindow::setMDecoratorWindowProperty()
{

    long on = 1;

    XChangeProperty(QX11Info::display(), winId(),
                    XInternAtom(QX11Info::display(), "_MEEGOTOUCH_DECORATOR_WINDOW", False),
                    XA_CARDINAL,
                    32, PropModeReplace,
                    (unsigned char *) &on, 1);
}


const QRect MDecoratorWindow::availableClientRect() const
{
    return decoratorRect;
}

#include "mdecoratorwindow.moc"
