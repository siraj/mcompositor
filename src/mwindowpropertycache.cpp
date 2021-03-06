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

#include <QtGui>
#include <stdlib.h>
#include <QX11Info>
#include <QRect>
#include <QDebug>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/Xmd.h>
#include "mcompositemanager.h"
#include "mwindowpropertycache.h"
#include "mcompositemanager_p.h"

#define MAX_TYPES 10

xcb_connection_t *MWindowPropertyCache::xcb_conn;

void MWindowPropertyCache::init()
{
    transient_for = -1,
    wm_protocols_valid = false;
    icon_geometry_valid = false;
    decor_buttons_valid = false;
    shape_rects_valid = false;
    real_geom_valid = false;
    net_wm_state_valid = false;
    wm_state_query = false;
    has_alpha = -1;
    global_alpha = -1;
    video_global_alpha = -1;
    is_decorator = -1;
    wmhints = 0;
    attrs = 0;
    meego_layer = -1;
    window_state = -1;
    window_type = MCompAtoms::INVALID;
    parent_window = QX11Info::appRootWindow();
    always_mapped = -1;
    cannot_minimize = -1;
    desktop_view = -1;
    being_mapped = false;
    dont_iconify = false;
    custom_region = 0;
    custom_region_request_fired = false;
    orientation_angle = 0;
    xcb_real_geom = 0;
    damage_object = 0;

    memset(&req_geom, 0, sizeof(req_geom));
    memset(&home_button_geom, 0, sizeof(home_button_geom));
    memset(&close_button_geom, 0, sizeof(close_button_geom));
    window_type_atom = 0;
    memset(&xcb_real_geom_cookie, 0, sizeof(xcb_real_geom_cookie));
}

void MWindowPropertyCache::init_invalid()
{
    is_valid = false;
    memset(&xcb_transient_for_cookie, 0,
           sizeof(xcb_transient_for_cookie));
    memset(&xcb_meego_layer_cookie, 0, sizeof(xcb_meego_layer_cookie));
    memset(&xcb_is_decorator_cookie, 0, sizeof(xcb_is_decorator_cookie));
    memset(&xcb_window_type_cookie, 0, sizeof(xcb_window_type_cookie));
    memset(&xcb_decor_buttons_cookie, 0, sizeof(xcb_decor_buttons_cookie));
    memset(&xcb_orientation_angle_cookie, 0,
           sizeof(xcb_orientation_angle_cookie));
    memset(&xcb_statusbar_cookie, 0, sizeof(xcb_statusbar_cookie));
    memset(&xcb_wm_protocols_cookie, 0, sizeof(xcb_wm_protocols_cookie));
    memset(&xcb_wm_state_cookie, 0, sizeof(xcb_wm_state_cookie));
    memset(&xcb_wm_hints_cookie, 0, sizeof(xcb_wm_hints_cookie));
    memset(&xcb_icon_geom_cookie, 0, sizeof(xcb_icon_geom_cookie));
    memset(&xcb_global_alpha_cookie, 0, sizeof(xcb_global_alpha_cookie));
    memset(&xcb_video_global_alpha_cookie, 0,
           sizeof(xcb_video_global_alpha_cookie));
    memset(&xcb_net_wm_state_cookie, 0, sizeof(xcb_net_wm_state_cookie));
    memset(&xcb_always_mapped_cookie, 0, sizeof(xcb_always_mapped_cookie));
    memset(&xcb_cannot_minimize_cookie, 0, sizeof(xcb_always_mapped_cookie));
    memset(&xcb_pict_formats_cookie, 0, sizeof(xcb_pict_formats_cookie));
    memset(&xcb_shape_rects_cookie, 0, sizeof(xcb_shape_rects_cookie));
    memset(&real_geom, 0, sizeof(real_geom));
    memset(&statusbar_geom, 0, sizeof(statusbar_geom));
}

MWindowPropertyCache::MWindowPropertyCache(Window w,
                        xcb_get_window_attributes_reply_t *wa,
                        xcb_get_geometry_reply_t *geom,
                        Damage damage_obj)
    : window(w)
{
    init();
    if (!wa) {
        attrs = xcb_get_window_attributes_reply(xcb_conn,
                        xcb_get_window_attributes(xcb_conn, window), 0);
        if (!attrs) {
            //qWarning("%s: invalid window 0x%lx", __func__, window);
            init_invalid();
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
    damage_object = damage_obj;

    if (!isMapped()) {
        // required to get property changes happening before mapping
        // (after mapping, MCompositeManager sets the window's input mask)
        XSelectInput(QX11Info::display(), window, PropertyChangeMask);
        XShapeSelectInput(QX11Info::display(), window, ShapeNotifyMask);
    }

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
    // FIXME: pict formats do not seem window-specific -- get them only once
    xcb_pict_formats_cookie = xcb_render_query_pict_formats(xcb_conn);
    xcb_decor_buttons_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS),
                                       XCB_ATOM_CARDINAL, 0, 8);
    xcb_orientation_angle_cookie = xcb_get_property(xcb_conn, 0, window,
                                    ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE),
                                    XCB_ATOM_CARDINAL, 0, 1);
    xcb_statusbar_cookie = xcb_get_property(xcb_conn, 0, window,
                                    ATOM(_MEEGOTOUCH_MSTATUSBAR_GEOMETRY),
                                    XCB_ATOM_CARDINAL, 0, 4);
    xcb_wm_protocols_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(WM_PROTOCOLS),
                                               XCB_ATOM_ATOM, 0, 100);
    xcb_wm_state_cookie = xcb_get_property(xcb_conn, 0, window,
                                  ATOM(WM_STATE), ATOM(WM_STATE), 0, 1);
    wm_state_query = true;
    xcb_wm_hints_cookie = xcb_get_property(xcb_conn, 0, window,
                                  XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 0, 10);
    xcb_icon_geom_cookie = xcb_get_property(xcb_conn, 0, window,
                                            ATOM(_NET_WM_ICON_GEOMETRY),
                                            XCB_ATOM_CARDINAL, 0, 4);
    xcb_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                                               XCB_ATOM_CARDINAL, 0, 1);
    xcb_video_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(_MEEGOTOUCH_VIDEO_ALPHA),
                                               XCB_ATOM_CARDINAL, 0, 1);
    xcb_shape_rects_cookie = xcb_shape_get_rectangles(xcb_conn, window,
                                                      ShapeBounding);
    xcb_net_wm_state_cookie = xcb_get_property(xcb_conn, 0, window,
                                               ATOM(_NET_WM_STATE),
                                               XCB_ATOM_ATOM, 0, 100);
    xcb_always_mapped_cookie = xcb_get_property(xcb_conn, 0, window,
                                                ATOM(_MEEGOTOUCH_ALWAYS_MAPPED),
                                                XCB_ATOM_CARDINAL, 0, 1);
    xcb_cannot_minimize_cookie = xcb_get_property(xcb_conn, 0, window,
                                         ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE),
                                         XCB_ATOM_CARDINAL, 0, 1);
    // add any transients to the transients list
    MCompositeManager *m = (MCompositeManager*)qApp;
    for (QList<Window>::const_iterator it = m->d->stacking_list.begin();
         it != m->d->stacking_list.end(); ++it) {
        MWindowPropertyCache *p = m->d->prop_caches.value(*it, 0);
        if (p && p != this && p->transientFor() == window)
            transients.append(*it);
    }
    connect(this, SIGNAL(meegoDecoratorButtonsChanged(Window)),
            m->d, SLOT(setupButtonWindows(Window)));
}

MWindowPropertyCache::MWindowPropertyCache()
    : window(None)
{
    init();
    init_invalid();
}

MWindowPropertyCache::~MWindowPropertyCache()
{
    if (custom_region) delete custom_region;
    if (!is_valid) {
        // no pending XCB requests
        if (wmhints)
            XFree(wmhints);
        return;
    }
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
    if (transient_for && transient_for != (Window)-1) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        // remove reference from the old "parent"
        MWindowPropertyCache *p = m->d->prop_caches.value(transient_for, 0);
        if (p) p->transients.removeAll(window);
    }
    if (transient_for == (Window)-1)
        xcb_discard_reply(xcb_conn, xcb_transient_for_cookie.sequence);
    if (always_mapped < 0)
        xcb_discard_reply(xcb_conn, xcb_always_mapped_cookie.sequence);
    if (cannot_minimize < 0)
        xcb_discard_reply(xcb_conn, xcb_cannot_minimize_cookie.sequence);
    desktopView(false);  // free the reply if it has been requested
    if (meego_layer < 0)
        xcb_discard_reply(xcb_conn, xcb_meego_layer_cookie.sequence);
    if (is_decorator < 0)
        xcb_discard_reply(xcb_conn, xcb_is_decorator_cookie.sequence);
    if (window_type == MCompAtoms::INVALID)
        xcb_discard_reply(xcb_conn, xcb_window_type_cookie.sequence);
    if (has_alpha < 0)
        xcb_discard_reply(xcb_conn, xcb_pict_formats_cookie.sequence);
    if (!decor_buttons_valid)
        xcb_discard_reply(xcb_conn, xcb_decor_buttons_cookie.sequence);
    if (!wm_protocols_valid)
        xcb_discard_reply(xcb_conn, xcb_wm_protocols_cookie.sequence);
    if (custom_region_request_fired)
        xcb_discard_reply(xcb_conn, xcb_custom_region_cookie.sequence);
    xcb_discard_reply(xcb_conn, xcb_orientation_angle_cookie.sequence);
    xcb_discard_reply(xcb_conn, xcb_statusbar_cookie.sequence);
    if (wm_state_query)
        windowState();
    if (!icon_geometry_valid)
        iconGeometry();
    if (global_alpha < 0)
        globalAlpha();
    if (video_global_alpha < 0)
        videoGlobalAlpha();
    if (!shape_rects_valid)
        shapeRegion();
    if (!net_wm_state_valid)
        netWmState();
    damageTracking(false);
}

bool MWindowPropertyCache::hasAlpha()
{
    if (!is_valid || has_alpha >= 0)
        return has_alpha == 1 ? true : false;

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

const QRegion &MWindowPropertyCache::shapeRegion()
{
    if (!is_valid || shape_rects_valid) {
        if (!shape_region.isEmpty() && isInputOnly())
            // InputOnly window obstructs nothing
            shape_region = QRegion(0, 0, 0, 0);
        if (shape_region.isEmpty())
            shape_region = QRegion(realGeometry());
        return shape_region;
    }
    xcb_shape_get_rectangles_reply_t *r;
    r = xcb_shape_get_rectangles_reply(xcb_conn, xcb_shape_rects_cookie, 0);
    if (!r) {
        shape_rects_valid = true;
        shape_region = QRegion(realGeometry());
        return shape_region;
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

const QRegion &MWindowPropertyCache::customRegion(bool request_only)
{
    if (!is_valid) {
        if (!custom_region) custom_region = new QRegion(0, 0, 0, 0);
        return *custom_region;
    }
    if (request_only || (!custom_region && !custom_region_request_fired)) {
        if (custom_region_request_fired)
            customRegion(false); // free the old reply
        xcb_custom_region_cookie = xcb_get_property(xcb_conn, 0, window,
                                         ATOM(_MEEGOTOUCH_CUSTOM_REGION),
                                         XCB_ATOM_CARDINAL, 0, 10 * 4);
        custom_region_request_fired = true;
    }
    if (!request_only && custom_region_request_fired) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_custom_region_cookie, 0);
        custom_region_request_fired = false;
        if (custom_region)
            delete custom_region;
        custom_region = new QRegion(0, 0, 0, 0);
        if (r) {
            int len = xcb_get_property_value_length(r);
            if (len >= (int)sizeof(CARD32) * 4) {
                int n = len / sizeof(CARD32) / 4;
                CARD32 *p = (CARD32*)xcb_get_property_value(r);
                for (int i = 0; i < n; ++i) {
                     int j = i * 4;
                     QRect tmp(p[j], p[j + 1], p[j + 2], p[j + 3]);
                     *custom_region += tmp;
                }
            }
            free(r);
        }
    }
    return *custom_region;
}

Window MWindowPropertyCache::transientFor()
{
    if (is_valid && transient_for == (Window)-1) {
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
            if (transient_for) {
                MCompositeManager *m = (MCompositeManager*)qApp;
                // add reference to the "parent"
                MWindowPropertyCache *p = m->d->prop_caches.value(
                                                        transient_for, 0);
                if (p) p->transients.append(window);
                // need to check stacking again to make sure the "parent" is
                // stacked according to the changed transient window list
                m->d->dirtyStacking(false);
            }
        } else
            transient_for = 0;
    }
    return transient_for;
}

int MWindowPropertyCache::cannotMinimize()
{
    if (is_valid && cannot_minimize < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_cannot_minimize_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                cannot_minimize = *((CARD32*)xcb_get_property_value(r));
            else
                cannot_minimize = 0;
            free(r);
        } else
            cannot_minimize = 0;
    }
    return cannot_minimize;
}

int MWindowPropertyCache::alwaysMapped()
{
    if (is_valid && always_mapped < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_always_mapped_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                always_mapped = *((CARD32*)xcb_get_property_value(r));
            else
                always_mapped = 0;
            free(r);
        } else
            always_mapped = 0;
    }
    return always_mapped;
}

int MWindowPropertyCache::desktopView(bool request_only)
{
    static bool request_fired = false;
    static xcb_get_property_cookie_t c;
    if (is_valid && request_only) {
        if (request_fired)
            // free the old reply
            desktopView(false);
        c = xcb_get_property(xcb_conn, 0, window,
                             ATOM(_MEEGOTOUCH_DESKTOP_VIEW),
                             XCB_ATOM_CARDINAL, 0, 1);
        request_fired = true;
    } else if (is_valid && request_fired) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, c, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                desktop_view = *((CARD32*)xcb_get_property_value(r));
            else
                desktop_view = -1;
            free(r);
        } else
            desktop_view = -1;
        request_fired = false;
    }
    return desktop_view;
}

bool MWindowPropertyCache::isDecorator()
{
    if (is_valid && is_decorator < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_is_decorator_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                is_decorator = *((CARD32*)xcb_get_property_value(r));
            else
                is_decorator = 0;
            free(r);
        } else
            is_decorator = 0;
    }
    return is_decorator == 1;
}

unsigned int MWindowPropertyCache::meegoStackingLayer()
{
    if (is_valid && meego_layer < 0) {
        xcb_get_property_reply_t *r;
        r = xcb_get_property_reply(xcb_conn, xcb_meego_layer_cookie, 0);
        if (r) {
            if (xcb_get_property_value_length(r) == sizeof(CARD32))
                meego_layer = *((CARD32*)xcb_get_property_value(r));
            else
                meego_layer = 0;
            free(r);
        } else
            meego_layer = 0;
    }
    if (meego_layer > 6) meego_layer = 6;
    return (unsigned)meego_layer;
}

/*!
 * Used for special windows that should not be minimised/iconified.
 */
bool MWindowPropertyCache::dontIconify()
{
    if (dont_iconify)
        return true;
    if (cannotMinimize() > 0)
        return true;
    int layer = meegoStackingLayer();
    if (layer == 1 || layer == 2)
        // these (screen/device lock) cannot be iconified by default
        return true;
    return false;
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
    if (!is_valid) {
        if (!wmhints)
            goto empty_value;
        else
            return *wmhints;
    }
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
    if (!is_valid)
        return false;
    if (e->atom == ATOM(WM_TRANSIENT_FOR)) {
        if (transient_for == (Window)-1)
            // collect the old reply
            transientFor();
        if (transient_for && transient_for != (Window)-1) {
            MCompositeManager *m = (MCompositeManager*)qApp;
            // remove reference from the old "parent"
            MWindowPropertyCache *p = m->d->prop_caches.value(transient_for, 0);
            if (p) p->transients.removeAll(window);
        }
        transient_for = (Window)-1;
        xcb_transient_for_cookie = xcb_get_property(xcb_conn, 0, window,
                                                    XCB_ATOM_WM_TRANSIENT_FOR,
                                                    XCB_ATOM_WINDOW, 0, 1);
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_ALWAYS_MAPPED)) {
        if (always_mapped < 0)
            // collect the old reply
            alwaysMapped();
        always_mapped = -1;
        xcb_always_mapped_cookie = xcb_get_property(xcb_conn, 0, window,
                                           ATOM(_MEEGOTOUCH_ALWAYS_MAPPED),
                                           XCB_ATOM_CARDINAL, 0, 1);
        emit alwaysMappedChanged(this);
    } else if (e->atom == ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE)) {
        if (cannot_minimize < 0)
            // collect the old reply
            cannotMinimize();
        cannot_minimize = -1;
        xcb_cannot_minimize_cookie = xcb_get_property(xcb_conn, 0, window,
                                           ATOM(_MEEGOTOUCH_CANNOT_MINIMIZE),
                                           XCB_ATOM_CARDINAL, 0, 1);
    } else if (e->atom == ATOM(_MEEGOTOUCH_DESKTOP_VIEW)) {
        emit desktopViewChanged(this);
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
        if (window_type == MCompAtoms::INVALID)
            // collect the old reply
            windowType();
        window_type = MCompAtoms::INVALID;
        xcb_window_type_cookie = xcb_get_property(xcb_conn, 0, window,
                                                  ATOM(_NET_WM_WINDOW_TYPE),
                                                  XCB_ATOM_ATOM, 0, MAX_TYPES);
    } else if (e->atom == ATOM(_NET_WM_ICON_GEOMETRY)) {
        if (!icon_geometry_valid)
            // collect the old reply
            iconGeometry();
        icon_geometry_valid = false;
        xcb_icon_geom_cookie = xcb_get_property(xcb_conn, 0, window,
                                            ATOM(_NET_WM_ICON_GEOMETRY),
                                            XCB_ATOM_CARDINAL, 0, 4);
        emit iconGeometryUpdated();
    } else if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA)) {
        if (global_alpha < 0)
            // collect the old reply
            globalAlpha();
        global_alpha = -1;
        xcb_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_GLOBAL_ALPHA),
                                       XCB_ATOM_CARDINAL, 0, 1);
    } else if (e->atom == ATOM(_MEEGOTOUCH_VIDEO_ALPHA)) {
        if (video_global_alpha < 0)
            // collect the old reply
            videoGlobalAlpha();
        video_global_alpha = -1;
        xcb_video_global_alpha_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_VIDEO_ALPHA),
                                       XCB_ATOM_CARDINAL, 0, 1);
    } else if (e->atom == ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS)) {
        if (!decor_buttons_valid)
            // collect the old reply
            buttonGeometryHelper();
        decor_buttons_valid = false;
        xcb_decor_buttons_cookie = xcb_get_property(xcb_conn, 0, window,
                                       ATOM(_MEEGOTOUCH_DECORATOR_BUTTONS),
                                       XCB_ATOM_CARDINAL, 0, 8);
        emit meegoDecoratorButtonsChanged(window);
    } else if (e->atom == ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE)) {
        xcb_discard_reply(xcb_conn, xcb_orientation_angle_cookie.sequence);
        xcb_orientation_angle_cookie = xcb_get_property(xcb_conn, 0, window,
                                    ATOM(_MEEGOTOUCH_ORIENTATION_ANGLE),
                                    XCB_ATOM_CARDINAL, 0, 1);
    } else if (e->atom == ATOM(_MEEGOTOUCH_MSTATUSBAR_GEOMETRY)) {
        xcb_discard_reply(xcb_conn, xcb_statusbar_cookie.sequence);
        xcb_statusbar_cookie = xcb_get_property(xcb_conn, 0, window,
                                    ATOM(_MEEGOTOUCH_MSTATUSBAR_GEOMETRY),
                                    XCB_ATOM_CARDINAL, 0, 4);
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        if (!wm_protocols_valid)
            // collect the old reply
            supportedProtocols();
        wm_protocols_valid = false;
        xcb_wm_protocols_cookie = xcb_get_property(xcb_conn, 0, window,
                                                   ATOM(WM_PROTOCOLS),
                                                   XCB_ATOM_ATOM, 0, 100);
    } else if (e->atom == ATOM(_NET_WM_STATE)) {
        if (!net_wm_state_valid)
            // collect the old reply
            netWmState();
        net_wm_state_valid = false;
        xcb_net_wm_state_cookie = xcb_get_property(xcb_conn, 0, window,
                                                   ATOM(_NET_WM_STATE),
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
        if (window_state == NormalState) {
            // raise it so that it becomes on top of same-leveled windows
            MCompositeManager *m = (MCompositeManager*)qApp;
            m->d->positionWindow(window, true);
        }
        return true;
    } else if (e->atom == ATOM(_MEEGOTOUCH_CUSTOM_REGION)) {
        emit customRegionChanged(this);
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
            // mark it so that MCompositeManagerPrivate::setWindowState sets it
            window_state = -1;
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
    if (!is_valid || decor_buttons_valid)
        return home_button_geom;
    buttonGeometryHelper();
    return home_button_geom;
}

const QRect &MWindowPropertyCache::closeButtonGeometry()
{
    if (!is_valid || decor_buttons_valid)
        return close_button_geom;
    buttonGeometryHelper();
    return close_button_geom;
}

unsigned MWindowPropertyCache::orientationAngle()
{
    xcb_get_property_reply_t *r;

    if (!xcb_orientation_angle_cookie.sequence)
        return orientation_angle;

    orientation_angle = 0;
    r = xcb_get_property_reply(xcb_conn, xcb_orientation_angle_cookie, 0);
    if (r != NULL) {
        if (xcb_get_property_value_length(r) == sizeof(CARD32))
            orientation_angle = *((CARD32*)xcb_get_property_value(r));
        free(r);
    }
    xcb_orientation_angle_cookie.sequence = 0;

    return orientation_angle;
}

const QRect &MWindowPropertyCache::statusbarGeometry()
{
    if (!xcb_statusbar_cookie.sequence)
        return statusbar_geom;

    xcb_get_property_reply_t *r;
    unsigned long x, y, w, h;
    int len;
    r = xcb_get_property_reply(xcb_conn, xcb_statusbar_cookie, 0);
    if (!r)
        goto failure;
    len = xcb_get_property_value_length(r);
    if (len != 4 * sizeof(CARD32)) {
        free(r);
        goto failure;
    }
    x = ((CARD32*)xcb_get_property_value(r))[0];
    y = ((CARD32*)xcb_get_property_value(r))[1];
    w = ((CARD32*)xcb_get_property_value(r))[2];
    h = ((CARD32*)xcb_get_property_value(r))[3];
    statusbar_geom.setRect(x, y, w, h);
    free(r);
    xcb_statusbar_cookie.sequence = 0;
    return statusbar_geom;

failure:
    statusbar_geom.setRect(0, 0, 0, 0);
    return statusbar_geom;
}

const QList<Atom>& MWindowPropertyCache::supportedProtocols()
{
    if (!is_valid || wm_protocols_valid)
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

const QList<Atom> &MWindowPropertyCache::netWmState()
{
    if (!is_valid || net_wm_state_valid)
        return net_wm_state;
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, xcb_net_wm_state_cookie, 0);
    if (!r) {
        net_wm_state_valid = true;
        net_wm_state.clear();
        return net_wm_state;
    }
    int n_atoms = xcb_get_property_value_length(r) / sizeof(Atom);
    net_wm_state.clear();
    for (int i = 0; i < n_atoms; ++i)
        net_wm_state.append(((Atom*)xcb_get_property_value(r))[i]);
    free(r);
    net_wm_state_valid = true;
    return net_wm_state;
}

const QRectF &MWindowPropertyCache::iconGeometry()
{
    if (!is_valid || icon_geometry_valid)
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

int MWindowPropertyCache::alphaValue(xcb_get_property_cookie_t c)
{
    xcb_get_property_reply_t *r;
    r = xcb_get_property_reply(xcb_conn, c, 0);
    if (!r) 
        return 255;
    
    int len = xcb_get_property_value_length(r);
    if ((unsigned)len < sizeof(CARD32)) {
        free(r);
        return 255;
    }

    /* Map 0..0xFFFFFFFF -> 0..0xFF. */
    int ret = *(CARD32*)xcb_get_property_value(r) >> 24;
    free(r);
    return ret;
}

int MWindowPropertyCache::globalAlpha()
{
    if (!is_valid || global_alpha != -1)
        return global_alpha;
    
    global_alpha = alphaValue(xcb_global_alpha_cookie);
    return global_alpha;
}

int MWindowPropertyCache::videoGlobalAlpha()
{    
    if (!is_valid || video_global_alpha != -1)
        return video_global_alpha;
    
    video_global_alpha = alphaValue(xcb_video_global_alpha_cookie);
    return video_global_alpha;
}

MCompAtoms::Type MWindowPropertyCache::windowType()
{
    if (!is_valid || window_type != MCompAtoms::INVALID)
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
    } else {
        window_type = MCompAtoms::NORMAL;
        return window_type;
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

// MWindowDummyPropertyCache
MWindowDummyPropertyCache *MWindowDummyPropertyCache::singleton;

MWindowDummyPropertyCache *MWindowDummyPropertyCache::get()
{
    if (!singleton)
        singleton = new MWindowDummyPropertyCache();
    return singleton;
}

bool MWindowDummyPropertyCache::event(QEvent *e)
{
    // Ignore deleteLater().
    return e->type() == QEvent::DeferredDelete
        ? true : MWindowPropertyCache::event(e);
}
