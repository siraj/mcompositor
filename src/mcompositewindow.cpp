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

#include "mcompositewindow.h"
#include "mcompwindowanimator.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mtexturepixmapitem.h"
#include "mcompatoms_p.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <X11/Xatom.h>

bool MCompositeWindow::window_transitioning = false;

MCompositeWindow::MCompositeWindow(Qt::HANDLE window, QGraphicsItem *p)
    : QGraphicsItem(p),
      scalefrom(1),
      scaleto(1),
      scaled(false),
      zval(1),
      blur(false),
      iconified(false),
      iconified_final(false),
      iconify_state(NoIconifyState),
      destroyed(false),
      requestzval(false),
      process_status(NORMAL),
      need_decor(false),
      is_decorator(false),
      window_visible(true),
      transient_for((Window)-1),
      wm_protocols_valid(false),
      window_obscured(false),
      wmhints(0),
      attrs(0),
      meego_layer(-1),
      win_id(window),
      window_state(-1)
{
    memset(&req_geom, 0, sizeof(req_geom));
    anim = new MCompWindowAnimator(this);
    connect(anim, SIGNAL(transitionDone()),  SLOT(finalizeState()));
    connect(anim, SIGNAL(transitionDone()),  SLOT(windowSettled()));
    connect(anim, SIGNAL(transitionStart()), SLOT(windowTransitioning()));
    thumb_mode = false;
    setAcceptHoverEvents(true);

    t_ping = new QTimer(this);
    connect(t_ping, SIGNAL(timeout()), SLOT(pingWindow()));

    attrs = (XWindowAttributes*)calloc(1, sizeof(XWindowAttributes));
    if (!XGetWindowAttributes(QX11Info::display(), win_id, attrs)) {
        qWarning("%s: invalid window 0x%lx", __func__, win_id);
        free(attrs);
        attrs = 0;
        is_valid = false;
    } else
        is_valid = true;
}

MCompositeWindow::~MCompositeWindow()
{
    delete anim;
    anim = 0;
    if (wmhints) {
        XFree(wmhints);
        wmhints = 0;
    }
    if (attrs) {
        free(attrs);
        attrs = 0;
    }
}

void MCompositeWindow::setBlurred(bool b)
{
    blur = b;
    update();
}

void MCompositeWindow::setUnBlurred()
{
    blur = false;
    update();
}

bool MCompositeWindow::blurred()
{
    return blur;
}

void MCompositeWindow::saveState()
{
    anim->saveState();
}

void MCompositeWindow::localSaveState()
{
    anim->localSaveState();
}

// void MCompositeWindow::restore()
// {
//     anim->restore();
// }

// set the scale point to actual values
void MCompositeWindow::setScalePoint(qreal from, qreal to)
{
    scalefrom = from;
    scaleto = to;
}

void MCompositeWindow::setThumbMode(bool mode)
{
    thumb_mode = mode;
}

/* This is a delayed animation. Actual animation is triggered
 * when startTransition() is called
 */
void MCompositeWindow::iconify(const QRectF &iconGeometry, bool defer)
{
    this->iconGeometry = iconGeometry;
    origPosition = pos();

    // horizontal and vert. scaling factors
    qreal sx = iconGeometry.width() / boundingRect().width();
    qreal sy = iconGeometry.height() / boundingRect().height();

    anim->deferAnimation(defer);
    anim->translateScale(qreal(1.0), qreal(1.0), sx, sy,
                         iconGeometry.topLeft());
    iconified = true;
}

void MCompositeWindow::setIconified(bool iconified)
{
    iconified_final = iconified;
    iconify_state = ManualIconifyState;
    if (iconified && !anim->pendingAnimation())
        emit itemIconified(this);
    else if (!iconified && !anim->pendingAnimation())
        iconify_state = NoIconifyState;
}

MCompositeWindow::IconifyState MCompositeWindow::iconifyState() const
{
    return iconify_state;
}

void MCompositeWindow::setWindowObscured(bool obscured, bool no_notify)
{
    if (obscured == window_obscured)
        return;
    window_obscured = obscured;

    if (!no_notify) {
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = window();
        c.state      = obscured ? VisibilityFullyObscured :
                                  VisibilityUnobscured;
        XSendEvent(QX11Info::display(), window(), true,
                   VisibilityChangeMask, (XEvent *)&c);
    }
}

void MCompositeWindow::startTransition()
{
    if (anim->pendingAnimation()) {
        anim->startAnimation();
        anim->deferAnimation(false);
    }
}

// TODO: have an option of disabling the animation
void MCompositeWindow::restore(const QRectF &iconGeometry, bool defer)
{
    this->iconGeometry = iconGeometry;
    // horizontal and vert. scaling factors
    qreal sx = iconGeometry.width() / boundingRect().width();
    qreal sy = iconGeometry.height() / boundingRect().height();

    setVisible(true);

    anim->deferAnimation(defer);
    anim->translateScale(qreal(1.0), qreal(1.0), sx, sy, origPosition, true);
    iconified = false;
}

void MCompositeWindow::prettyDestroy()
{
    setVisible(true);
    destroyed = true;
    iconify();
}

void MCompositeWindow::finalizeState()
{
    // request zvalue
    if (requestzval)
        setZValue(zval);

    // iconification status
    if (iconified) {
        iconified_final = true;
        hide();
        iconify_state = TransitionIconifyState;
        emit itemIconified(this);
    } else {
        iconify_state = NoIconifyState;
        iconified_final = false;
        show();
        QTimer::singleShot(200, this, SLOT(q_itemRestored()));
    }

    // item lifetime
    if (destroyed)
        deleteLater();

    requestzval = false;
}

void MCompositeWindow::q_itemRestored()
{
    emit itemRestored(this);
}

void MCompositeWindow::requestZValue(int zvalue)
{
    if (anim->isActive()) {
        zval = zvalue;
        requestzval = true;
    } else {
        setZValue(zvalue);
        requestzval = false;
    }
}

bool MCompositeWindow::isIconified() const
{
    if (anim->isActive())
        return false;

    return iconified_final;
}
bool MCompositeWindow::isScaled() const
{
    return scaled;
}
void MCompositeWindow::setScaled(bool s)
{
    scaled = s;
}

Window MCompositeWindow::transientFor()
{
    if (transient_for == (Window)-1) {
        XGetTransientForHint(QX11Info::display(), win_id, &transient_for);
        if (transient_for == win_id)
            transient_for = 0;
    }
    return transient_for;
}

unsigned int MCompositeWindow::meegoStackingLayer()
{
    if (meego_layer < 0)
        meego_layer = MCompAtoms::instance()->cardValueProperty(window(),
                                                ATOM(_MEEGO_STACKING_LAYER));
    if (meego_layer > 6) meego_layer = 6;
    return (unsigned)meego_layer;
}

bool MCompositeWindow::wantsFocus()
{
    bool val = true;
    const XWMHints &h = getWMHints();
    if ((h.flags & InputHint) && (h.input == False))
        val = false;
    return val;
}

XID MCompositeWindow::windowGroup()
{
    XID val = 0;
    const XWMHints &h = getWMHints();
    if (h.flags & WindowGroupHint)
        val = h.window_group;
    return val;
}

const XWMHints &MCompositeWindow::getWMHints()
{
    if (!wmhints) {
        wmhints = XGetWMHints(QX11Info::display(), win_id);
        if (!wmhints) {
            wmhints = XAllocWMHints();
            memset(wmhints, 0, sizeof(XWMHints));
        }
    }
    return *wmhints;
}

bool MCompositeWindow::propertyEvent(XPropertyEvent *e)
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
    } else if (e->atom == ATOM(WM_PROTOCOLS)) {
        wm_protocols_valid = false;
    } else if (e->atom == ATOM(WM_STATE)) {
        Atom type;
        int format;
        unsigned long length, after;
        uchar *data = 0;
        int r = XGetWindowProperty(QX11Info::display(),window(), 
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
        meego_layer = MCompAtoms::instance()->cardValueProperty(window(),
                                                 ATOM(_MEEGO_STACKING_LAYER));
        return true;
    }
    return false;
}

const QList<Atom>& MCompositeWindow::supportedProtocols()
{
    if (!wm_protocols_valid) {
        Atom actual_type;
        int actual_format;
        unsigned long actual_n, left;
        unsigned char *data = NULL;
        int result = XGetWindowProperty(QX11Info::display(), win_id,
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

void MCompositeWindow::hoverEnterEvent(QGraphicsSceneHoverEvent *e)
{
    if (thumb_mode) {
        zval = zValue();
        setZValue(scene()->items().count() + 1);
        setZValue(100);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos());

    }
    return QGraphicsItem::hoverEnterEvent(e);
}

void MCompositeWindow::hoverLeaveEvent(QGraphicsSceneHoverEvent   *e)
{
    if (thumb_mode) {
        setZValue(zval);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos(), true);
    }
    return QGraphicsItem::hoverLeaveEvent(e);
}

void MCompositeWindow::mouseReleaseEvent(QGraphicsSceneMouseEvent *m)
{
    anim->restore();
    setThumbMode(false);
    setScaled(false);
    setZValue(100);

    emit acceptingInput();
    windowRaised();
    QGraphicsItem::mouseReleaseEvent(m);
}

void MCompositeWindow::manipulationEnabled(bool isEnabled)
{
    setFlag(QGraphicsItem::ItemIsMovable, isEnabled);
    setFlag(QGraphicsItem::ItemIsSelectable, isEnabled);
}

void MCompositeWindow::setVisible(bool visible)
{
    // Set the iconification status as well
    iconified_final = !visible;
    if (visible != window_visible)
        emit visualized(visible);
    window_visible = visible;

    QGraphicsItem::setVisible(visible);
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->setWindowDebugProperties(window());
}

void MCompositeWindow::startPing()
{
    if (t_ping->isActive())
        t_ping->stop();
    // this could be configurable. But will do for now. Most WMs use 5s delay
    t_ping->start(5000);
}

void MCompositeWindow::stopPing()
{
    t_ping->stop();
}

void MCompositeWindow::receivedPing(ulong serverTimeStamp)
{
    ping_server_timestamp = serverTimeStamp;
    startPing();
    process_status = NORMAL;
    if (blurred())
        setBlurred(false);
}

void MCompositeWindow::pingTimeout()
{
    if (ping_server_timestamp != ping_client_timestamp && process_status != HUNG) {
        setBlurred(true);
        emit windowHung(this);
        process_status = HUNG;
    }
}

void MCompositeWindow::setClientTimeStamp(ulong timeStamp)
{
    ping_client_timestamp = timeStamp;
}

void MCompositeWindow::pingWindow()
{
    QTimer::singleShot(5000, this, SLOT(pingTimeout()));
    emit pingTriggered(this);
}

MCompositeWindow::ProcessStatus MCompositeWindow::status() const
{
    return process_status;
}

bool MCompositeWindow::needDecoration() const
{
    return need_decor;
}

void MCompositeWindow::setDecorated(bool decorated)
{
    need_decor = decorated;
}

MCompositeWindow *MCompositeWindow::compositeWindow(Qt::HANDLE window)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->windows.value(window, 0);
}

Qt::HANDLE MCompositeWindow::window() const
{
    return win_id;
}

bool MCompositeWindow::isDecorator() const
{
    return is_decorator;
}

void MCompositeWindow::setIsDecorator(bool decorator)
{
    is_decorator = decorator;
}

void MCompositeWindow::windowTransitioning()
{
    window_transitioning = true;
}

void MCompositeWindow::windowSettled()
{
    window_transitioning = false;
    if (window_type_atom == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        emit desktopActivated(this);
}

bool MCompositeWindow::isTransitioning()
{
    return window_transitioning;
}

void MCompositeWindow::q_delayShow()
{
    MCompositeWindow::setVisible(true);
    updateWindowPixmap();
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->updateWinList();
}

QVariant MCompositeWindow::itemChange(GraphicsItemChange change, const QVariant &value)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    bool zvalChanged = (change == ItemZValueHasChanged);
    if (zvalChanged)
        p->d->setWindowDebugProperties(window());

    if (zvalChanged || change == ItemVisibleHasChanged || change == ItemParentHasChanged)
        p->d->glwidget->update();

    return QGraphicsItem::itemChange(change, value);
}

void MCompositeWindow::update()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->glwidget->update();
}

bool MCompositeWindow::windowVisible() const
{
    return window_visible;
}
