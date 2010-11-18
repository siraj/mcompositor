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

#ifndef DUIDECORATORFRAME_H
#define DUIDECORATORFRAME_H

#include <QObject>
#include <QRect>

class MCompositeWindow;
class MRmiClient;
class QRect;

/*!
 * MDecoratorFrame is a singleton class that represents the decorator process
 * which draws the decorations for non-DirectUI applications.
 * This class handles the communication as well to the decorator.
 */
class MDecoratorFrame: public QObject
{
    Q_OBJECT
public:

    /*!
     * Singleton accessor
     */
    static MDecoratorFrame *instance();

    /*!
     * Retuns the window id of the managed window.
     */
    Qt::HANDLE managedWindow() const;
    MCompositeWindow *managedClient() const { return client; }

    /*!
     * Returns the window id of the decorator window.
     */
    Qt::HANDLE winId() const;

    /*!
     * Lowers the decorator window.
     */
    void lower();

    /*!
     * Raises the decorator window.
     */
    void raise();

    /*!
     * Possibly resizes and moves the managed window to match the decorator
     * and to fit to the screen.
     */
    void updateManagedWindowGeometry();

    /*!
     * Sets the managed window.
     */
    void setManagedWindow(MCompositeWindow *cw, bool no_resize = false);

    /*!
     * Sets the automatic rotation mode.
     */
    void setAutoRotation(bool mode);

    /*!
     * Sets the "only statusbar" mode.
     */
    void setOnlyStatusbar(bool mode);

    /*!
     * Show/hide the query dialog.
     */
    void showQueryDialog(bool visible);

    /*!
     * Sets the decorator window and maps that window if it is unmapped.
     */
    void setDecoratorWindow(Qt::HANDLE window);

    void setDecoratorItem(MCompositeWindow *window);

    MCompositeWindow *decoratorItem() const;
    const QRect &decoratorRect() const { return decorator_rect; }

public slots:
    void setDecoratorAvailableRect(const QRect& r);

private slots:
    void destroyDecorator();
    void destroyClient();
    void visualizeDecorator(bool visible);

private:
    explicit MDecoratorFrame(QObject *object = 0);
    static MDecoratorFrame *d;

    MCompositeWindow *client;
    Qt::HANDLE decorator_window;
    MCompositeWindow *decorator_item;
    MRmiClient *remote_decorator;
    int top_offset;
    bool no_resize;
    QRect decorator_rect;
};

#endif // DUIDECORATORFRAME_H
