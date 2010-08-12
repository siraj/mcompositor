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
#include "mcompositemanager.h"

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
      client(0),
      decorator_window(0),
      decorator_item(0),
      no_resize(false)
{    
    MCompositeManager *mgr = (MCompositeManager *) qApp;
    connect(mgr, SIGNAL(decoratorRectChanged(const QRect&)), this,
            SLOT(setDecoratorAvailableRect(const QRect&)));
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

void MDecoratorFrame::updateManagedWindowGeometry()
{
    if (client && client->needDecoration()) {
#if 0
        Display *dpy = QX11Info::display();
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, decorator_window, &a)) {
            qWarning("%s: invalid window 0x%lx", __func__, decorator_window);
            return;
        }
#endif
        setDecoratorAvailableRect(decorator_rect);
    }
}

void MDecoratorFrame::setManagedWindow(MCompositeWindow *cw,
                                       bool no_resize)
{    
    this->no_resize = no_resize;

    if (client == cw)
        return;
    disconnect(this, SLOT(destroyClient()));
    client = cw;

    qulonglong winid = client ? client->window() : 0;
    if (decorator_window) {
        long val = winid;
        Atom a = XInternAtom(QX11Info::display(),
                             "_MDECORATOR_MANAGED_WINDOW", False);
        XChangeProperty(QX11Info::display(), decorator_window, a, XA_WINDOW,
                        32, PropModeReplace, (unsigned char *)&val, 1);
    }
    if (!decorator_item)
        return;
    
    if(cw)
        remote_decorator->invoke("MAbstractDecorator",
                                 "RemoteSetClientGeometry",
                                 cw->propertyCache()->requestedGeometry());
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", false);
    /* FIXME: replaced with a window property due to reliability problems
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetManagedWinId", winid);
                             */
    
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
    if (client == sender())
        client = 0;
}

void MDecoratorFrame::visualizeDecorator(bool visible)
{
    if (sender() == decorator_item)
        return;

    if (decorator_item)
        decorator_item->setVisible(visible);
}

void MDecoratorFrame::setDecoratorAvailableRect(const QRect& decorect)
{    
    if (!client || no_resize || !decorator_item
        || !decorator_item->propertyCache())
        return;
    
    Display* dpy = QX11Info::display();
    
    int actual_decor_ypos = decorator_item->propertyCache()->realGeometry().y();

    decorator_rect = decorect;    

    // if window is not same width as screen, stretch it.
    QRect appgeometry = client->propertyCache()->requestedGeometry();
    if(appgeometry.width() < decorect.width())
        appgeometry = QApplication::desktop()->screenGeometry();
    
    // region of decorator + statusbar window. remove this once we removed the statubar
    QRect actual_decorect = decorect;
    actual_decorect.setHeight(decorect.height() + actual_decor_ypos);
    QRect r = (QRegion(appgeometry) - actual_decorect).boundingRect();
    
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    int excess = r.y() + r.height() - yres;
    if (excess > 0)
        r.setHeight(r.height() - excess);
    excess = r.x() + r.width() - xres;
    if (excess > 0)
        r.setWidth(r.width() - excess);

    XMoveResizeWindow(dpy, client->window(), r.x(), r.y(), r.width(), r.height());
}

void MDecoratorFrame::setAutoRotation(bool mode)
{
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetAutoRotation", mode);
}

void MDecoratorFrame::setOnlyStatusbar(bool mode)
{
    if (decorator_window) {
        long val = mode;
        Atom a = XInternAtom(QX11Info::display(),
                             "_MDECORATOR_ONLY_STATUSBAR", False);
        XChangeProperty(QX11Info::display(), decorator_window, a, XA_CARDINAL,
                        32, PropModeReplace, (unsigned char *)&val, 1);
    }
    /* FIXME: replaced with a window property due to reliability problems
    remote_decorator->invoke("MAbstractDecorator",
                             "RemoteSetOnlyStatusbar", mode);
                             */
}
