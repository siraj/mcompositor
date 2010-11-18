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

#ifndef MABSTRACTDECORATOR_H
#define MABSTRACTDECORATOR_H

#include <QObject>

class MAbstractDecoratorPrivate;
class MRmiClient;
class QRect;

/*!
 * MAbstractDecorator is the base class for window decorators
 */
class MAbstractDecorator: public QObject
{
    Q_OBJECT
public:
    /*!
     * Initializes MAbstractDecorator and the connections to MCompositor
     */
    MAbstractDecorator(QObject *parent = 0);
    virtual ~MAbstractDecorator() = 0;

    /*!
     * Returns the id of the window decorated by this decorator
     */
    Qt::HANDLE managedWinId();
    
    /*!
     * Informs the compositor of the available client geometry when client
     * is managed by the decorator. 
     *
     * \param rect is the geometry of the decorator area.
     */ 
    void setAvailableGeometry(const QRect& rect);

    /*!
     * Informs the compositor what was the answer to the query dialog.
     *
     * \param window managed window that the answer concerns.
     * \param yes_answer true if the answer was 'yes'.
     */ 
    void queryDialogAnswer(unsigned int window, bool yes_answer);

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
     * Interface to MRMI sockets
     */
    void RemoteSetManagedWinId(qulonglong window);
    void RemoteActivateWindow();
    void RemoteSetAutoRotation(bool mode);
    void RemoteSetClientGeometry(const QRect& rect);
    void RemoteSetOnlyStatusbar(bool mode);
    void RemoteShowQueryDialog(bool visible);

protected:

    /*!
     * Pure virtual function that gets called when the user activates the
     * managed window by tapping on its client area.
     * Re-implement to obtain activate events from the managed window
     */
    virtual void activateEvent() = 0;
    
     /*!
      * Pure virtual function that gets called this decorator manages a window
      */
    virtual void manageEvent(Qt::HANDLE window) = 0;

     /*!
      * Pure virtual function to set automatic rotation mode.
      */
    virtual void setAutoRotation(bool mode) = 0;

     /*!
      * Pure virtual function to set "only statusbar" mode.
      */
    virtual void setOnlyStatusbar(bool mode) = 0;

     /*!
      * Pure virtual function to show the "not responding" query dialog.
      */
    virtual void showQueryDialog(bool visible) = 0;

private:
    
    Q_DECLARE_PRIVATE(MAbstractDecorator)
        
    MAbstractDecoratorPrivate * const d_ptr;
};

#endif //MABSTRACTDECORATOR_H
