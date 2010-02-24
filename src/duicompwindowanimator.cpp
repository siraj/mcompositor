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

#include "duicompwindowanimator.h"
#include "duicompositewindow.h"
#include "duicompositemanager.h"
#include "duicompositemanager_p.h"

static int Fps = 60;

static qreal interpolate(qreal step, qreal x1, qreal x2)
{
    return ((x2 - x1) * step) + x1;
}

DuiCompWindowAnimator::DuiCompWindowAnimator(DuiCompositeWindow *item)
    : QObject(item),
      timer(200),
      reversed(false),
      deferred_animation(false)
{
    this->item = item;
    timer.setCurveShape(QTimeLine::EaseInCurve);
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
void DuiCompWindowAnimator::restore()
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
void DuiCompWindowAnimator::advanceFrame(qreal step)
{
    //item->setTransform(QTransform(anim.matrixAt(step)) );
    item->setTransform(matrix);

    item->scale(anim.horizontalScaleAt(step),
                anim.verticalScaleAt(step));
    item->setPos(anim.posAt(step));

    // TODO: move calculation to GPU to imrpove speed
    item->setOpacity(!reversed ? interpolate(step, 1.0, 0.1) :
                     interpolate(step, 0.1, 1.0));

    DuiCompositeManager *p = (DuiCompositeManager *) qApp;
    p->d->glwidget->update();
}

// translate item + scale
void DuiCompWindowAnimator::translateScale(qreal fromSx, qreal fromSy,
        qreal toSx, qreal toSy,
        const QPointF &newPos,
        bool reverse)
{
    reversed = reverse;

    if (!reverse) {
        anim.setScaleAt(0, fromSx, fromSy);
        anim.setScaleAt(1.0, toSx, toSy);
        anim.setPosAt(0, item->pos());
        anim.setPosAt(1.0, newPos);
    } else {
        if(item->transform().m22() == 1.0 && item->transform().m11() == 1.0)
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
void DuiCompWindowAnimator::localSaveState()
{
    local = item->transform();
}

void DuiCompWindowAnimator::saveState(int states)
{
    if ((states & DuiCompWindowAnimator::Transformation) ||
            (states & DuiCompWindowAnimator::All))
        matrix     = item->transform();

    if ((states & DuiCompWindowAnimator::ZValue) ||
            (states & DuiCompWindowAnimator::All))
        zval       = item->zValue();

    if ((states & DuiCompWindowAnimator::InitialPosition) ||
            (states & DuiCompWindowAnimator::All))
        initpos    = item->pos();

    if ((states & DuiCompWindowAnimator::Visibility) ||
            (states & DuiCompWindowAnimator::All))
        visibility = item->isVisible();
}

void DuiCompWindowAnimator::restoreState(int states)
{
    if ((states & DuiCompWindowAnimator::Transformation) ||
            (states & DuiCompWindowAnimator::All))
        item->setTransform(matrix);

    if ((states & DuiCompWindowAnimator::ZValue) ||
            (states & DuiCompWindowAnimator::All))
        item->setZValue(zval);

    if ((states & DuiCompWindowAnimator::InitialPosition) ||
            (states & DuiCompWindowAnimator::All))
        item->setPos(initpos);

    if ((states & DuiCompWindowAnimator::Visibility) ||
            (states & DuiCompWindowAnimator::All))
        item->setVisible(visibility);
}

bool DuiCompWindowAnimator::isActive()
{
    return !(timer.state() == QTimeLine::NotRunning);
}

void DuiCompWindowAnimator::startAnimation()
{
    if (deferred_animation) {
        if (timer.state() == QTimeLine::NotRunning) {
            emit transitionStart();
            timer.start();
        }
    }
}

void DuiCompWindowAnimator::deferAnimation(bool defer)
{
    deferred_animation = defer;
}

bool DuiCompWindowAnimator::pendingAnimation() const
{
    return deferred_animation;
}
