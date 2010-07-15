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

#define MAX_TYPES 10

xcb_connection_t *MWindowPropertyCache::xcb_conn;

MWindowPropertyCache::MWindowPropertyCache(Window w,
                        xcb_get_window_attributes_reply_t *wa,
                        xcb_get_geometry_reply_t *geom)
    : transient_for((Window)-1),
      wm_protocols_valid(false),
      icon_geometry_valid(false),
      decor_buttons_valid(false),
      has_alpha(-1),
      global_alpha(-1),
      is_decorator(-1),
      wmhints(0),
      attrs(0),
      meego_layer(-1),
      window_state(-1),
      window_type(MCompAtoms::INVALID),
      window(w),
      being_mapped(false),
      xcb_real_geom(0)
{
    memset(&req_geom, 0, sizeof(req_geom));
    memset(&home_button_geom, 0, sizeof(home_button_geom));
    memset(&close_button_geom, 0, sizeof(close_button_geom));

    if (!wa) {
        attrs = xcb_get_window_attributes_reply(xcb_conn,
                        xcb_get_window_attributes(xcb_conn, window), 0);
        if (!attrs) {
            qWarning("%s: invalid window 0x%lx", __func__, window);
            is_valid = false;
            return;
        }
    } else
        attrs = wa;
    if (!geom)
        xcb_real_geom_cookie = xcb_get_geometry(xcb_conn, window);
    else {
        xcb_real_geom = geom;
        real_geom = QRect(xcb_real_geom->x, xcb_real_geom->y,
                          xcb_real_geom->width, xcb_real_geom->height);
    }
    is_valid = true;

    xcb_is_decorator_cookie = xcb_get_property(xcb_conn, 0, window,
                                        ATOM(_MEEGOTOUCH_DECORATOR_WINDOW),
                                        XCB_ATOM_CARDINAL, 0, 1);
    xcb_transient_for_cookie = xcb_get_property(xcb_conn, 0, window,
                                                XCB_ATOM_WM_TRANSIENT_FOR,
                                                XCB_ATOM_WINDOW, 0, 1);
    xcb_meego_layer_cookie = xcb_get_property(xcb_conn, 0, window,
                                              ATOM(_MEEGO_STACKING_LAYER),
                                              XCB_ATOM_CARDINAL, 0, 1);
    xcb_window_type_cookie = xcb_get_property(xcb_conn, 0, window,
                                              ATOM(_NET_WM_WINDOW_TYPE),
                                              XCB_ATOM_ATOM, 0, MAX_TYPES);
    xcb_pict_formats_cookie = xcb_render_query_pict_formats(xcb_conn);
    xcb_decor_buttons_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS),
                                       XCB_ATOM_CARDINAL, 0, 8);
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
    if (!xcb_real_geom)
        xcb_real_geom = xcb_get_geometry_reply(xcb_conn,
                                               xcb_real_geom_cookie, 0);
    if (xcb_real_geom) {
        free(xcb_real_geom);
        xcb_real_geom = 0;
    }
    xcb_get_property_reply_t *r;
    if (transient_for == (Window)-1) {
        r = xcb_get_property_reply(xcb_conn, xcb_transient_for_cookie, 0);
        if (r) free(r);
    }
    if (meego_layer < 0) {
        r = xcb_get_property_reply(xcb_conn, xcb_meego_layer_cookie, 0);
        if (r) free(r);
    }
    if (is_decorator < 0) {
        r = xcb_get_property_reply(xcb_conn, xcb_is_decorator_cookie, 0);
        if (r) free(r);
    }
    if (window_type == MCompAtoms::INVALID) {
        r = xcb_get_property_reply(xcb_conn, xcb_window_type_cookie, 0);
        if (r) free(r);
    }
    if (has_alpha < 0) {
        xcb_render_query_pict_formats_reply_t *pfr;
        pfr = xcb_render_query_pict_formats_reply(xcb_conn,
                                                  xcb_pict_formats_cookie, 0);
        if (pfr) free(pfr);
    }
    if (!decor_buttons_valid) {
        r = xcb_get_property_reply(xcb_conn, xcb_decor_buttons_cookie, 0);
        if (r) free(r);
    }
}

bool MWindowPropertyCache::hasAlpha()
{
    if (has_alpha >= 0)
        return has_alpha ? true : false;

    // the following code is replacing a XRenderFindVisualFormat() call...
    xcb_render_query_pict_formats_reply_t *pict_formats_reply;
    pict_formats_reply = xcb_render_query_pict_formats_reply(xcb_conn,
                                xcb_pict_formats_cookie, 0);
    if (!pict_formats_reply) {
        qWarning("%s: querying alpha for 0x%lx has failed", __func__, window);
        has_alpha = 0;
        return false;
    }

    xcb_render_pictformat_t format = 0;
    xcb_render_pictscreen_iterator_t scr_i;
    scr_i = xcb_render_query_pict_formats_screens_iterator(pict_formats_reply);
    for (; scr_i.rem; xcb_render_pictscreen_next(&scr_i)) {
        xcb_render_pictdepth_iterator_t depth_i;
        depth_i = xcb_render_pictscreen_depths_iterator(scr_i.data);
        for (; depth_i.rem; xcb_render_pictdepth_next(&depth_i)) {
            xcb_render_pictvisual_iterator_t visual_i;
            visual_i = xcb_render_pictdepth_visuals_iterator(depth_i.data);
            for (; visual_i.rem; xcb_render_pictvisual_next(&visual_i)) {
                if (visual_i.data->visual == attrs->visual) {
                    format = visual_i.data->format;
                    break;
                }
            }
        }
    }
    // now we have the format, next find details for it
    xcb_render_pictforminfo_iterator_t pictform_i;
    pictform_i = xcb_render_query_pict_formats_formats_iterator(
                                                      pict_formats_reply);
    for (; pictform_i.rem; xcb_render_pictforminfo_next(&pictform_i)) {
        if (pictform_i.data->id == format) {
            if (pictform_i.data->direct.alpha_mask)
                has_alpha = 1;
            else
                has_alpha = 0;
            break;
        }
    }
    return has_alpha ? true : false;
}

Window MWindowPropertyCache::transientFor()
{
    if (transient_for == (Window)-1) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_transient_for_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(Window))
                transient_for = *((Window*)xcb_get_property_value(r));
            else
                transient_for = 0;
            free(r);
            if (transient_for == window)
                transient_for = 0;
        }
    }
    return transient_for;
}

bool MWindowPropertyCache::isDecorator()
{
    if (is_decorator < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_is_decorator_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(int))
                is_decorator = *((int*)xcb_get_property_value(r));
            else
                is_decorator = 0;
            free(r);
        }
    }
    return is_decorator == 1;
}

unsigned int MWindowPropertyCache::meegoStackingLayer()
{
    if (meego_layer < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_meego_layer_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(int))
                meego_layer = *((int*)xcb_get_property_value(r));
            else
                meego_layer = 0;
            free(r);
        }
    }
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
        if (transient_for == (Window)-1)
            // collect the old reply
            transientFor();
        transient_for = (Window)-1;
        xcb_transient_for_cookie = xcb_get_property(xcb_conn, 0, window,
                                                    XCB_ATOM_WM_TRANSIENT_FOR,
                                                    XCB_ATOM_WINDOW, 0, 1);
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
    } else if (e->atom == ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS)) {
        if (!decor_buttons_valid)
            // collect the old reply
            buttonGeometryHelper();
        decor_buttons_valid = false;
        xcb_decor_buttons_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS),
                                       XCB_ATOM_CARDINAL, 0, 8);
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
        if (meego_layer < 0)
            // collect the old reply
            meegoStackingLayer();
        meego_layer = -1;
        xcb_meego_layer_cookie = xcb_get_property(xcb_conn, 0, window,
                                                  ATOM(_MEEGO_STACKING_LAYER),
                                                  XCB_ATOM_CARDINAL, 0, 1);
        return true;
    }
    return false;
}

void MWindowPropertyCache::buttonGeometryHelper()
{
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_decor_buttons_cookie, 0);
    if (!r) {
        decor_buttons_valid = true;
        return;
    }
    int len = xcb_get_property_value_length(r);
    unsigned long x, y, w, h;
    if (len == 0)
        goto go_out;
    else if (len != 8 * sizeof(unsigned long)) {
        qWarning("%s: _MEEGOTOUCH_DECORATOR_BUTTONS size is %d", __func__, len);
        goto go_out;
    }
    x = ((unsigned long*)xcb_get_property_value(r))[0];
    y = ((unsigned long*)xcb_get_property_value(r))[1];
    w = ((unsigned long*)xcb_get_property_value(r))[2];
    h = ((unsigned long*)xcb_get_property_value(r))[3];
    home_button_geom.setRect(x, y, w, h);
    x = ((unsigned long*)xcb_get_property_value(r))[4];
    y = ((unsigned long*)xcb_get_property_value(r))[5];
    w = ((unsigned long*)xcb_get_property_value(r))[6];
    h = ((unsigned long*)xcb_get_property_value(r))[7];
    close_button_geom.setRect(x, y, w, h);
go_out:
    free(r);
    decor_buttons_valid = true;
}

const QRect &MWindowPropertyCache::homeButtonGeometry()
{
    if (decor_buttons_valid)
        return home_button_geom;
    buttonGeometryHelper();
    return home_button_geom;
}

const QRect &MWindowPropertyCache::closeButtonGeometry()
{
    if (decor_buttons_valid)
        return close_button_geom;
    buttonGeometryHelper();
    return close_button_geom;
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

MCompAtoms::Type MWindowPropertyCache::windowType()
{
    if (window_type != MCompAtoms::INVALID)
        return window_type;

    QVector<Atom> a(MAX_TYPES);
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_window_type_cookie, 0);
    if (r) {
        int len = xcb_get_property_value_length(r);
        if (len >= (int)sizeof(Atom)) {
            memcpy(a.data(), xcb_get_property_value(r), len);
            a.resize(len / sizeof(Atom));
        } else {
            free(r);
            window_type = MCompAtoms::NORMAL;
            return window_type;
        }
        free(r);
    }

    if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        window_type = MCompAtoms::DESKTOP;
    else if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_NORMAL))
        window_type = MCompAtoms::NORMAL;
    else if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
        if (a.indexOf(ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)) != -1)
            window_type = MCompAtoms::NO_DECOR_DIALOG;
        else
            window_type = MCompAtoms::DIALOG;
    }
    else if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
        window_type = MCompAtoms::DOCK;
    else if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_INPUT))
        window_type = MCompAtoms::INPUT;
    else if (a[0] == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
        window_type = MCompAtoms::NOTIFICATION;
    else if (a[0] == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) ||
             a[0] == ATOM(_NET_WM_WINDOW_TYPE_MENU))
        window_type = MCompAtoms::FRAMELESS;
    else if (transientFor())
        window_type = MCompAtoms::UNKNOWN;
    else // fdo spec suggests unknown non-transients must be normal
        window_type = MCompAtoms::NORMAL;
    return window_type;
}

