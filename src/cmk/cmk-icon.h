/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CMK_ICON_H__
#define __CMK_ICON_H__

#include "cmk-widget.h"
#include <cairo.h>

G_BEGIN_DECLS

#define CMK_TYPE_ICON cmk_icon_get_type()
G_DECLARE_DERIVABLE_TYPE(CmkIcon, cmk_icon, CMK, ICON, CmkWidget);

typedef struct _CmkIconClass CmkIconClass;

struct _CmkIconClass
{
	CmkWidgetClass parentClass;
};

CmkIcon * cmk_icon_new(gfloat size);
CmkIcon * cmk_icon_new_from_name(const gchar *iconName, gfloat size);
CmkIcon * cmk_icon_new_full(const gchar *iconName, const gchar *themeName, gfloat size, gboolean useForeground);

void cmk_icon_set_icon(CmkIcon *icon, const gchar *iconName);
const gchar * cmk_icon_get_icon(CmkIcon *icon);

/*
 * Instead of using a named icon, use a pixel buffer (or animation). For a
 * static image, set frames to 1. Otherwise, buf should contain a sequence
 * of pixmaps in order and frames should be the number of pixmaps. 
 * Note that the pixmap must be a square, of width and height 'size'. This
 * does not need to be the same size as set with cmk_icon_set_size or
 * the CmkIcon constructors; if it is not, the pixmap will be scaled.
 * The stride of the data buffer must be appropriate for the given format
 * and size.
 */
void cmk_icon_set_pixmap(CmkIcon *icon, guchar *data, cairo_format_t format, guint size, guint frames, guint fps);

/*
 * Sets the square size of the icon. This automatically takes into
 * account the widget's scale factor and adjusts the size appropriately.
 */
void cmk_icon_set_size(CmkIcon *icon, gfloat size);
gfloat cmk_icon_get_size(CmkIcon *icon);

/*
 * If the icon is set to use the foreground color, it will mask the entire
 * icon with the current foreground (font) color. This is useful for icons
 * which are solid-colored and should match the current theme, but should
 * not be used on, for example, application icons.
 */
void cmk_icon_set_use_foreground_color(CmkIcon *icon, gboolean useForeground);
gboolean cmk_icon_get_use_foreground_color(CmkIcon *icon);

/*
 * Sets the icon theme to use by name. If set to NULL (default),
 * the icon theme chosen in GSettings will be used.
 */
void cmk_icon_set_icon_theme(CmkIcon *icon, const gchar *themeName);
const gchar * cmk_icon_get_icon_theme(CmkIcon *icon);

G_END_DECLS

#endif
