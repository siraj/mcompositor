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

#include "duitexturepixmapitem.h"

#include <QX11Info>
#include <QRect>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

#include "duitexturepixmapitem_p.h"

bool DuiTexturePixmapPrivate::inverted_texture = true;

void DuiTexturePixmapPrivate::init()
{
    XWindowAttributes a;
    XGetWindowAttributes(QX11Info::display(), item->window(), &a);

    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(), a.visual);
    has_alpha = (format->type == PictTypeDirect && format->direct.alphaMask);

    if (a.map_state != IsViewable)
        viewable = false;

    override_redirect = a.override_redirect ? true : false;
    resize(a.width, a.height);
    item->setPos(a.x, a.y);
}

DuiTexturePixmapPrivate::DuiTexturePixmapPrivate(Qt::HANDLE window, QGLWidget *w, DuiTexturePixmapItem *p)
    : ctx(0),
      glwidget(w),
      windowp(0),
      custom_tfp(false),
      has_alpha(false),
      direct_fb_render(false),
      override_redirect(false),
      viewable(true),
      angle(0),
      item(p)
{
    damage_object = XDamageCreate(QX11Info::display(), window, XDamageReportNonEmpty);
    init();
}

DuiTexturePixmapPrivate::~DuiTexturePixmapPrivate()
{
    XDamageDestroy(QX11Info::display(), damage_object);
}

void DuiTexturePixmapPrivate::saveBackingStore(bool renew)
{
    XWindowAttributes a;
    XGetWindowAttributes(QX11Info::display(), item->window(), &a);
    if (a.map_state != IsViewable)
        return;

    if (renew && windowp)
        XFreePixmap(QX11Info::display(), windowp);
    Pixmap px = XCompositeNameWindowPixmap(QX11Info::display(), item->window());
    windowp = px;
    if (renew)
        item->rebindPixmap();

    XSync(QX11Info::display(), FALSE);
}

void DuiTexturePixmapPrivate::windowRaised()
{
    XRaiseWindow(QX11Info::display(), item->window());
}

void DuiTexturePixmapPrivate::resize(int w, int h)
{
    if (!brect.isEmpty() && !item->isDirectRendered() && (brect.width() != w || brect.height() != h)) {
        item->saveBackingStore(true);
        item->updateWindowPixmap();
    }
    brect.setWidth(w);
    brect.setHeight(h);
}

bool DuiTexturePixmapPrivate::hasAlpha() const
{
    return has_alpha;
}

bool DuiTexturePixmapPrivate::isOverrideRedirect() const
{
    return override_redirect;
}
