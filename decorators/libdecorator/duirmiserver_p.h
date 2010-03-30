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
#ifndef DUIRMISERVER_P_H
#define DUIRMISERVER_P_H

#include <QVariant>
#include <QLocalServer>

class QDataStream;

class DuiRmiServerPrivate
{
    Q_DECLARE_PUBLIC( DuiRmiServer )
public:

    DuiRmiServerPrivate(const QString& key);
	virtual ~DuiRmiServerPrivate();
    virtual void exportObject(QObject* p);
    QObject* currentObject();
    QString key() const;

    virtual void _q_incoming() = 0;
    virtual void _q_readData() = 0;

    DuiRmiServer * q_ptr;
private:
    QString _key;
    QObject* _obj;
};

class DuiRmiServerPrivateSocket: public DuiRmiServerPrivate
{
    Q_DECLARE_PUBLIC( DuiRmiServer )

public:
    DuiRmiServerPrivateSocket(const QString& key);

    virtual void exportObject(QObject* p);

    virtual void _q_incoming();
    virtual void _q_readData();

private:
    void invoke(QDataStream&);

    QLocalServer  _serv;
    QLocalSocket* _sock;
    quint16 method_size;
};

#endif //DUIRMISERVER_P_H
