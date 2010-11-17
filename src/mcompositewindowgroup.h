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

#ifndef MCOMPOSITEWINDOWGROUP_H
#define MCOMPOSITEWINDOWGROUP_H

#include <mcompositewindow.h>

class MTexturePixmapItem;
class MCompositeWindowGroupPrivate;

class MCompositeWindowGroup: public MCompositeWindow
{
    Q_OBJECT        
 public: 
    enum { Type =  UserType + 2 };

    MCompositeWindowGroup(MTexturePixmapItem* mainWindow);
    virtual ~MCompositeWindowGroup();
    
    void addChildWindow(MTexturePixmapItem* window);
    void removeChildWindow(MTexturePixmapItem* window);
    GLuint texture();
    
    //! \reimp  
    virtual void windowRaised();
    virtual void updateWindowPixmap(XRectangle *rects = 0, int num = 0, 
                                    Time = 0);
    virtual void saveBackingStore();
    virtual void resize(int , int);
    virtual void clearTexture();
    virtual bool isDirectRendered() const;
    virtual QRectF boundingRect() const;
    virtual void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*);
    virtual Pixmap windowPixmap() const { return 0; }    
    //! \reimp_end
    
    virtual int	type () const { return Type; }
       
 private slots:
    void q_removeWindow();

 private:
    Q_DECLARE_PRIVATE(MCompositeWindowGroup)       
    void init();
    virtual MTexturePixmapPrivate* renderer() const;
    
    QScopedPointer<MCompositeWindowGroupPrivate> d_ptr;
};

#endif // MCOMPOSITEWINDOWGROUP_H
