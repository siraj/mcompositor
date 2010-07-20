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

#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <X11/Xmd.h>
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
      shape_rects_valid(false),
      real_geom_valid(false),
      wm_state_query(true),
      has_alpha(-1),
      global_alpha(-1),
      is_decorator(-1),
      wmhints(0),
      attrs(0),
      meego_layer(-1),
      window_state(-1),
      window_type(MCompAtoms::INVALID),
      window(w),
      parent_window(RootWindow(QX11Info::display(), 0)),
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
    xcb_wm_protocols_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(WM_PROTOCOLS),
                                               XCB_ATOM_ATOM, 0, 100);
    xcb_wm_state_cookie = xcb_get_property(xcb_conn, 0, window,
                                  ATOM(WM_STATE), ATOM(WM_STATE), 0, 1);
    xcb_wm_hints_cookie = xcb_get_property(xcb_conn, 0, window,
                                  XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 0, 10);
    xcb_icon_geom_cookie = xcb_get_property(xcb_conn, 0, window,
                                            ATOM(_NET_WM_ICON_GEOMETRY),
                                            XCB_ATOM_CARDINAL, 0, 4);
    xcb_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                                               XCB_ATOM_CARDINAL, 0, 1);
    xcb_shape_rects_cookie = xcb_shape_get_rectangles(xcb_conn, window,
                                                      ShapeBounding);
}

MWindowPropertyCache::~MWindowPropertyCache()
{
    if (!is_valid)
        // no pending XCB requests
        return;
    if (!wmhints)
        getWMHints();
    XFree(wmhints);
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
    if (!wm_protocols_valid) {
        r = xcb_get_property_reply(xcb_conn, xcb_wm_protocols_cookie, 0);
        if (r) free(r);
    }
    if (wm_state_query)
        windowState();
    if (!icon_geometry_valid)
        iconGeometry();
    if (global_alpha < 0)
        globalAlpha();
    if (!shape_rects_valid)
        shapeRegion();
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

const QRegion MWindowPropertyCache::shapeRegion()
{
    if (shape_rects_valid) {
        if (shape_region.isEmpty())
            return QRegion(realGeometry());
        else
            return shape_region;
    }
    xcb_shape_get_rectangles_reply_t *r;
    r = xcb_shape_get_rectangles_reply(xcb_conn, xcb_shape_rects_cookie, 0);
    if (!r) {
        shape_rects_valid = true;
        return QRegion(realGeometry());
    }
    xcb_rectangle_iterator_t i;
    i = xcb_shape_get_rectangles_rectangles_iterator(r);
    shape_region = QRegion(0, 0, 0, 0);
    for (; i.rem; xcb_rectangle_next(&i))
        shape_region += QRect(i.data->x, i.data->y, i.data->width,
                              i.data->height);
    free(r);
    shape_rects_valid = true;
    return shape_region;
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
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                is_decorator = *((CARD32*)xcb_get_property_value(r));
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
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                meego_layer = *((CARD32*)xcb_get_property_value(r));
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
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_wm_hints_cookie, 0);
        int len;
        if (!r)
            goto empty_value;
        len = xcb_get_property_value_length(r);
        if ((unsigned)len < sizeof(XWMHints)) {
            free(r);
            goto empty_value;
        }
        wmhints = XAllocWMHints();
        memcpy(wmhints, xcb_get_property_value(r), sizeof(XWMHints));
        free(r);
    }
    return *wmhints;
empty_value:
    wmhints = XAllocWMHints();
    memset(wmhints, 0, sizeof(XWMHints));
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
        if (!wmhints)
            // collect the old reply
            getWMHints();
        XFree(wmhints);
        wmhints = 0;
        xcb_wm_hints_cookie = xcb_get_property(xcb_conn, 0, window,
                                  XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 0, 10);
        return true;
    } else if (e->atom == ATOM(_NET_WM_WINDOW_TYPE)) {
        qWarning("_NET_WM_WINDOW_TYPE for 0x%lx changed", window);
    } else if (e->atom == ATOM(_NET_WM_ICON_GEOMETRY)) {
        if (!icon_geometry_valid)
            // collect the old reply
            iconGeometry();
        icon_geometry_valid = false;
        xcb_icon_geom_cookie = xcb_get_property(xcb_conn, 0, window,
                                            ATOM(_NET_WM_ICON_GEOMETRY),
                                            XCB_ATOM_CARDINAL, 0, 4);
    } else if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA)) {
        if (global_alpha < 0)
            // collect the old reply
            globalAlpha();
        global_alpha = -1;
        xcb_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                                       XCB_ATOM_CARDINAL, 0, 1);
    } else if (e->atom == ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS)) {
        if (!decor_buttons_valid)
            // collect the old reply
            buttonGeometryHelper();
        decor_buttons_valid = false;
        xcb_decor_buttons_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS),
                                       XCB_ATOM_CARDINAL, 0, 8);
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        if (!wm_protocols_valid)
            // collect the old reply
            supportedProtocols();
        wm_protocols_valid = false;
        xcb_wm_protocols_cookie = xcb_get_property(xcb_conn, 0, window,
                                                   ATOM(WM_PROTOCOLS),
                                                   XCB_ATOM_ATOM, 0, 100);
    } else if (e->atom == ATOM(WM_STATE)) {
        if (wm_state_query)
            // collect the old reply
            windowState();
        xcb_wm_state_cookie = xcb_get_property(xcb_conn, 0, window,
                                  ATOM(WM_STATE), ATOM(WM_STATE), 0, 1);
        wm_state_query = true;
        return true;
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

int MWindowPropertyCache::windowState()
{
    if (wm_state_query) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_wm_state_cookie, 0);
        if (r && (unsigned)xcb_get_property_value_length(r) >= sizeof(CARD32))
            window_state = ((CARD32*)xcb_get_property_value(r))[0];
        else {
            window_state = NormalState;
        }
        if (r) free(r);
        wm_state_query = false;
    }
    return window_state;
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
    else if (len != 8 * sizeof(CARD32)) {
        qWarning("%s: _MEEGOTOUCH_DECORATOR_BUTTONS size is %d", __func__, len);
        goto go_out;
    }
    x = ((CARD32*)xcb_get_property_value(r))[0];
    y = ((CARD32*)xcb_get_property_value(r))[1];
    w = ((CARD32*)xcb_get_property_value(r))[2];
    h = ((CARD32*)xcb_get_property_value(r))[3];
    home_button_geom.setRect(x, y, w, h);
    x = ((CARD32*)xcb_get_property_value(r))[4];
    y = ((CARD32*)xcb_get_property_value(r))[5];
    w = ((CARD32*)xcb_get_property_value(r))[6];
    h = ((CARD32*)xcb_get_property_value(r))[7];
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
    if (wm_protocols_valid)
        return wm_protocols;

    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_wm_protocols_cookie, 0);
    if (!r) {
        wm_protocols_valid = true;
        wm_protocols.clear();
        return wm_protocols;
    }
    int n_atoms = xcb_get_property_value_length(r) / sizeof(Atom);
    wm_protocols.clear();
    for (int i = 0; i < n_atoms; ++i)
        wm_protocols.append(((Atom*)xcb_get_property_value(r))[i]);
    free(r);
    wm_protocols_valid = true;
    return wm_protocols;
}

const QRectF &MWindowPropertyCache::iconGeometry()
{
    if (icon_geometry_valid)
        return icon_geometry;
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_icon_geom_cookie, 0);
    int len;
    if (!r)
        goto empty_geom;
    len = xcb_get_property_value_length(r);
    if ((unsigned)len < 4 * sizeof(CARD32)) {
        free(r);
        goto empty_geom;
    }
    icon_geometry.setRect(((CARD32*)xcb_get_property_value(r))[0],
                          ((CARD32*)xcb_get_property_value(r))[1],
                          ((CARD32*)xcb_get_property_value(r))[2],
                          ((CARD32*)xcb_get_property_value(r))[3]);
    free(r);
    icon_geometry_valid = true;
    return icon_geometry;
empty_geom:
    icon_geometry = QRectF();
    icon_geometry_valid = true;
    return icon_geometry;
}

#define OPAQUE 0xffffffff
int MWindowPropertyCache::globalAlpha()
{
    if (global_alpha != -1)
        return global_alpha;
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_global_alpha_cookie, 0);
    if (!r) {
        global_alpha = 255;
        return global_alpha;
    }
    int len = xcb_get_property_value_length(r);
    if ((unsigned)len < sizeof(CARD32)) {
        free(r);
        global_alpha = 255;
        return global_alpha;
    }
    CARD32 i = *((CARD32*)xcb_get_property_value(r));
    double opacity = i * 1.0 / OPAQUE;
    global_alpha = opacity * 255;
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

