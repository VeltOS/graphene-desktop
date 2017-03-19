/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "cmk-scroll-box.h"

typedef struct _CmkScrollBoxPrivate CmkScrollBoxPrivate;
struct _CmkScrollBoxPrivate
{
	ClutterScrollMode scrollMode;
	ClutterPoint scroll;
};

enum
{
	PROP_SCROLL_MODE = 1,
	PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void cmk_scroll_box_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void cmk_scroll_box_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static gboolean on_scroll(ClutterActor *self_, ClutterScrollEvent *event);
static void on_key_focus_changed(CmkWidget *self, ClutterActor *newfocus);

G_DEFINE_TYPE_WITH_PRIVATE(CmkScrollBox, cmk_scroll_box, CMK_TYPE_WIDGET);
#define PRIVATE(self) ((CmkScrollBoxPrivate *)cmk_scroll_box_get_instance_private(self))

CmkScrollBox * cmk_scroll_box_new(ClutterScrollMode scrollMode)
{
	return CMK_SCROLL_BOX(g_object_new(CMK_TYPE_SCROLL_BOX, "scroll-mode", scrollMode, NULL));
}

static void cmk_scroll_box_class_init(CmkScrollBoxClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->get_property = cmk_scroll_box_get_property;
	base->set_property = cmk_scroll_box_set_property;

	CLUTTER_ACTOR_CLASS(class)->scroll_event = on_scroll;
	CMK_WIDGET_CLASS(class)->key_focus_changed = on_key_focus_changed;
	
	properties[PROP_SCROLL_MODE] = g_param_spec_flags("scroll-mode", "scroll mode", "scrolling mode", CLUTTER_TYPE_SCROLL_MODE, CLUTTER_SCROLL_BOTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	
	g_object_class_install_properties(base, PROP_LAST, properties);
}

static void cmk_scroll_box_init(CmkScrollBox *self)
{
	clutter_actor_set_reactive(CLUTTER_ACTOR(self), TRUE);
	clutter_actor_set_clip_to_allocation(CLUTTER_ACTOR(self), TRUE);
	clutter_point_init(&(PRIVATE(self)->scroll), 0.0f, 0.0f);
}

static void cmk_scroll_box_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	switch(propertyId)
	{
	case PROP_SCROLL_MODE:
		g_value_set_flags(value, PRIVATE(CMK_SCROLL_BOX(self_))->scrollMode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void cmk_scroll_box_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	switch(propertyId)
	{
	case PROP_SCROLL_MODE:
		PRIVATE(CMK_SCROLL_BOX(self_))->scrollMode = g_value_get_flags(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void scroll_to(CmkScrollBox *self, const ClutterPoint *point, gboolean exact)
{
	CmkScrollBoxPrivate *private = PRIVATE(self);

	gfloat width = clutter_actor_get_width(CLUTTER_ACTOR(self));
	gfloat height = clutter_actor_get_height(CLUTTER_ACTOR(self));
	
	ClutterPoint new = *point;

	if(!exact)
	{
		// Don't scroll anywhere if the requested point is already in view
		if(new.x >= private->scroll.x && new.x <= private->scroll.x + height)
			new.x = private->scroll.x;
		if(new.y >= private->scroll.y && new.y <= private->scroll.y + height)
			new.y = private->scroll.y;
	}

	if(clutter_point_equals(&private->scroll, &new))
		return;

	gfloat minW, natW, minH, natH;
	clutter_actor_get_preferred_size(CLUTTER_ACTOR(self), &minW, &natW, &minH, &natH); 
	gfloat maxScrollW = MAX(natW - width, 0);
	gfloat maxScrollH = MAX(natH - height, 0);
	new.x = MIN(MAX(0, new.x), maxScrollW);
	new.y = MIN(MAX(0, new.y), maxScrollH);

	if(clutter_point_equals(&private->scroll, &new))
		return;

	private->scroll = new;
	g_message("%f, %f", new.x, new.y);

	ClutterMatrix transform;
	clutter_matrix_init_identity(&transform);
	cogl_matrix_translate(&transform,
		(private->scrollMode & CLUTTER_SCROLL_HORIZONTALLY) ? -new.x : 0.0f,
		(private->scrollMode & CLUTTER_SCROLL_VERTICALLY)   ? -new.y : 0.0f,
		0.0f);
	clutter_actor_set_child_transform(CLUTTER_ACTOR(self), &transform);
}

static inline void scroll_by(CmkScrollBox *self, gdouble dx, gdouble dy)
{
	ClutterPoint point;
	point.x = PRIVATE(self)->scroll.x + dx;
	point.y = PRIVATE(self)->scroll.y + dy;
	scroll_to(self, &point, TRUE);
}

static gboolean on_scroll(ClutterActor *self_, ClutterScrollEvent *event)
{
	if(event->direction == CLUTTER_SCROLL_SMOOTH)
	{
		gdouble dx=0.0f, dy=0.0f;
		clutter_event_get_scroll_delta((ClutterEvent *)event, &dx, &dy);
		dx *= 50; // TODO: Not magic number for multiplier
		dy *= 50;
		scroll_by(CMK_SCROLL_BOX(self_), dx, dy);
	}
	return CLUTTER_EVENT_STOP;
}

static void scroll_to_actor(CmkScrollBox *self, ClutterActor *scrollto)
{
	ClutterVertex vert = {0};
	ClutterVertex out = {0};
	clutter_actor_apply_relative_transform_to_point(scrollto, CLUTTER_ACTOR(self), &vert, &out);
	ClutterPoint *scroll = &(PRIVATE(self)->scroll);
	ClutterPoint p = {out.x + scroll->x, out.y + scroll->y};
	scroll_to(self, &p, FALSE);
}

static void on_key_focus_changed(CmkWidget *self, ClutterActor *newfocus)
{
	scroll_to_actor(CMK_SCROLL_BOX(self), newfocus);
}
