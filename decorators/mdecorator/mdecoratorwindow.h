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
#ifndef MDECORATORWINDOW_H
#define MDECORATORWINDOW_H

#include <MWindow>

#include <X11/Xlib.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <QObject>

class MSceneManager;

class MDecoratorWindow : public MWindow
{
    Q_OBJECT

public:
    explicit MDecoratorWindow(QWidget *parent = 0);
    virtual ~MDecoratorWindow();

    void init(MSceneManager &sceneManager);

    /*!
     * \brief Sets the region of the window that can receive input events.
     *
     * Input events landing on the area outside this region will fall directly
     * to the windows below.
     */
    void setInputRegion(const QRegion &region);

signals:

    void homeClicked();
    void escapeClicked();
    void windowTitleChanged(const QString& title);
    
private:
    void fillXRectangle(XRectangle *xRect, const QRect &rect) const;
    void setSceneSize(MSceneManager &sceneManager);
    void setMDecoratorWindowProperty();

    Q_DISABLE_COPY(MDecoratorWindow);
};

#endif