/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include "background.h"

struct _GrapheneWMBackground
{
	ClutterActor parent;
	MetaScreen *screen;
	guint monitor;
	MetaBackgroundActor *actor;
	GSettings *settings;
};

enum
{
	PROP_SCREEN = 1,
	PROP_MONITOR,
	PROP_LAST,
};

static GParamSpec *properties[PROP_LAST];

static void graphene_wm_background_constructed(GObject *self);
static void graphene_wm_background_dispose(GObject *gobject);
static void graphene_wm_background_set_property(GObject *self, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_wm_background_get_property(GObject *self, guint propertyId, GValue *value, GParamSpec *pspec);
static void update(GrapheneWMBackground *backgroundGroup);

G_DEFINE_TYPE (GrapheneWMBackground, graphene_wm_background, CLUTTER_TYPE_ACTOR);

static void graphene_wm_background_class_init(GrapheneWMBackgroundClass *class)
{
	GObjectClass *gobjectClass = G_OBJECT_CLASS(class);
	gobjectClass->dispose = graphene_wm_background_dispose;
	gobjectClass->set_property = graphene_wm_background_set_property;
	gobjectClass->get_property = graphene_wm_background_get_property;
	gobjectClass->constructed = graphene_wm_background_constructed;

	properties[PROP_SCREEN] = g_param_spec_object("screen", "screen", "MetaScreen for the current plugin", META_TYPE_SCREEN, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	properties[PROP_MONITOR] = g_param_spec_int("monitor", "monitor", "monitor index for this background", 0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_properties(gobjectClass, PROP_LAST, properties);
}

GrapheneWMBackground* graphene_wm_background_new(MetaScreen *screen, guint monitor)
{
	return GRAPHENE_WM_BACKGROUND(g_object_new(GRAPHENE_TYPE_WM_BACKGROUND,
		"screen", screen,
		"monitor", monitor,
		NULL));
}

static void graphene_wm_background_init(GrapheneWMBackground *self) {}

static void graphene_wm_background_constructed(GObject *self_)
{
	GrapheneWMBackground *self = GRAPHENE_WM_BACKGROUND(self_);
	self->settings = g_settings_new("org.gnome.desktop.background");
	g_signal_connect_swapped(self->settings, "changed", G_CALLBACK(update), self);
	update(self);
}

static void graphene_wm_background_dispose(GObject *gobject)
{
	GrapheneWMBackground *self = GRAPHENE_WM_BACKGROUND(gobject);
	g_clear_object(&self->screen);
	g_clear_object(&self->actor);
	g_clear_object(&self->settings);
	G_OBJECT_CLASS(graphene_wm_background_parent_class)->dispose(gobject);
}

static void graphene_wm_background_set_property(GObject *self, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	g_return_if_fail(GRAPHENE_IS_WM_BACKGROUND(self));
	if(propertyId == PROP_SCREEN)
		GRAPHENE_WM_BACKGROUND(self)->screen = g_object_ref(g_value_get_object(value));
	else if(propertyId == PROP_MONITOR)
		GRAPHENE_WM_BACKGROUND(self)->monitor = g_value_get_int(value);
}

static void graphene_wm_background_get_property(GObject *self, guint propertyId, GValue *value, GParamSpec *pspec) {}

static void update_done(ClutterActor *newActor, GrapheneWMBackground *self)
{
	clutter_actor_remove_all_transitions(newActor);
	clutter_actor_set_opacity(newActor, 255);
	g_signal_handlers_disconnect_by_func(newActor, update_done, NULL);

	if(self->actor)
		clutter_actor_remove_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->actor));
	self->actor = META_BACKGROUND_ACTOR(newActor);
}

static void update(GrapheneWMBackground *self)
{
	ClutterActor *newActor = meta_background_actor_new(self->screen, self->monitor);
	MetaBackground *newBackground = meta_background_new(self->screen);
	meta_background_actor_set_background(META_BACKGROUND_ACTOR(newActor), newBackground);
	
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_screen_get_monitor_geometry(self->screen, self->monitor, &rect);
	
	clutter_actor_set_position(newActor, rect.x, rect.y);
	clutter_actor_set_size(newActor, rect.width, rect.height);
	clutter_actor_set_opacity(newActor, 0);
	clutter_actor_insert_child_at_index(CLUTTER_ACTOR(self), newActor, -1);
	
	ClutterColor *primaryColor = clutter_color_new(255, 255, 255, 255);
	ClutterColor *secondaryColor = clutter_color_new(255, 255, 255, 255);
	clutter_color_from_string(primaryColor, g_settings_get_string(self->settings, "primary-color"));
	clutter_color_from_string(secondaryColor, g_settings_get_string(self->settings, "secondary-color"));
	GDesktopBackgroundShading shading = g_settings_get_enum(self->settings, "color-shading-type");
	meta_background_set_gradient(newBackground, shading, primaryColor, secondaryColor);
	
	char *imageURI = g_settings_get_string(self->settings, "picture-uri");
	GDesktopBackgroundStyle style = g_settings_get_enum(self->settings, "picture-options");
	GFile *imageFile = g_file_new_for_uri(imageURI);
	meta_background_set_file(newBackground, imageFile, style);
	g_object_unref(imageFile);
	g_free(imageURI);
	
	clutter_actor_show(newActor);
	g_signal_connect(newActor, "transitions_completed", G_CALLBACK(update_done), self);
	clutter_actor_save_easing_state(newActor);
	clutter_actor_set_easing_mode(newActor, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(newActor, 1000);
	clutter_actor_set_opacity(newActor, 255);
	clutter_actor_restore_easing_state(newActor);
}
