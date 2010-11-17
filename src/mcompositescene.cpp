/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
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

#include "mcompositewindow.h"
#include "mcompositescene.h"
#include "mcompositewindowgroup.h"

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

MCompositeScene::MCompositeScene(QObject *p)
    : QGraphicsScene(p)
{
    setBackgroundBrush(Qt::NoBrush);
    setForegroundBrush(Qt::NoBrush);
    setSceneRect(QRect(0, 0,
                       QApplication::desktop()->width(),
                       QApplication::desktop()->height()));
    installEventFilter(this);
}

void MCompositeScene::prepareRoot()
{
    Display *dpy = QX11Info::display();
    Window root =  QX11Info::appRootWindow();

    XSetWindowAttributes sattr;
    sattr.event_mask =  SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask;

    // All newly mapped windows should be redirected to avoid double Expose
    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);

    XChangeWindowAttributes(dpy, root, CWEventMask, &sattr);
    XSelectInput(dpy, root, SubstructureNotifyMask | SubstructureRedirectMask
                            | StructureNotifyMask | PropertyChangeMask
                            | FocusChangeMask);
    XSetErrorHandler(error_handler);
}

void MCompositeScene::drawItems(QPainter *painter, int numItems, QGraphicsItem *items[], const QStyleOptionGraphicsItem options[], QWidget *widget)
{
    QRegion visible(sceneRect().toRect());
    QVector<int> to_paint(10);
    int size = 0;
    bool desktop_painted = false;
    // visibility is determined from top to bottom
    for (int i = numItems - 1; i >= 0; --i) {
        MCompositeWindow *cw = (MCompositeWindow *) items[i];

        if (cw->type() != MCompositeWindowGroup::Type) {
            if (!cw->propertyCache())  // this window is dead
                continue;
            if (cw->hasTransitioningWindow() && cw->propertyCache()->isDecorator())
                // if we have a transition animation, don't draw the decorator
                // lest we can have it drawn with the transition (especially
                // when desktop window is not yet shown, NB#192454)
                continue;
            if (cw->isDirectRendered() || !cw->isVisible()
                || !(cw->propertyCache()->isMapped() || cw->isWindowTransitioning())
                || cw->propertyCache()->isInputOnly())
                    continue;            
            if (visible.isEmpty())
                // nothing below is visible anymore
                break;
        }

        // Ensure that intersects() still work, otherwise, painting a window
        // is skipped when another window above it is scaled or moved to an 
        // area that exposed the lower window and causes an ugly flicker.
        // r reflects the applied transformation and position of the window
        QRegion r = cw->sceneTransform().map(cw->propertyCache()->shapeRegion());
        
        // transitioning window can be smaller than shapeRegion(), so paint
        // all transitioning windows
        if (cw->isWindowTransitioning() || visible.intersects(r)
            || cw->type() == MCompositeWindowGroup::Type) {
            if (size >= 9)
                to_paint.resize(to_paint.size()+1);
            to_paint[size++] = i;
            if (cw->propertyCache()->windowTypeAtom()
                                         == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
                desktop_painted = true;
        }

        // subtract opaque regions
        if (!cw->isWindowTransitioning()
            && !cw->propertyCache()->hasAlpha() 
            && cw->opacity() == 1.0
            && !cw->windowGroup()) // window is renderered off-screen)
            visible -= r;
    }
    if (size > 0) {
        // paint from bottom to top so that blending works
        for (int i = size - 1; i >= 0; --i) {
            int item_i = to_paint[i];
            painter->save();
            if (!desktop_painted) {
                // clear rubbish from the root window during startup when
                // desktop window does not exist and we show zoom animations
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                desktop_painted = true;
                if (((MCompositeWindow*)items[item_i])->
                                             propertyCache()->isDecorator()) {
                    // don't paint decorator on top of plain black background
                    // (see NB#182860, NB#192454)
                    painter->restore();
                    continue;
                }
            }
            // TODO: paint only the intersected region (glScissor?)
            painter->setMatrix(items[item_i]->sceneMatrix(), true);
            items[item_i]->paint(painter, &options[item_i], widget);
            painter->restore();
        }
    }
}
