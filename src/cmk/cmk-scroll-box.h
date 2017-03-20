/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CMK_SCROLL_BOX_H__
#define __CMK_SCROLL_BOX_H__

#include "cmk-widget.h"

G_BEGIN_DECLS

#define CMK_TYPE_SCROLL_BOX cmk_scroll_box_get_type()
G_DECLARE_DERIVABLE_TYPE(CmkScrollBox, cmk_scroll_box, CMK, SCROLL_BOX, CmkWidget);

typedef struct _CmkScrollBoxClass CmkScrollBoxClass;

struct _CmkScrollBoxClass
{
	CmkWidgetClass parentClass;
};

CmkScrollBox * cmk_scroll_box_new(ClutterScrollMode scrollMode);

G_END_DECLS

#endif
