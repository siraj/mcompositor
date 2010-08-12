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

#include "mcompwindowanimator.h"
#include "mcompositewindow.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"

static int Fps = 60;

static qreal interpolate(qreal step, qreal x1, qreal x2)
{
    return ((x2 - x1) * step) + x1;
}

MCompWindowAnimator::MCompWindowAnimator(MCompositeWindow *item)
    : QObject(item),
      timer(200),
      reversed(false),
      deferred_animation(false)
{
    this->item = item;
    timer.setFrameRange(0, 2000);
    timer.setUpdateInterval(int(1000.0 / Fps));

    // So that we have complete control of the transformation
    // we don't call setitem
    // anim.setItem(item);
    //anim.setTimeLine(&timer);
    connect(&timer, SIGNAL(valueChanged(qreal)), SLOT(advanceFrame(qreal)));
    connect(&timer, SIGNAL(finished()), SIGNAL(transitionDone()));
}

// restore original global state w/ animation
// TODO: rename this. this is not similar to restore state!
void MCompWindowAnimator::restore()
{
    item->setTransform(matrix);
    item->setZValue(zval);

    // animate
    anim.setPosAt(0, item->pos());
    anim.setPosAt(1.0, initpos);

    anim.setScaleAt(0, item->transform().m11(), item->transform().m22());
    anim.setScaleAt(1.0, 1.0, 1.0);

    if (timer.state() == QTimeLine::NotRunning) {
        emit transitionStart();
        timer.start();
    }

    // item->setPos(initpos);
}

// item transition
void MCompWindowAnimator::advanceFrame(qreal step)
{
#define OPAQUE 1.0
#define DIMMED 0.1

    //item->setTransform(QTransform(anim.matrixAt(step)) );
    item->setTransform(matrix);

    item->scale(anim.horizontalScaleAt(step),
                anim.verticalScaleAt(step));
    item->setPos(anim.posAt(step));

    qreal opac_norm = interpolate(step, OPAQUE, DIMMED);
    qreal opac_rev = interpolate(step, DIMMED, OPAQUE);
    
    // TODO: move calculation to GPU to imrpove speed
    // TODO: Use QPropertyAnimation instead
    ((MCompositeWindow*)item)->setDimmedEffect(false);
    item->setOpacity(!reversed ? opac_norm : opac_rev);
    MCompositeWindow* behind = ((MCompositeWindow*)item)->behind();
    if (behind) {
        behind->setDimmedEffect(true);
        behind->setOpacity(!reversed ? opac_rev : opac_norm);
    }
    
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->glwidget->update();
}

// translate item + scale
void MCompWindowAnimator::translateScale(qreal fromSx, qreal fromSy,
        qreal toSx, qreal toSy,
        const QPointF &newPos,
        bool reverse)
{
    reversed = reverse;

    if (!reverse) {
        timer.setCurveShape(QTimeLine::EaseInCurve);
        
        anim.setScaleAt(0, fromSx, fromSy);
        anim.setScaleAt(1.0, toSx, toSy);
        anim.setPosAt(0, item->pos());
        anim.setPosAt(1.0, newPos);
    } else {
        timer.setCurveShape(QTimeLine::EaseOutCurve);
        
        if (item->transform().m22() == 1.0 && item->transform().m11() == 1.0)
            item->scale(toSx, toSy);

        anim.setScaleAt(0, toSx, toSy);
        anim.setScaleAt(1.0, fromSx, fromSy);
        anim.setPosAt(0, item->pos());
        anim.setPosAt(1.0, newPos);
    }

    if (!deferred_animation)
        if (timer.state() == QTimeLine::NotRunning) {
            emit transitionStart();
            timer.start();
        }
}

// call this after item is scaled to desired size
void MCompWindowAnimator::localSaveState()
{
    local = item->transform();
}

void MCompWindowAnimator::saveState(int states)
{
    if ((states & MCompWindowAnimator::Transformation) ||
            (states & MCompWindowAnimator::All))
        matrix     = item->transform();

    if ((states & MCompWindowAnimator::ZValue) ||
            (states & MCompWindowAnimator::All))
        zval       = item->zValue();

    if ((states & MCompWindowAnimator::InitialPosition) ||
            (states & MCompWindowAnimator::All))
        initpos    = item->pos();

    if ((states & MCompWindowAnimator::Visibility) ||
            (states & MCompWindowAnimator::All))
        visibility = item->isVisible();
}

void MCompWindowAnimator::restoreState(int states)
{
    if ((states & MCompWindowAnimator::Transformation) ||
            (states & MCompWindowAnimator::All))
        item->setTransform(matrix);

    if ((states & MCompWindowAnimator::ZValue) ||
            (states & MCompWindowAnimator::All))
        item->setZValue(zval);

    if ((states & MCompWindowAnimator::InitialPosition) ||
            (states & MCompWindowAnimator::All))
        item->setPos(initpos);

    if ((states & MCompWindowAnimator::Visibility) ||
            (states & MCompWindowAnimator::All))
        item->setVisible(visibility);
}

bool MCompWindowAnimator::isActive()
{
    return !(timer.state() == QTimeLine::NotRunning);
}

void MCompWindowAnimator::startAnimation()
{
    if (deferred_animation) {
        if (timer.state() == QTimeLine::NotRunning) {
            emit transitionStart();
            timer.start();
        }
    }
}

void MCompWindowAnimator::deferAnimation(bool defer)
{
    deferred_animation = defer;
}

bool MCompWindowAnimator::pendingAnimation() const
{
    return deferred_animation;
}
