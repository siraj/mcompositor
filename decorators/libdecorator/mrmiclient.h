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
// -*- C++ -*-
#ifndef MRMICLIENT_H
#define MRMICLIENT_H

#include <QVariant>
#include <QObject>
#include <mexport.h>

class MRmiClientPrivate;
class MRmiClientPrivateSocket;

/*!
 * \class MRmiClient
 *
 * \brief The MRmiClient allows member functions of QObjects exported by
 * MRmiServer from another process to be invoked remotely.
 */
class M_EXPORT MRmiClient: public QObject
{
    Q_OBJECT

public:

    /*!
     * Creates a MRmiClient
     *
     * \param key the key used to identify the remote MRmiServer
     * \param parent the parent QObject
     */
    explicit MRmiClient(const QString& key, QObject* parent  = 0);

    /*!
     * Disconnects all connections and destroys this object
     */
    virtual ~MRmiClient();

    /*!
     * Invoked the remote function
     *
     * \param objectName the name of the remote object. Currently unused
     * \param method literal string of the method name of the remote object
     * \param argN QVariant representation of the arguments of the remote method
     */
    void invoke(const char* objectName, const char* method);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0,   const QVariant& arg1);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0,   const QVariant& arg1,
                const QVariant& arg2);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0,  const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0, const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3,
                const QVariant& arg4);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0,   const QVariant& arg,
                const QVariant& arg2, const QVariant& arg3,
                const QVariant& arg4, const QVariant& arg5);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0, const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3,
                const QVariant& arg4, const QVariant& arg5,
                const QVariant& arg6);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0, const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3,
                const QVariant& arg4,   const QVariant& arg5,
                const QVariant& arg6,   const QVariant& arg7);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0, const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3,
                const QVariant& arg4, const QVariant& arg5,
                const QVariant& arg6,   const QVariant& arg7,
                const QVariant& arg8);
    void invoke(const char* objectName, const char* method,
                const QVariant& arg0, const QVariant& arg1,
                const QVariant& arg2,   const QVariant& arg3,
                const QVariant& arg4,   const QVariant& arg5,
                const QVariant& arg6,   const QVariant& arg7,
                const QVariant& arg8,   const QVariant& arg9);

Q_SIGNALS:
    /*!
     * Signal emitted when a remote function has a return value expected
     *
     * \param arg QVariant representation of the data returned.
     */
    void returnValue(const QVariant& arg);

private:
    Q_DISABLE_COPY(MRmiClient)
    Q_DECLARE_PRIVATE(MRmiClient)

    void initConnection();
    void finalizeConnection();

    MRmiClientPrivate * const d_ptr;
    friend class MRmiClientPrivateSocket;

    Q_PRIVATE_SLOT(d_func(), void _q_readyRead())
};

#endif // QRMICLIENT_H
