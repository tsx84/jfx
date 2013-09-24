/*
 * Copyright (c) 2012, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
 
#include "input/LensInput.h"

#include "utilInternal.h"
#include "platformUtil.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#if defined(OMAP3)

#include <linux/omapfb.h>

typedef struct {
    int fd;
    struct omapfb_plane_info plane;
    int width;
    int height;
    int screenWidth;
    int screenHeight;
    jlong currentCursor;
    jboolean isVisible;
} FBCursor;

typedef struct {
    int width;
    int height;
    int bpp;
    jbyte *buffer;
} FBCursorImage;


FBCursor cursor = { .fd = -1, .width = 0, .height = 0, .currentCursor = 0, .isVisible = 0};

void fbOmapCursorClose() {
    if (cursor.fd >= 0) {
        cursor.plane.enabled = 0;
        if (ioctl(cursor.fd, OMAPFB_SETUP_PLANE, &cursor.plane)) {
            GLASS_LOG_SEVERE("Failed to disable cursor plane");
        }
        close(cursor.fd);
        cursor.fd = -1;
        cursor.isVisible = 0;
    }
}

void fbOmapCreateCursor(jbyte *cursorImage, int width, int height, int bpp) {
    struct fb_var_screeninfo screenInfo;
    cursor.width = width;
    cursor.height = height;

    GLASS_LOG_FINE("open(%s, O_RDWR)", FB_CURSOR_DEVICE);
    cursor.fd = open(FB_CURSOR_DEVICE, O_RDWR);
    if (cursor.fd < 0) {
        GLASS_LOG_SEVERE("Cannot open frame buffer device for cursor");
        return;
    }
    if (ioctl(cursor.fd, FBIOGET_VSCREENINFO, &screenInfo)) {
        GLASS_LOG_SEVERE("Cannot query screen info");
        fbOmapCursorClose();
        return;
    }
    screenInfo.xoffset = 0;
    screenInfo.yoffset = 0;
    screenInfo.xres = screenInfo.xres_virtual = cursor.width;
    screenInfo.yres = screenInfo.yres_virtual = cursor.height;

    if (ioctl(cursor.fd, FBIOPUT_VSCREENINFO, &screenInfo)) {
        GLASS_LOG_SEVERE("Cannot set screen info");
        fbOmapCursorClose();
        return;
    }
    cursor.plane.enabled = 1;
    cursor.plane.out_width = cursor.width;
    cursor.plane.out_height = cursor.height;
    if (ioctl(cursor.fd, OMAPFB_SETUP_PLANE, &cursor.plane)) {
        GLASS_LOG_SEVERE("Cannot set plane info");
        fbOmapCursorClose();
        return;
    }

    if (ioctl(cursor.fd, OMAPFB_QUERY_PLANE, &cursor.plane)) {
        GLASS_LOG_SEVERE("Cannot query plane info");
        fbOmapCursorClose();
        return;
    }

    // Set up the color key
    struct omapfb_color_key color_key;
    if (ioctl(cursor.fd, OMAPFB_GET_COLOR_KEY, &color_key)) {
        GLASS_LOG_SEVERE("Cannot set color key");
        return;
    }

    color_key.key_type = OMAPFB_COLOR_KEY_VID_SRC;
    color_key.trans_key = LENSFB_CURSOR_COLOR_KEY;
    if (ioctl(cursor.fd, OMAPFB_SET_COLOR_KEY, &color_key)) {
        GLASS_LOG_SEVERE("OMAPFB_SET_COLOR_KEY");
        return;
    }

    int cursorSize = cursor.width * cursor.height * bpp;
    if (write(cursor.fd, cursorImage, cursorSize) < cursorSize) {
        GLASS_LOG_SEVERE("Cannot write cursor plane");
        return;
    }
}

jlong fbOmapCreateNativeCursor(JNIEnv *env, jint x, jint y,  jbyte *srcArray, jint width, jint height) {
    FBCursorImage *cursorImage;
    int imageSize = width * height * 4;
    cursorImage = (FBCursorImage *)malloc(sizeof(FBCursorImage) + imageSize);

    cursorImage->width = width;
    cursorImage->height = height;
    cursorImage->bpp = 4;
    cursorImage->buffer = (jbyte *)(cursorImage + 1);

    {
        int i;
        for (i = 0; (i + 3) < imageSize; i += 4) {
            if (srcArray[i + 3] != 0) {
                cursorImage->buffer[i] = srcArray[i];
                cursorImage->buffer[i + 1] = srcArray[i + 1];
                cursorImage->buffer[i + 2] = srcArray[i + 2];
                cursorImage->buffer[i + 3] = srcArray[i + 3];
            } else {
                // 171 == 0xAB of the color key.
                cursorImage->buffer[i] = 171;
                cursorImage->buffer[i + 1] = 171;
                cursorImage->buffer[i + 2] = 171;
                cursorImage->buffer[i + 3] = 171;
            }
        }
    }
    return ptr_to_jlong(cursorImage);
}

void fbOmapCursorInitialize(int screenWidth, int screenHeight) {
    cursor.screenWidth = screenWidth;
    cursor.screenHeight = screenHeight;
}

void fbOmapCursorSetPosition(int x, int y) {

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x > cursor.screenWidth - cursor.width) {
        x = cursor.screenWidth - cursor.width;
    }
    if (y > cursor.screenHeight - cursor.height) {
        y = cursor.screenHeight - cursor.height;
    }
    cursor.plane.enabled = 1;
    cursor.plane.pos_x = x;
    cursor.plane.pos_y = y;
    if (cursor.fd >= 0) {
        if (ioctl(cursor.fd, OMAPFB_SETUP_PLANE, &cursor.plane)) {
            GLASS_LOG_SEVERE("Cannot set plane info to show cursor at %i,%i", x, y);
        }
    }
}

void fbOmapSetNativeCursor(jlong nativeCursorPointer) {
    FBCursorImage *cursorImage = (FBCursorImage *)jlong_to_ptr(nativeCursorPointer);
    if (cursor.currentCursor == nativeCursorPointer) {
        return;
    }

    cursor.currentCursor = nativeCursorPointer;

    if (cursor.isVisible) {
        fbOmapCursorClose();
        fbOmapCreateCursor(cursorImage->buffer, cursorImage->width, cursorImage->height, cursorImage->bpp);
        // reset the visibility - because closing the cursor also makes it 
        // not visible
        cursor.isVisible = 1;
    } 
}

void fbOmapReleaseNativeCursor(jlong nativeCursorPointer) {
    if (nativeCursorPointer != 0) {
        FBCursorImage *cursorImage = (FBCursorImage *)jlong_to_ptr(nativeCursorPointer);
        free(cursorImage);
    }

    if (cursor.currentCursor == nativeCursorPointer) {
        fbCursorClose();
        cursor.currentCursor = 0;
    }
}

void fbOmapSetVisible(jboolean isVisible) {
    if (isVisible) {
        if (!cursor.isVisible && cursor.currentCursor != 0) {
            FBCursorImage *cursorImage = 
                (FBCursorImage *)jlong_to_ptr(cursor.currentCursor);
            fbOmapCreateCursor(cursorImage->buffer, cursorImage->width, 
                               cursorImage->height, cursorImage->bpp);
        }
    } else {
        fbOmapCursorClose();
    }

    cursor.isVisible = isVisible;
}


jboolean fbOmapPlatformCursorTranslucency() {
    return JNI_FALSE;
}

void fbOmapCursorTerminate(void) {
    fbOmapCursorClose();
}

void select_omap_cursor() {
    fbPlatformSetNativeCursor = fbOmapSetNativeCursor;
    fbPlatformCursorInitialize = fbOmapCursorInitialize;
    fbPlatformCursorSetPosition = fbOmapCursorSetPosition;
    fbPlatformCursorClose = fbOmapCursorClose;
    fbPlatformCreateNativeCursor = fbOmapCreateNativeCursor;
    fbPlatformReleaseNativeCursor = fbOmapReleaseNativeCursor;
    fbPlatformSetVisible = fbOmapSetVisible;
    fbPlatformCreateCursor = fbOmapCreateCursor;
}

#else // !OMAP3
void select_omap_cursor() { }
#endif // OMAP3