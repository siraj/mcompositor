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
#ifndef TEXTUREPIXMAPSHADERS_H
#define TEXTUREPIXMAPSHADERS_H

static const char *AlphaTestFragShaderSource = "\
    precision lowp float;\
    varying lowp vec2 fragTexCoord;\
    uniform sampler2D texture0;\
    void main(void) \
    {\
        vec4 baseColor = texture2D(texture0, fragTexCoord);\
        float v = baseColor.r + baseColor.g + baseColor.b;   \
        if(v == 0.0)\
            discard;\
        else\
            gl_FragColor = baseColor;\
    }";

static const char *blurshader = "\
precision mediump float;\
varying lowp vec2 fragTexCoord;\
uniform sampler2D texture0;\
uniform float blurstep;\
void main(void)\
{\
vec4 sample0,\
sample1,\
sample2,\
sample3;\
float step = blurstep / 100.0;\
sample0 = texture2D(texture0, vec2(fragTexCoord.x - step,\
                                   fragTexCoord.y - step));\
sample1 = texture2D(texture0, vec2(fragTexCoord.x + step,\
                                   fragTexCoord.y + step));\
sample2 = texture2D(texture0, vec2(fragTexCoord.x + step,\
                                   fragTexCoord.y - step));\
sample3 = texture2D(texture0, vec2(fragTexCoord.x - step,\
                                   fragTexCoord.y + step));\
gl_FragColor = ((sample0 + sample1 + sample2 + sample3) / 4.0) * 0.5;\
}";


/* static const char* AlphaTestFragShaderSource = "\ */
/*     precision lowp float;\ */
/*     varying lowp vec2 fragTexCoord;\ */
/*     uniform sampler2D texture0;\ */
/*     void main(void) \ */
/*     {\ */
/*               vec4 baseColor = texture2D(texture0, fragTexCoord);   \ */
/*                if(baseColor.r == 0.0 &&\ */
/*                   baseColor.g == 0.0 &&\ */
/*                   baseColor.b == 0.0)\ */
/*                   discard;\ */
/*                else\ */
/*                   gl_FragColor = baseColor;\ */
/*     }"; */


#endif
