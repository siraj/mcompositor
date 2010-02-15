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

#include <QX11Info>
#include <QKeySequence>
#include <QKeyEvent>
#include <QEvent>
#include <QTimer>
#include <QApplication>
#include <QDesktopWidget>

#include "duicompositewindow.h"
#include "duicompositescene.h"

#include <X11/extensions/Xfixes.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif
#include <X11/extensions/Xcomposite.h>

static int error_handler(Display * , XErrorEvent *error)
{
    if (error->resourceid == QX11Info::appRootWindow() && error->error_code == BadAccess) {
        qCritical("Another window manager is running.");
        ::exit(0);
    }
    if (error->error_code == BadMatch)
        qDebug() << "Bad match error " << error->resourceid;

    return 0;
}

DuiCompositeScene::DuiCompositeScene(QObject *p)
    : QGraphicsScene(p)
{
    setBackgroundBrush(Qt::NoBrush);
    setForegroundBrush(Qt::NoBrush);
    setSceneRect(QRect(0, 0,
                       QApplication::desktop()->width(),
                       QApplication::desktop()->height()));
    installEventFilter(this);
}

void DuiCompositeScene::prepareRoot()
{
    Display *dpy = QX11Info::display();
    Window root =  QX11Info::appRootWindow();

    XSetWindowAttributes sattr;
    sattr.event_mask =  SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask;

    //XCompositeRedirectSubwindows (dpy, root, CompositeRedirectAutomatic);

    XChangeWindowAttributes(dpy, root, CWEventMask, &sattr);
    XSelectInput(dpy, root, SubstructureNotifyMask | SubstructureRedirectMask | StructureNotifyMask | PropertyChangeMask);
    XSetErrorHandler(error_handler);
}


void DuiCompositeScene::setupOverlay(Window window, const QRect &geom,
                                     bool restoreInput)
{
    Display *dpy = QX11Info::display();
    XRectangle rect;

    rect.x      = geom.x();
    rect.y      = geom.y();
    rect.width  = geom.width();
    rect.height = geom.height();
    XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);

    XFixesSetWindowShapeRegion(dpy, window, ShapeBounding, 0, 0, 0);
    if (!restoreInput)
        XFixesSetWindowShapeRegion(dpy, window, ShapeInput, 0, 0, region);
    else
        XFixesSetWindowShapeRegion(dpy, window, ShapeInput, 0, 0, 0);

    XFixesDestroyRegion(dpy, region);
}

void DuiCompositeScene::drawItems(QPainter *painter, int numItems, QGraphicsItem *items[], const QStyleOptionGraphicsItem options[], QWidget *widget)
{
    for (int i = 0; i < numItems; ++i) {
        DuiCompositeWindow *window = (DuiCompositeWindow *) items[i];
        if (window->windowVisible() && !window->isIconified()) {
            painter->save();
            painter->setMatrix(items[i]->sceneMatrix(), true);
            items[i]->paint(painter, &options[i], widget);
            painter->restore();
        }
    }
}
