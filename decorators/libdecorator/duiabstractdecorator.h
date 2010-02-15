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

#ifndef DUIABSTRACTDECORATOR_H
#define DUIABSTRACTDECORATOR_H

#include <QObject>

/*!
 * DuiAbstractDecorator is the base class for window decorators
 */
class DuiAbstractDecorator: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes DuiAbstractDecorator and the connections to DuiCompositor
     */
    DuiAbstractDecorator(QObject *parent = 0);
    virtual ~DuiAbstractDecorator() = 0;

    /*!
     * Returns the id of the window decorated by this decorator
     */
    Qt::HANDLE managedWinId();

public slots:

    /*!
    * Minimizes the managed window
    */
    void minimize();

    /*!
    * Closes the managed window
    */
    void close();

    /*!
     * Interface to DuiRMI sockets
     */
    void RemoteSetManagedWinId(qulonglong window);
    void RemoteActivateWindow();

protected:

    /*!
     * Pure virtual function that gets called when the user activates the
     * managed window by tapping on its client area.
     * Re-implement to obtain activate events from the managed window
     */
    virtual void activateEvent() = 0;

private:
    Qt::HANDLE client;

};

#endif //DUIABSTRACTDECORATOR_H
