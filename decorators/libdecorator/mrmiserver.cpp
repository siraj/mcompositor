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
#include "mrmiserver.h"
#include "mrmiserver_p.h"

#include <QLocalSocket>
#include <QDataStream>
#include <QMetaObject>
#include <QGenericArgument>
#include <QFile>
#include <QTextStream>

QGenericArgument unmarshall(const char*name, const void* data)
{
    return QGenericArgument(name, data);
}

MRmiServerPrivate::MRmiServerPrivate(const QString& key)
        : _key(key), _obj(0)
{
}

MRmiServerPrivate::~MRmiServerPrivate()
{
}

void MRmiServerPrivate::exportObject(QObject* p)
{
    _obj = p;
}

// TODO object selection from multiple sources
QObject* MRmiServerPrivate::currentObject()
{
    return _obj;
}

QString MRmiServerPrivate::key() const
{
    return _key;
}

MRmiServerPrivateSocket::MRmiServerPrivateSocket(const QString& key)
        : MRmiServerPrivate(key), method_size(0)
{
}

void MRmiServerPrivateSocket::exportObject(QObject* p)
{
    Q_Q(MRmiServer);
    MRmiServerPrivate::exportObject(p);

    q->connect(&_serv, SIGNAL(newConnection()), q, SLOT(_q_incoming()));

    if (QFile::exists("/tmp/" + key()))
        QFile::remove("/tmp/" + key());

    if (!_serv.listen(key()))
        qDebug() << "MRmiServerPrivateSocket" << "system error, can't listen to local socket";
}

void MRmiServerPrivateSocket::_q_incoming()
{
    Q_Q(MRmiServer);
    QLocalSocket* s = _serv.nextPendingConnection();
    q->connect(s, SIGNAL(disconnected()), s, SLOT(deleteLater()));
    if (!s)
        return;
    q->connect(s, SIGNAL(readyRead()), this, SLOT(_q_readData()));
}

void MRmiServerPrivateSocket::_q_readData()
{
    QLocalSocket* socket = (QLocalSocket*) sender();
    uint sz = socket->bytesAvailable();

    QDataStream stream(socket);
    stream.setVersion(QDataStream::Qt_4_0);

    if (method_size == 0) {
        if (sz < (int)sizeof(quint16))
            return;
        stream >> method_size;
    }

    if (sz < method_size)
        return;

    invoke(stream);
}

void MRmiServerPrivateSocket::invoke(QDataStream& stream)
{
    char* className   = 0;
    char* methodName  = 0;
    quint16 arglength = 0;

    QVariant arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9;

    stream >> arglength >> className >> methodName;
    switch (arglength) {
    case 0:
        QMetaObject::invokeMethod(currentObject(),
                                  methodName);
        break;
    case 1:
        stream >> arg0;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()));

        break;
    case 2:
        stream >> arg0 >> arg1;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()));
        break;
    case 3:
        stream >> arg0 >> arg1 >> arg2;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()));
        break;
    case 4:
        stream >> arg0 >> arg1 >> arg2 >> arg3;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()));
        break;
    case 5:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()));
        break;
    case 6:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4 >> arg5;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()),
                                  unmarshall(arg5.typeName(),
                                             arg5.data()));
        break;
    case 7:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4 >> arg5 >> arg6;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()),
                                  unmarshall(arg5.typeName(),
                                             arg5.data()),
                                  unmarshall(arg6.typeName(),
                                             arg6.data()));
        break;
    case 8:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4 >> arg5 >> arg6 >> arg7;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()),
                                  unmarshall(arg5.typeName(),
                                             arg5.data()),
                                  unmarshall(arg6.typeName(),
                                             arg6.data()),
                                  unmarshall(arg7.typeName(),
                                             arg7.data()));
        break;
    case 9:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4 >> arg5 >> arg6 >> arg7
        >> arg8;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()),
                                  unmarshall(arg5.typeName(),
                                             arg5.data()),
                                  unmarshall(arg6.typeName(),
                                             arg6.data()),
                                  unmarshall(arg7.typeName(),
                                             arg7.data()),
                                  unmarshall(arg8.typeName(),
                                             arg8.data()));
        break;
    case 10:
        stream >> arg0 >> arg1 >> arg2 >> arg3 >> arg4 >> arg5 >> arg6 >> arg7
        >> arg8 >> arg9;
        QMetaObject::invokeMethod(currentObject(),
                                  methodName,
                                  unmarshall(arg0.typeName(),
                                             arg0.data()),
                                  unmarshall(arg1.typeName(),
                                             arg1.data()),
                                  unmarshall(arg2.typeName(),
                                             arg2.data()),
                                  unmarshall(arg3.typeName(),
                                             arg3.data()),
                                  unmarshall(arg4.typeName(),
                                             arg4.data()),
                                  unmarshall(arg5.typeName(),
                                             arg5.data()),
                                  unmarshall(arg6.typeName(),
                                             arg6.data()),
                                  unmarshall(arg7.typeName(),
                                             arg7.data()),
                                  unmarshall(arg8.typeName(),
                                             arg8.data()),
                                  unmarshall(arg9.typeName(),
                                             arg9.data()));
        break;
    default:
        break;

    }

    delete[] className;
    delete[] methodName;
    method_size = 0;
}


MRmiServer::MRmiServer(const QString& key, QObject* p)
        : QObject(p),
        d_ptr(new MRmiServerPrivateSocket(key))
{
	d_ptr->q_ptr = this;
}


MRmiServer::~MRmiServer()
{
    delete d_ptr;
}


void MRmiServer::exportObject(QObject* obj)
{
    Q_D(MRmiServer);
    d->exportObject(obj);
}

#include "moc_mrmiserver.cpp"

