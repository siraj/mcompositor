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
#ifndef TEXTUREPIXMAPSHADERS_H
#define TEXTUREPIXMAPSHADERS_H

static const char* TexpVertShaderSource = "\
    attribute highp vec4 inputVertex; \
    attribute lowp  vec2 textureCoord; \
    uniform   highp mat4 matProj; \
    uniform   highp mat4 matWorld; \
    varying   lowp  vec2 fragTexCoord; \
    void main(void) \
    {\
            gl_Position = (matProj * matWorld) * inputVertex;\
            fragTexCoord = textureCoord; \
    }";

static const char* TexpFragShaderSource = "\
    varying lowp vec2 fragTexCoord;\
    uniform sampler2D texture;\
    uniform lowp float opacity;\n\
    void main(void) \
    {\
            gl_FragColor = texture2D(texture, fragTexCoord) * opacity; \
    }";

static const char* TexpCustomShaderSource = "\
    varying highp vec2 fragTexCoord;\n\
    uniform lowp sampler2D texture;\n\
    uniform lowp float opacity;\n\
    void main(void) \n\
    {\n\
            gl_FragColor = customShader(texture, fragTexCoord) * opacity; \n\
    }\n\n";


#if 0
static const char *AlphaTestFragShaderSource = "\
    varying lowp vec2 fragTexCoord;\
    uniform sampler2D texture0;\
    void main(void) \
    {\
        vec4 baseColor = texture2D(texture0, fragTexCoord);\
        lowp float v = baseColor.r + baseColor.g + baseColor.b;   \
        if(v == 0.0)\
            discard;\
        else\
            gl_FragColor = baseColor;\
    }";
#endif

static const char *blurshader = "\
varying lowp vec2 fragTexCoord;\
uniform sampler2D texture0;\
uniform mediump float blurstep;\
void main(void)\
{\
mediump vec4 sample0,\
sample1,\
sample2,\
sample3;\
mediump float step = blurstep / 100.0;\
mediump vec2 upperleft = fragTexCoord.xy - vec2(step);\
mediump vec2 lowerright = fragTexCoord.xy + vec2(step);\
mediump vec2 upperright = fragTexCoord.xy + vec2(step, -step);\
mediump vec2 lowerleft = fragTexCoord.xy + vec2(-step, step);\
sample0 = texture2D(texture0, upperleft); \
sample1 = texture2D(texture0, lowerright); \
sample2 = texture2D(texture0, upperright); \
sample3 = texture2D(texture0, lowerleft); \
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
