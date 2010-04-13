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

#include "duidecoratorframe.h"
#include "duicompositewindow.h"
#include "duitexturepixmapitem.h"

#include <duirmiclient.h>
#include <QX11Info>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

DuiDecoratorFrame *DuiDecoratorFrame::d = 0;

DuiDecoratorFrame *DuiDecoratorFrame::instance()
{
    if (!d)
        d = new DuiDecoratorFrame();
    return d;
}

DuiDecoratorFrame::DuiDecoratorFrame(QObject *p)
    : QObject(p),
      decorator_window(0),
      decorator_item(0)
{
    /*!
     * Excute decorator process here.
     */

    remote_decorator = new DuiRmiClient(".duiabstractdecorator", this);
}

Qt::HANDLE DuiDecoratorFrame::managedWindow() const
{
    return client ? client->window() : 0;
}

Qt::HANDLE DuiDecoratorFrame::winId() const
{
    return decorator_window;
}

void DuiDecoratorFrame::lower()
{
    if (decorator_item)
        decorator_item->setVisible(false);
}

void DuiDecoratorFrame::raise()
{
    if (decorator_item)
        decorator_item->setVisible(true);
}

void DuiDecoratorFrame::updateManagedWindowGeometry(int top_offset)
{
    if (client && client->needDecoration()) {
        Display *dpy = QX11Info::display();
#if 0
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, decorator_window, &a)) {
            qWarning("%s: invalid window 0x%lx", __func__, decorator_window);
            return;
        }
#endif
        // TODO: Make this dynamic based on decorator dimensions.
        int deco_h;
        if (decorator_window)
            deco_h = 65;
        else
            deco_h = 0;
        int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
        int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
        QRect wq = client->requestedGeometry();
        QRect r = QRect(wq.x(), deco_h + top_offset, wq.width(), wq.height());
        int excess = r.y() + r.height() - yres;
        if (excess > 0)
            r.setHeight(r.height() - excess);
        excess = r.x() + r.width() - xres;
        if (excess > 0)
            r.setWidth(r.width() - excess);
        XMoveResizeWindow(dpy, client->window(), r.x(), r.y(),
                          r.width(), r.height());
    }
}

void DuiDecoratorFrame::setManagedWindow(DuiCompositeWindow *cw,
                                         int top_offset)
{
    if (client == cw)
        return;
    client = cw;

    if (!decorator_item)
        return;

    if (cw)
        updateManagedWindowGeometry(top_offset);

    qulonglong winid = client ? client->window() : 0;
    remote_decorator->invoke("DuiAbstractDecorator",
                             "RemoteSetManagedWinId", winid);
    if (cw)
        connect(cw, SIGNAL(destroyed()), SLOT(destroyClient()));
}

void DuiDecoratorFrame::setDecoratorWindow(Qt::HANDLE window)
{
    decorator_window = window;
    XMapWindow(QX11Info::display(), window);
}

void DuiDecoratorFrame::setDecoratorItem(DuiCompositeWindow *window)
{
    decorator_item = window;
    decorator_item->setIsDecorator(true);
    connect(decorator_item, SIGNAL(destroyed()), SLOT(destroyDecorator()));

    DuiTexturePixmapItem *item = (DuiTexturePixmapItem *) window;
    if (!decorator_window)
        setDecoratorWindow(item->window());
}

DuiCompositeWindow *DuiDecoratorFrame::decoratorItem() const
{
    return decorator_item;
}

void DuiDecoratorFrame::destroyDecorator()
{
    decorator_item = 0;
    decorator_window = 0;
}

void DuiDecoratorFrame::destroyClient()
{
    client = 0;
}

void DuiDecoratorFrame::visualizeDecorator(bool visible)
{
    if (decorator_item)
        decorator_item->setVisible(visible);
}

void DuiDecoratorFrame::activate()
{
    remote_decorator->invoke("DuiAbstractDecorator", "RemoteActivateWindow");
}
