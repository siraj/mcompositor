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

#include "duicompositewindow.h"
#include "duicompwindowanimator.h"
#include "duicompositemanager.h"
#include "duicompositemanager_p.h"
#include "duitexturepixmapitem.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

bool DuiCompositeWindow::window_transitioning = false;

DuiCompositeWindow::DuiCompositeWindow(Qt::HANDLE window, QGraphicsItem *p)
    : QGraphicsItem(p),
      scalefrom(1),
      scaleto(1),
      scaled(false),
      zval(1),
      blur(false),
      iconified(false),
      iconified_final(false),
      destroyed(false),
      requestzval(false),
      process_hung(false),
      process_status(NORMAL),
      need_decor(false),
      is_decorator(false),
      window_visible(true),
      win_id(window)
{
    anim = new DuiCompWindowAnimator(this);
    connect(anim, SIGNAL(transitionDone()),  SLOT(finalizeState()));
    connect(anim, SIGNAL(transitionDone()),  SLOT(windowSettled()));
    connect(anim, SIGNAL(transitionStart()), SLOT(windowTransitioning()));
    thumb_mode = false;
    setAcceptHoverEvents(true);

    t_ping = new QTimer(this);
    connect(t_ping, SIGNAL(timeout()), SLOT(pingWindow()));
}

DuiCompositeWindow::~DuiCompositeWindow()
{
    delete anim;
    anim = 0;
}

void DuiCompositeWindow::setBlurred(bool b)
{
    blur = b;
    update();
}

void DuiCompositeWindow::setUnBlurred()
{
    blur = false;
    update();
}

bool DuiCompositeWindow::blurred()
{
    return blur;
}

void DuiCompositeWindow::saveState()
{
    anim->saveState();
}

void DuiCompositeWindow::localSaveState()
{
    anim->localSaveState();
}

// void DuiCompositeWindow::restore()
// {
//     anim->restore();
// }

// set the scale point to actual values
void DuiCompositeWindow::setScalePoint(qreal from, qreal to)
{
    scalefrom = from;
    scaleto = to;
}

void DuiCompositeWindow::setThumbMode(bool mode)
{
    thumb_mode = mode;
}

/* This is a delayed animation. Actual animation is triggered
 * when startTransition() is called
 */
void DuiCompositeWindow::iconify(const QRectF &iconGeometry, bool defer)
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

void DuiCompositeWindow::setIconified(bool iconified)
{
    iconified_final = iconified;
    if (iconified && !anim->pendingAnimation())
        emit itemIconified();
}

void DuiCompositeWindow::startTransition()
{
    anim->startAnimation();
    anim->deferAnimation(false);
}

// TODO: have an option of disabling the animation
void DuiCompositeWindow::restore(const QRectF &iconGeometry, bool defer)
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

void DuiCompositeWindow::prettyDestroy()
{
    setVisible(true);
    destroyed = true;
    iconify();
}

void DuiCompositeWindow::finalizeState()
{
    // request zvalue
    if (requestzval)
        setZValue(zval);

    // iconification status
    if (iconified) {
        iconified_final = true;
        hide();
        emit itemIconified();
    } else {
        iconified_final = false;
        show();
        QTimer::singleShot(200, this, SIGNAL(itemRestored()));
    }

    // item lifetime
    if (destroyed)
        deleteLater();

    requestzval = false;
}

void DuiCompositeWindow::requestZValue(int zvalue)
{
    if (anim->isActive()) {
        zval = zvalue;
        requestzval = true;
    } else
        setZValue(zvalue);
}

bool DuiCompositeWindow::isIconified() const
{
    if (anim->isActive())
        return false;

    return iconified_final;
}
bool DuiCompositeWindow::isScaled() const
{
    return scaled;
}
void DuiCompositeWindow::setScaled(bool s)
{
    scaled = s;
}

void DuiCompositeWindow::hoverEnterEvent(QGraphicsSceneHoverEvent *e)
{
    if (thumb_mode) {
        zval = zValue();
        setZValue(scene()->items().count() + 1);
        setZValue(100);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos());

    }
    return QGraphicsItem::hoverEnterEvent(e);
}

void DuiCompositeWindow::hoverLeaveEvent(QGraphicsSceneHoverEvent   *e)
{
    if (thumb_mode) {
        setZValue(zval);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos(), true);
    }
    return QGraphicsItem::hoverLeaveEvent(e);
}

void DuiCompositeWindow::mouseReleaseEvent(QGraphicsSceneMouseEvent *m)
{
    anim->restore();
    setThumbMode(false);
    setScaled(false);
    setZValue(100);

    emit acceptingInput();
    windowRaised();
    QGraphicsItem::mouseReleaseEvent(m);
}

void DuiCompositeWindow::manipulationEnabled(bool isEnabled)
{
    setFlag(QGraphicsItem::ItemIsMovable, isEnabled);
    setFlag(QGraphicsItem::ItemIsSelectable, isEnabled);
}

void DuiCompositeWindow::setVisible(bool visible)
{
    // Set the iconification status as well
    iconified_final = !visible;
    emit visualized(visible);
    window_visible = visible;

    QGraphicsItem::setVisible(visible);
}

void DuiCompositeWindow::startPing()
{
    // this could be configurable. But will do for now. Most WMs use 5s delay
    t_ping->start(5000);
}

void DuiCompositeWindow::stopPing()
{
    t_ping->stop();
}

void DuiCompositeWindow::receivedPing()
{
    process_hung = false;
    process_status = NORMAL;
    if (blurred())
        setBlurred(false);
}

void DuiCompositeWindow::pingTimeout()
{
    if (process_hung) {
        setBlurred(true);
        emit windowHung(this);
        process_status = HUNG;
    }
}

void DuiCompositeWindow::pingWindow()
{
    process_hung = true;

    // 1s delay, if we dont get anything, means window is dead!
    QTimer::singleShot(1000, this, SLOT(pingTimeout()));
    emit pingTriggered(this);
}

DuiCompositeWindow::ProcessStatus DuiCompositeWindow::status() const
{
    return process_status;
}

bool DuiCompositeWindow::needDecoration() const
{
    return need_decor;
}

void DuiCompositeWindow::setDecorated(bool decorated)
{
    need_decor = decorated;
}

DuiCompositeWindow *DuiCompositeWindow::compositeWindow(Qt::HANDLE window)
{
    DuiCompositeManager *p = (DuiCompositeManager *) qApp;
    return p->d->texturePixmapItem(window);
}

Qt::HANDLE DuiCompositeWindow::window() const
{
    return win_id;
}

bool DuiCompositeWindow::isDecorator() const
{
    return is_decorator;
}

void DuiCompositeWindow::setDecoratorWindow(bool decorator)
{
    is_decorator = decorator;
}

void DuiCompositeWindow::windowTransitioning()
{
    window_transitioning = true;
}

void DuiCompositeWindow::windowSettled()
{
    window_transitioning = false;
}

bool DuiCompositeWindow::isTransitioning() const
{
    return window_transitioning;
}

void DuiCompositeWindow::delayShow(int delay)
{
    // TODO: only do this for qt/dui apps because it delays translucency
    setVisible(false);
    QTimer::singleShot(delay, this, SLOT(q_delayShow()));
}

void DuiCompositeWindow::q_delayShow()
{
    DuiCompositeWindow::setVisible(true);
    updateWindowPixmap();
}

QVariant DuiCompositeWindow::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemZValueHasChanged  ||
            change == ItemVisibleHasChanged ||
            change == ItemParentHasChanged) {
        DuiCompositeManager *p = (DuiCompositeManager *) qApp;
        p->d->glwidget->update();
    }
    return QGraphicsItem::itemChange(change, value);
}

void DuiCompositeWindow::update()
{
    DuiCompositeManager *p = (DuiCompositeManager *) qApp;
    p->d->glwidget->update();
}

bool DuiCompositeWindow::windowVisible() const
{
    return window_visible;
}
