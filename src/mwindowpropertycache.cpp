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

#include <stdlib.h>
#include <QX11Info>
#include <QRect>
#include <QDebug>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include "mwindowpropertycache.h"

MWindowPropertyCache::MWindowPropertyCache(Window w, XWindowAttributes *wa)
    : transient_for((Window)-1),
      wm_protocols_valid(false),
      icon_geometry_valid(false),
      has_alpha(false),
      global_alpha(-1),
      is_decorator(false),
      wmhints(0),
      attrs(0),
      meego_layer(-1),
      window_state(-1),
      window(w),
      being_mapped(false)
{
    memset(&req_geom, 0, sizeof(req_geom));

    if (!wa) {
        attrs = (XWindowAttributes*)calloc(1, sizeof(XWindowAttributes));
        if (!XGetWindowAttributes(QX11Info::display(), window, attrs)) {
            qWarning("%s: invalid window 0x%lx", __func__, window);
            free(attrs);
            attrs = 0;
            is_valid = false;
            return;
        }
    } else
        attrs = wa;
    is_valid = true;
    QRect r(attrs->x, attrs->y, attrs->width, attrs->height);
    setRealGeometry(r);
    window_type = MCompAtoms::instance()->windowType(window);

    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(),
                                                        attrs->visual);
    if (format && (format->type == PictTypeDirect && format->direct.alphaMask))
        has_alpha = true;

    is_decorator = MCompAtoms::instance()->isDecorator(window);
}

MWindowPropertyCache::~MWindowPropertyCache()
{
    if (wmhints) {
        XFree(wmhints);
        wmhints = 0;
    }
    if (attrs) {
        free(attrs);
        attrs = 0;
    }
}

Window MWindowPropertyCache::transientFor()
{
    if (transient_for == (Window)-1) {
        XGetTransientForHint(QX11Info::display(), window, &transient_for);
        if (transient_for == window)
            transient_for = 0;
    }
    return transient_for;
}

unsigned int MWindowPropertyCache::meegoStackingLayer()
{
    if (meego_layer < 0)
        meego_layer = MCompAtoms::instance()->cardValueProperty(window,
                                                ATOM(_MEEGO_STACKING_LAYER));
    if (meego_layer > 6) meego_layer = 6;
    return (unsigned)meego_layer;
}

bool MWindowPropertyCache::wantsFocus()
{
    bool val = true;
    const XWMHints &h = getWMHints();
    if ((h.flags & InputHint) && (h.input == False))
        val = false;
    return val;
}

XID MWindowPropertyCache::windowGroup()
{
    XID val = 0;
    const XWMHints &h = getWMHints();
    if (h.flags & WindowGroupHint)
        val = h.window_group;
    return val;
}

const XWMHints &MWindowPropertyCache::getWMHints()
{
    if (!wmhints) {
        wmhints = XGetWMHints(QX11Info::display(), window);
        if (!wmhints) {
            wmhints = XAllocWMHints();
            memset(wmhints, 0, sizeof(XWMHints));
        }
    }
    return *wmhints;
}

bool MWindowPropertyCache::propertyEvent(XPropertyEvent *e)
{
    if (e->atom == ATOM(WM_TRANSIENT_FOR)) {
        transient_for = (Window)-1;
        return true;
    } else if (e->atom == ATOM(WM_HINTS)) {
        if (wmhints) {
            XFree(wmhints);
            wmhints = 0;
        }
        return true;
    } else if (e->atom == ATOM(_NET_WM_WINDOW_TYPE)) {
        qWarning("_NET_WM_WINDOW_TYPE for 0x%lx changed", window);
    } else if (e->atom == ATOM(_NET_WM_ICON_GEOMETRY)) {
        icon_geometry_valid = false;
    } else if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA)) {
        global_alpha = -1;
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        wm_protocols_valid = false;
    } else if (e->atom == ATOM(WM_STATE)) {
        Atom type;
        int format;
        unsigned long length, after;
        uchar *data = 0;
        int r = XGetWindowProperty(QX11Info::display(), window, 
                                   ATOM(WM_STATE), 0, 2,
                                   False, AnyPropertyType, &type, &format,
                                   &length, &after, &data);
        if (r == Success && data && format == 32) {
            unsigned long *wstate = (unsigned long *) data;
            window_state = *wstate;
            XFree((char *)data);
            return true;
        }
    } else if (e->atom == ATOM(_MEEGO_STACKING_LAYER)) {
        meego_layer = MCompAtoms::instance()->cardValueProperty(window,
                                                 ATOM(_MEEGO_STACKING_LAYER));
        return true;
    }
    return false;
}

const QList<Atom>& MWindowPropertyCache::supportedProtocols()
{
    if (!wm_protocols_valid) {
        Atom actual_type;
        int actual_format;
        unsigned long actual_n, left;
        unsigned char *data = NULL;
        int result = XGetWindowProperty(QX11Info::display(), window,
                                        ATOM(WM_PROTOCOLS), 0, 100,
                                        False, XA_ATOM, &actual_type,
                                        &actual_format,
                                        &actual_n, &left, &data);
        if (result == Success && data && actual_type == XA_ATOM) {
            wm_protocols.clear();
            for (unsigned int i = 0; i < actual_n; ++i)
                 wm_protocols.append(((Atom *)data)[i]);
        }
        if (result == Success &&
            (actual_type == XA_ATOM || (actual_type == None && !actual_format)))
            wm_protocols_valid = true;
        if (data) XFree(data);
    }
    return wm_protocols;
}

const QRectF &MWindowPropertyCache::iconGeometry()
{
    if (icon_geometry_valid)
        return icon_geometry;
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), window,
                                    ATOM(_NET_WM_ICON_GEOMETRY), 0L, 4L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned long *geom = (unsigned long *) data;
        QRectF r(geom[0], geom[1], geom[2], geom[3]);
        XFree((void *) data);
        icon_geometry = r;
    } else
        icon_geometry = QRectF(); // empty
    icon_geometry_valid = true;
    return icon_geometry;
}

#define OPAQUE 0xffffffff
int MWindowPropertyCache::globalAlpha()
{
    if (global_alpha != -1)
        return global_alpha;
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data = 0;
    int result = XGetWindowProperty(QX11Info::display(), window,
                                    ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                                    0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned int i;
        memcpy(&i, data, sizeof(unsigned int));
        XFree((void *) data);
        double opacity = i * 1.0 / OPAQUE;
        global_alpha = opacity * 255;
    } else
        global_alpha = 255;
    return global_alpha;
}
