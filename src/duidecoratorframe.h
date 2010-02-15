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

#ifndef DUIDECORATORFRAME_H
#define DUIDECORATORFRAME_H

#include <QObject>

class DuiCompositeWindow;
class DuiRmiClient;

/*!
 * DuiDecoratorFrame is a singleton class that represents the decorator process
 * which draws the decorations for non-DirectUI applications.
 * This class handles the communication as well to the decorator.
 */
class DuiDecoratorFrame: public QObject
{
    Q_OBJECT
public:

    /*!
     * Singleton accessor
     */
    static DuiDecoratorFrame *instance();

    /*!
     * Retuns the window id of the managed window.
     */
    Qt::HANDLE managedWindow() const;

    /*!
     * Returns the window id of the decorator window.
     */
    Qt::HANDLE winId() const;

    /*!
     * Activates the window
     */
    void activate();

    /*!
     * Lowers the decorator window.
     */
    void lower();

    /*!
     * Raises the decorator window.
     */
    void raise();

    /*!
     * Sets the managed window.
     */
    void setManagedWindow(Qt::HANDLE window);

    /*!
     * Sets the decorator window and maps that window if it is unmapped.
     */
    void setDecoratorWindow(Qt::HANDLE window);

    void setDecoratorItem(DuiCompositeWindow *window);

    DuiCompositeWindow *decoratorItem() const;

private slots:
    void destroyDecorator();
    void destroyClient();
    void visualizeDecorator(bool visible);

private:
    explicit DuiDecoratorFrame(QObject *object = 0);
    static DuiDecoratorFrame *d;

    Qt::HANDLE client;
    Qt::HANDLE decorator_window;
    DuiCompositeWindow *decorator_item;
    DuiRmiClient *remote_decorator;
};

#endif // DUIDECORATORFRAME_H
