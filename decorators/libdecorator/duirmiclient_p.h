/* * This file is part of libdui *
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 * All rights reserved.
 *
 * Contact: Tomas Junnonen <tomas.junnonen@nokia.com>
 *
 * This software, including documentation, is protected by copyright
 * controlled by Nokia Corporation. All rights are reserved. Copying,
 * including reproducing, storing, adapting or translating, any or all of
 * this material requires the prior written consent of Nokia Corporation.
 * This material also contains confidential information which may not be
 * disclosed to others without the prior written consent of Nokia.
 */
#ifndef DUIRMICLIENT_P_H
#define DUIRMICLIENT_P_H

#include <QLocalSocket>
#include <QByteArray>

class QDataStream;
class DuiRmiClient;

class DuiRmiClientPrivate
{
    Q_DECLARE_PUBLIC(DuiRmiClient)
public:
    DuiRmiClientPrivate(const QString& key);
	virtual ~DuiRmiClientPrivate();
    virtual void writeData(const QByteArray&) = 0;
    virtual void connectToServer() = 0;
    virtual QDataStream& stream() = 0;

    virtual void initConnection();
    virtual void finalizeConnection();

    QString key() const;

    void returnValue(const QVariant& v);

    DuiRmiClient * q_ptr;

    virtual void _q_readyRead() = 0;
private:
    QString _key;
};

class DuiRmiClientPrivateSocket: public DuiRmiClientPrivate
{
    Q_DECLARE_PUBLIC(DuiRmiClient)
public:

    DuiRmiClientPrivateSocket(const QString& key, DuiRmiClient *q);

    void writeData(const QByteArray&);
    void connectToServer();
    void initConnection();
    void finalizeConnection();
    QDataStream& stream();

    virtual void _q_readyRead();

private:
    QLocalSocket _socket;
    QByteArray   _buf;
    QDataStream* _stream;
    quint16 return_sz;
};

#endif //DUIRMICLIENT_P_H
