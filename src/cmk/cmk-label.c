/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "cmk-label.h"

typedef struct _CmkLabelPrivate CmkLabelPrivate;
struct _CmkLabelPrivate
{
	ClutterText *text;
};

enum
{
	PROP_TEXT = 1,
	PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void cmk_label_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void cmk_label_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void on_background_changed(CmkWidget *self_);

G_DEFINE_TYPE_WITH_PRIVATE(CmkLabel, cmk_label, CMK_TYPE_WIDGET);
#define PRIVATE(label) ((CmkLabelPrivate *)cmk_label_get_instance_private(label))

CmkLabel * cmk_label_new(void)
{
	return CMK_LABEL(g_object_new(CMK_TYPE_LABEL, NULL));
}

CmkLabel * cmk_label_new_with_text(const gchar *text)
{
	return CMK_LABEL(g_object_new(CMK_TYPE_LABEL, "text", text, NULL));
}

static void cmk_label_class_init(CmkLabelClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->get_property = cmk_label_get_property;
	base->set_property = cmk_label_set_property;
	
	CMK_WIDGET_CLASS(class)->background_changed = on_background_changed;

	properties[PROP_TEXT] = g_param_spec_string("text", "text", "text", NULL, G_PARAM_READWRITE);

	g_object_class_install_properties(base, PROP_LAST, properties);
}

static void cmk_label_init(CmkLabel *self)
{
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_CENTER, CLUTTER_BIN_ALIGNMENT_CENTER));

	PangoFontDescription *desc = pango_font_description_new();
	pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
	
	PRIVATE(self)->text = CLUTTER_TEXT(clutter_text_new());
	clutter_text_set_font_description(PRIVATE(self)->text, desc);
	pango_font_description_free(desc);

	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(PRIVATE(self)->text));
}

static void cmk_label_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	g_return_if_fail(CMK_IS_LABEL(self_));
	CmkLabel *self = CMK_LABEL(self_);
	
	switch(propertyId)
	{
	case PROP_TEXT:
		cmk_label_set_text(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}

static void cmk_label_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	g_return_if_fail(CMK_IS_LABEL(self_));
	CmkLabel *self = CMK_LABEL(self_);
	
	switch(propertyId)
	{
	case PROP_TEXT:
		g_value_set_string(value, cmk_label_get_text(self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}

static void on_background_changed(CmkWidget *self_)
{
	const ClutterColor *color = cmk_widget_get_foreground_color(self_);
	clutter_text_set_color(PRIVATE(CMK_LABEL(self_))->text, color);
	CMK_WIDGET_CLASS(cmk_label_parent_class)->background_changed(self_);
}

void cmk_label_set_text(CmkLabel *self, const gchar *text)
{
	g_return_if_fail(CMK_IS_LABEL(self));
	clutter_text_set_text(PRIVATE(self)->text, text);
}

const gchar * cmk_label_get_text(CmkLabel *self)
{
	g_return_val_if_fail(CMK_IS_LABEL(self), NULL);
	return clutter_text_get_text(PRIVATE(self)->text);
}

void cmk_label_set_markup(CmkLabel *self, const gchar *markup)
{
	g_return_if_fail(CMK_IS_LABEL(self));
	clutter_text_set_markup(PRIVATE(self)->text, markup);
}
