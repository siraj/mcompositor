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

#ifndef DUICOMPWINDOWANIMATOR_H
#define DUICOMPWINDOWANIMATOR_H

#include <QObject>
#include <QTimeLine>
#include <QGraphicsItemAnimation>
#include <QTransform>

class QGraphicsItem;
class MCompositeWindow;

/*!
 * This class is responsible for complete control of the animation of
 * MCompositeWindow items. It provides a way to save and restore
 * transformation matrices directly in animations. Some transformation
 * functions are provided which animates the transitions by default.
 * These transitions could be themeable in the future
 */
class MCompWindowAnimator: public QObject
{
    Q_OBJECT

public:

    enum {
        All = 0x0,
        Transformation,
        ZValue,
        InitialPosition,
        Visibility
    };

    MCompWindowAnimator(MCompositeWindow *item);

    //! Restores original item with animation. (TODO: deprecate this!(
    void restore();

    //! translates and scale the item to a new size and
    // from its current position to new position with animation
    //
    // \param reverScale Set to true to reverse the scaling
    void translateScale(qreal fromSx, qreal fromSy, qreal toSx, qreal toSy,
                        const QPointF &newPos, bool reverseScale = false);

    //! Global coordinate save state
    void saveState(int state = 0);

    void restoreState(int state = 0);

    //! Local coordinate save state
    void localSaveState();

    //! Animation is runnning
    bool isActive();

    void startAnimation();
    void deferAnimation(bool);

    //! There is a pending animation to be executed soon
    bool pendingAnimation() const;

public slots:
    /*!
     * Direct interface to timeline. MTexturePixmapItem doesn't support
     * standard QGraphicsItem scale and rotation. So we call this for each
     * item's frame for complete control of the transitions
     */
    void advanceFrame(qreal step);

signals:
    void transitionDone();
    void transitionStart();

private:

    // Item state
    QTransform matrix;
    QTransform local;
    bool visibility;
    QGraphicsItem *item;
    QGraphicsItemAnimation anim;
    QTimeLine timer;
    int zval;
    QPointF initpos;

    // reverse animation
    bool reversed;
    bool deferred_animation;
};

#endif
