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


#ifndef MRMISERVER_P_H
#define MRMISERVER_P_H

#include <QVariant>
#include <QLocalServer>
#include <QObject>

class QDataStream;
class MRmiServer;

class MRmiServerPrivate
{
    Q_DECLARE_PUBLIC( MRmiServer )
public:

    MRmiServerPrivate(const QString& key);
    virtual ~MRmiServerPrivate();
    virtual void exportObject(QObject* p);
    QObject* currentObject();
    QString key() const;

    virtual void _q_incoming() = 0;
    virtual void _q_readData() = 0;

    MRmiServer * q_ptr;
private:
    QString _key;
    QObject* _obj;
};

class MRmiServerPrivateSocket: public QObject, public MRmiServerPrivate
{
    Q_OBJECT
    Q_DECLARE_PUBLIC( MRmiServer )

public:
    MRmiServerPrivateSocket(const QString& key);

    virtual void exportObject(QObject* p);

public slots:
    virtual void _q_incoming();
    virtual void _q_readData();

private:
    void invoke(QDataStream&);

    QLocalServer  _serv;
    quint16 method_size;
};

#endif //MRMISERVER_P_H
