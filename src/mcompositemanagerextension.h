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

#ifndef MCOMPOSITEMANAGEREXTENSION_H
#define MCOMPOSITEMANAGEREXTENSION_H

#include <QObject>
#include <X11/Xlib.h>

class MCompositeManagerPrivate;
class MCompositeWindow;

class MCompositeManagerExtension: public QObject
{
    Q_OBJECT

 public:
    MCompositeManagerExtension(QObject *parent = 0);
    ~MCompositeManagerExtension();

    void testFn();
    
    /*
     * Returns the desktop window
     */
    Qt::HANDLE desktopWindow();

    /*
     * Returns the current application window
     */
    Qt::HANDLE currentAppWindow();
    
    /*! 
     * Informs the composite manager that this extension is subscribed
     * to an XEvent type and requests delivery of only the registered event to
     * prevent it from being spammed by unwanted events. 
     * Note: Do not combine the types in the same way as the
     * event mask in XSelectInput. XEvent type is not a mask enum type and will 
     * not work. Call this function once for each XEvent type you need to 
     * register with.
     *
     * \param XEventType Specifies the XEvent type that this extension is 
     * interested in listening
     */
    void listenXEventType(long XEventType);

 signals:    
    void currentAppChanged(Qt::HANDLE window);
    
 protected:
    /*!
     * Special event handler to receive native X11 events passed in the event
     * parameter. Return true to stop the composite manager from handling
     * the event, otherwise return false to try forwarding the native event to 
     * the composite manager. 
     * 
     * If there are other extensions that are subscribed to the same event and 
     * this function returns true, the event will still be blocked from 
     * propagating to the composite manager even if this function 
     * returns false. Be careful when returning true when there are other 
     * extensions around and only use as a last resort to reimplement core 
     * functionality.
     */
    virtual bool x11Event ( XEvent * event ) = 0;
    
 private slots:
    void q_currentAppChanged(Window window);

 private:
    friend class MCompositeManagerPrivate;
};

#endif
