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
#include "mrmiclient_p.h"
#include "mrmiclient.h"
#include <QDataStream>
#include <QByteArray>
#include <MDebug>

MRmiClientPrivate:: MRmiClientPrivate(const QString& key)
        : _key(key)
{
}

MRmiClientPrivate::~MRmiClientPrivate()
{
}

void MRmiClientPrivate::initConnection()
{
    //unused
}

void MRmiClientPrivate::finalizeConnection()
{
    // unused
}

QString MRmiClientPrivate::key() const
{
    return _key;
}

MRmiClientPrivateSocket::MRmiClientPrivateSocket(const QString& key, MRmiClient *q)
        : MRmiClientPrivate(key),
        return_sz(0)
{
    q_ptr = q;
    QObject::connect(&_socket, SIGNAL(readyRead()), q_ptr, SLOT(_q_readyRead()));
}


void MRmiClientPrivateSocket::initConnection()
{
    _buf.clear();
    _stream = new QDataStream(&_buf, QIODevice::WriteOnly);
    _stream->setVersion(QDataStream::Qt_4_0);

    connectToServer();
    if (!_socket.waitForConnected())
        mDebug("MRmiClientPrivateSocket") << _socket.errorString() << key();
}

void MRmiClientPrivateSocket::finalizeConnection()
{
    uint sz = _buf.size();
    mDebug("MRmiClientPrivateSocket") << sz;
    _stream->device()->seek(0);
    *_stream << (quint16)(sz - sizeof(quint16));

    if (_socket.state() == QLocalSocket::ConnectedState) {
        writeData(_buf);
    }

    _socket.close();
    delete _stream;
}

QDataStream& MRmiClientPrivateSocket::stream()
{
    return *_stream;
}

void MRmiClientPrivateSocket::writeData(const QByteArray& buffer)
{
    _socket.write(buffer);
    _socket.waitForBytesWritten();
    // _socket.disconnect();
}

void MRmiClientPrivateSocket::connectToServer()
{
    if (_socket.state() == QLocalSocket::UnconnectedState)
        _socket.connectToServer(key());
}

void MRmiClientPrivateSocket::_q_readyRead()
{
    Q_Q(MRmiClient);
    QDataStream stream(&_socket);
    stream.setVersion(QDataStream::Qt_4_0);

    if (return_sz == 0) {
        if (_socket.bytesAvailable() < (int)sizeof(quint16))
            return;
        stream >> return_sz;
    }

    if (_socket.bytesAvailable() < return_sz)
        return;

    QVariant v;
    stream >> v;
    emit q->returnValue(v);
}

MRmiClient::MRmiClient(const QString& key, QObject* p)
        : QObject(p),
        d_ptr(new MRmiClientPrivateSocket(key, this))
{
}
MRmiClient::~MRmiClient()
{
    delete d_ptr;
}

void MRmiClient::initConnection()
{
    Q_D(MRmiClient);
    d->initConnection();
}

void MRmiClient::finalizeConnection()
{
    Q_D(MRmiClient);
    d->finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method)
{
    Q_D(MRmiClient);
    initConnection();

    /* packet is composed of |block size|argument length|arguments...| */
    d->stream() << (quint16)0 << (quint16)0 << objectName << method;

    finalizeConnection();
}


void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)1 << objectName << method << arg0;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0, const QVariant& arg1)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)2 << objectName << method << arg0
    << arg1;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)3 << objectName << method << arg0
    << arg1 << arg2;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)4 << objectName << method << arg0
    << arg1 << arg2 << arg3;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)5 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4,   const QVariant& arg5)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)6 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4 << arg5;
    finalizeConnection();

}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4,   const QVariant& arg5,
                          const QVariant& arg6)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)7 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4 << arg5 << arg6;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4,   const QVariant& arg5,
                          const QVariant& arg6,   const QVariant& arg7)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)8 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4 << arg5 << arg6 << arg7;
    finalizeConnection();

}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4,   const QVariant& arg5,
                          const QVariant& arg6,   const QVariant& arg7,
                          const QVariant& arg8)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)9 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4 << arg5 << arg6 << arg7 << arg8;
    finalizeConnection();
}

void MRmiClient::invoke(const char* objectName, const char* method,
                          const QVariant& arg0,   const QVariant& arg1,
                          const QVariant& arg2,   const QVariant& arg3,
                          const QVariant& arg4,   const QVariant& arg5,
                          const QVariant& arg6,   const QVariant& arg7,
                          const QVariant& arg8,   const QVariant& arg9)
{
    Q_D(MRmiClient);
    initConnection();
    d->stream() << (quint16)0 << (quint16)10 << objectName << method << arg0
    << arg1 << arg2 << arg3 << arg4 << arg5 << arg6 << arg7 << arg8
    << arg9;
    finalizeConnection();
}

#include "moc_mrmiclient.cpp"

