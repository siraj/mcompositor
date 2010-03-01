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

#ifndef DUITEXTUREPIXMAPITEM_H
#define DUITEXTUREPIXMAPITEM_H

#include "duicompositewindow.h"

#include <QtOpenGL>
#include <QPixmap>
#include <X11/Xutil.h>

class DuiTexturePixmapPrivate;

/*!
 * DuiTexturePixmapItem represents an offscreen pixmap that is rendered
 * directly as a texture. The texture is drawn as an item in a
 * QGraphicsView directly using standard OpenGL / OpenGL ES2 calls without
 * the use of QPainter.
 *
 * If a GLX or EGL surface that supports render to texture is not found, the
 * renderer defaults to its own custom texture to pixmap routines
 *
 */
class DuiTexturePixmapItem: public DuiCompositeWindow
{
public:
    /*!
     * Constructs a DuiTexturePixmapItem
     *
     * \param w the redirected window where the texture is derived.
     * \param enableAlpha if the alpha component of window is rendered
     * \param parent QGraphicsItem, defaults to 0
     */
    DuiTexturePixmapItem(Window window, QGLWidget *glwidget, QGraphicsItem *parent = 0);
    /*!
     * Destroys the DuiTexturePixmapItem and frees the allocated
     * surface and texture
     */
    virtual ~DuiTexturePixmapItem();

    /*!
     * Ensures that the corresponding texture reflects the contents of the
     * associated pixmap and schedules a redraw of this item.
     */
    void updateWindowPixmap(XRectangle *rects = 0, int num = 0);

    /*!
     * Creates the pixmap id and saves the offscreen buffer that represents
     * this window
     *
     * \param renew Set to true if the window was just mapped or resized. This
     * will update the offscreen backing store.
     */
    void saveBackingStore(bool renew = false);

    /*!
      Clears the texture that is associated with the offscreen pixmap
     */
    void clearTexture();

    /*!
      Returns true if the window corresponding to the offscreen pixmap
      is rendering directly to the framebuffer, otherwise return false.
     */
    bool isDirectRendered() const;

    /*!
      Returns true if the window corresponding to the offscreen pixmap has an
      alpha channel, otherwise returns false.
     */
    bool hasAlpha() const;

    /*!
     Returns true if the window corresponding to the offscreen pixmap has an
     is an override-redirect window, otherwise returns false.
    */
    bool isOverrideRedirect() const;

    /*!
     * Sets the width and height if the item
     */
    void resize(int w, int h);

    void enableDirectFbRendering();
    void enableRedirectedRendering();

protected:
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget);

    QSizeF sizeHint(Qt::SizeHint, const QSizeF &) const;
    QRectF boundingRect() const;
    QPainterPath shape() const;
    virtual void windowRaised();

private:
    void init();
    void initCustomTfp();
    void cleanup();
    void rebindPixmap();

    DuiTexturePixmapPrivate *const d;
    friend class DuiTexturePixmapPrivate;
    friend class DuiCompositeManagerPrivate;
};

#endif // DUIGLXTEXTUREPIXMAPITEM_H
