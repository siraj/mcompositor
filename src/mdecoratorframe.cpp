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
    return client;
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

void MDecoratorFrame::setManagedWindow(Qt::HANDLE window)
{
    Display *dpy = QX11Info::display();

    if (client == window)
        return;
    client = window;

    if (!decorator_item)
        return;

    // TODO: Make this dynamic based on decorator dimensions.
    MCompositeWindow *w = MCompositeWindow::compositeWindow(window);
    if (w && w->needDecoration()) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, decorator_window, &a)) {
            qWarning("%s: invalid window 0x%lx", __func__, decorator_window);
            return;
        }
        QRegion d = QRegion(a.x, a.y, a.width, a.height);
        QRect r = (d - QRegion(a.x, a.y, a.width, 65)).boundingRect();
        XMoveResizeWindow(dpy, window, r.x(), r.y(), r.width(), r.height());
    }

    qulonglong winid = client;
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetManagedWinId", winid);
    if (w)
        connect(w, SIGNAL(destroyed()), SLOT(destroyClient()));
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
    if (decorator_item)
        decorator_item->setVisible(visible);
}

void MDecoratorFrame::activate()
{
    remote_decorator->invoke("MAbstractDecorator", "RemoteActivateWindow");
}
