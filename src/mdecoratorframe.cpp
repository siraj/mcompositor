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

#include "mdecoratorframe.h"
#include "mcompositewindow.h"
#include "mtexturepixmapitem.h"

#include <mrmiclient.h>
#include <QX11Info>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

MDecoratorFrame *MDecoratorFrame::d = 0;

MDecoratorFrame *MDecoratorFrame::instance()
{
    if (!d)
        d = new MDecoratorFrame();
    return d;
}

MDecoratorFrame::MDecoratorFrame(QObject *p)
    : QObject(p),
      decorator_window(0),
      decorator_item(0)
{
    /*!
     * Excute decorator process here.
     */

    remote_decorator = new MRmiClient(".mabstractdecorator", this);
}

Qt::HANDLE MDecoratorFrame::managedWindow() const
{
    return client ? client->window() : 0;
}

Qt::HANDLE MDecoratorFrame::winId() const
{
    return decorator_window;
}

void MDecoratorFrame::lower()
{
    if (decorator_item)
        decorator_item->setVisible(false);
}

void MDecoratorFrame::raise()
{
    if (decorator_item)
        decorator_item->setVisible(true);
}

void MDecoratorFrame::updateManagedWindowGeometry(int top_offset)
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

void MDecoratorFrame::setManagedWindow(MCompositeWindow *cw,
                                       int top_offset, bool no_resize)
{
    if (client == cw)
        return;
    client = cw;

    if (!decorator_item)
        return;

    if (cw && !no_resize)
        updateManagedWindowGeometry(top_offset);

    qulonglong winid = client ? client->window() : 0;
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetManagedWinId", winid);
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", false);
    if (cw)
        connect(cw, SIGNAL(destroyed()), SLOT(destroyClient()));
}

void MDecoratorFrame::setDecoratorWindow(Qt::HANDLE window)
{
    decorator_window = window;
    XMapWindow(QX11Info::display(), window);
}

void MDecoratorFrame::setDecoratorItem(MCompositeWindow *window)
{
    decorator_item = window;
    decorator_item->setIsDecorator(true);
    connect(decorator_item, SIGNAL(destroyed()), SLOT(destroyDecorator()));

    MTexturePixmapItem *item = (MTexturePixmapItem *) window;
    if (!decorator_window)
        setDecoratorWindow(item->window());
}

MCompositeWindow *MDecoratorFrame::decoratorItem() const
{
    return decorator_item;
}

void MDecoratorFrame::destroyDecorator()
{
    decorator_item = 0;
    decorator_window = 0;
}

void MDecoratorFrame::destroyClient()
{
    client = 0;
}

void MDecoratorFrame::visualizeDecorator(bool visible)
{
    if (sender() == decorator_item)
        return;

    if (decorator_item)
        decorator_item->setVisible(visible);
}

void MDecoratorFrame::activate()
{
    remote_decorator->invoke("MAbstractDecorator", "RemoteActivateWindow");
}

void MDecoratorFrame::setAutoRotation(bool mode)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", mode);
}
