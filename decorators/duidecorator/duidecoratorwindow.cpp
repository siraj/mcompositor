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

#include <DuiSceneManager>
#include <DuiScene>

#include <QCoreApplication>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>

#include "duidecoratorwindow.h"

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

#include <duiabstractdecorator.h>


class DuiDecorator: public DuiAbstractDecorator
{
    Q_OBJECT
public:
    DuiDecorator(DuiDecoratorWindow *p)
        : DuiAbstractDecorator(p)
    { 
        connect(this, SIGNAL(windowTitleChanged(const QString&)),
                p, SIGNAL(windowTitleChanged(const QString&)));
    }
    
    ~DuiDecorator() {
    }

protected:
    virtual void activateEvent() {
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
        
        emit windowTitleChanged(title);
    }    

signals:
    
    void windowTitleChanged(const QString&);
};

DuiDecoratorWindow::DuiDecoratorWindow(QWidget *parent)
    : DuiWindow(parent)
{
    setTranslucentBackground(true);
    // We do not rotate (change orientation) at all.
    setOrientationAngle(Dui::Angle0, Dui::ImmediateOrientationChange);
    setOrientationAngleLocked(true);

    DuiDecorator *d = new DuiDecorator(this);
    connect(this, SIGNAL(homeClicked()), d, SLOT(minimize()));
    connect(this, SIGNAL(escapeClicked()), d, SLOT(close()));
}

DuiDecoratorWindow::~DuiDecoratorWindow()
{
}

void DuiDecoratorWindow::init(DuiSceneManager &sceneManager)
{    
    setFocusPolicy(Qt::NoFocus);
    setSceneSize(sceneManager);
    setDuiDecoratorWindowProperty();
}

void DuiDecoratorWindow::setInputRegion(const QRegion &region)
{
    Display *dpy = QX11Info::display();

    int size = region.rects().size();

    //we should receive correct pointer even if region is empty
    XRectangle *rects = (XRectangle *)malloc(sizeof(XRectangle) * size);

    XRectangle *rect = rects;
    for (int i = 0; i < size; i ++, rect++) {
        fillXRectangle(rect, region.rects().at(i));
    }


    XserverRegion shapeRegion = XFixesCreateRegion(dpy, rects, size);
    XFixesSetWindowShapeRegion(dpy, winId(), ShapeBounding, 0, 0, 0);
    XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput, 0, 0, shapeRegion);

    XFixesDestroyRegion(dpy, shapeRegion);

    free(rects);
    XSync(dpy, False);

    // selective compositing
    if (isVisible() && region.isEmpty()) {
        hide();
    } else if (!isVisible() && !region.isEmpty()) {
        show();
    }
}

void DuiDecoratorWindow::fillXRectangle(XRectangle *xRect, const QRect &rect) const
{
    xRect->x = rect.x();
    xRect->y = rect.y();
    xRect->width = rect.width();
    xRect->height = rect.height();
}

void DuiDecoratorWindow::setSceneSize(DuiSceneManager &sceneManager)
{
    QSize sceneSize = sceneManager.visibleSceneSize();
    scene()->setSceneRect(0, 0, sceneSize.width(), sceneSize.height());
    setMinimumSize(sceneSize.width(), sceneSize.height());
    setMaximumSize(sceneSize.width(), sceneSize.height());
}

void DuiDecoratorWindow::setDuiDecoratorWindowProperty()
{

    long on = 1;

    XChangeProperty(QX11Info::display(), winId(),
                    XInternAtom(QX11Info::display(), "_DUI_DECORATOR_WINDOW", False),
                    XA_CARDINAL,
                    32, PropModeReplace,
                    (unsigned char *) &on, 1);
}

#include "duidecoratorwindow.moc"
