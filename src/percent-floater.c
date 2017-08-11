/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "percent-floater.h"

#define PERCENT_FLOATER_MARGIN 2

struct _GraphenePercentFloater {
	ClutterActor parent;
	ClutterActor *inner;
	guint delaySourceId;
	guint divisions;
	gfloat percent;
	gfloat scale;
};

static void update_bar(GraphenePercentFloater *self);

G_DEFINE_TYPE(GraphenePercentFloater, graphene_percent_floater, CLUTTER_TYPE_ACTOR);

GraphenePercentFloater * graphene_percent_floater_new(void)
{
	return GRAPHENE_PERCENT_FLOATER(g_object_new(GRAPHENE_TYPE_PERCENT_FLOATER, NULL));
}

static void graphene_percent_floater_class_init(UNUSED GraphenePercentFloaterClass *class)
{
}

static void graphene_percent_floater_init(GraphenePercentFloater *self)
{
	self->percent = 0;
	self->scale = 1;
	self->divisions = 10;
	clutter_actor_set_reactive(CLUTTER_ACTOR(self), FALSE);
	clutter_actor_set_opacity(CLUTTER_ACTOR(self), 0);
	
	ClutterColor *color = clutter_color_new(255, 255, 255, 180);
	clutter_actor_set_background_color(CLUTTER_ACTOR(self), color);
	clutter_color_free(color);

	self->inner = clutter_actor_new();
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->inner);
	clutter_actor_set_clip_to_allocation(self->inner, TRUE);
	clutter_actor_show(self->inner);
	
	g_signal_connect(self, "notify::width", G_CALLBACK(update_bar), NULL);
	g_signal_connect(self, "notify::height", G_CALLBACK(update_bar), NULL);
	update_bar(self);
}

static void update_bar(GraphenePercentFloater *self)
{
	gfloat width, height;
	clutter_actor_get_size(CLUTTER_ACTOR(self), &width, &height); 
	
	gfloat margin = PERCENT_FLOATER_MARGIN * self->scale;
	gfloat innerWidth = (width - margin*2);
	gfloat innerHeight = (height - margin*2);

	clutter_actor_set_position(self->inner, margin, margin);
	clutter_actor_set_size(self->inner, innerWidth*self->percent, innerHeight);

	clutter_actor_remove_all_children(self->inner);
	for(guint i=0;i<self->divisions;++i)
	{
		ClutterActor *div = clutter_actor_new();
		clutter_actor_add_child(self->inner, div);
		clutter_actor_set_height(div, innerHeight);
		clutter_actor_set_width(div, width/self->divisions - margin);
		clutter_actor_set_x(div, width/self->divisions * i);
		clutter_actor_set_y(div, 0);
		ClutterColor *color = clutter_color_new(208, 37, 37, 180);
		clutter_actor_set_background_color(div, color);
		clutter_color_free(color);
		clutter_actor_show(div);
	}
}

void graphene_percent_floater_set_divisions(GraphenePercentFloater *self, guint divisions)
{
	g_return_if_fail(GRAPHENE_IS_PERCENT_FLOATER(self));
	self->divisions = divisions;
	update_bar(self);
}

void graphene_percent_floater_set_scale(GraphenePercentFloater *self, gfloat scale)
{
	g_return_if_fail(GRAPHENE_IS_PERCENT_FLOATER(self));
	self->scale = scale;
	update_bar(self);
}

static gboolean percent_bar_fade_out(GraphenePercentFloater *self)
{
	clutter_actor_remove_all_transitions(CLUTTER_ACTOR(self));
	clutter_actor_save_easing_state(CLUTTER_ACTOR(self));
	clutter_actor_set_easing_mode(CLUTTER_ACTOR(self), CLUTTER_EASE_IN_QUAD);
	clutter_actor_set_easing_duration(CLUTTER_ACTOR(self), 500);
	clutter_actor_set_opacity(CLUTTER_ACTOR(self), 0);
	clutter_actor_restore_easing_state(CLUTTER_ACTOR(self));
	self->delaySourceId = 0;
	return G_SOURCE_REMOVE;
}

void graphene_percent_floater_set_percent(GraphenePercentFloater *self, gfloat percent)
{
	g_return_if_fail(GRAPHENE_IS_PERCENT_FLOATER(self));

	percent = (percent > 1) ? 1 : ((percent < 0) ? 0 : percent);

	if(self->delaySourceId)
		g_source_remove(self->delaySourceId);
	self->delaySourceId = g_timeout_add(800, (GSourceFunc)percent_bar_fade_out, self); 

	clutter_actor_set_opacity(CLUTTER_ACTOR(self), 255);
	
	if(self->percent == percent)
		return;

	self->percent = percent;

	gfloat width, height;
	clutter_actor_get_size(CLUTTER_ACTOR(self), &width, &height); 
	gfloat margin = PERCENT_FLOATER_MARGIN * self->scale;
	gfloat innerWidth = (width - margin*2);

	clutter_actor_save_easing_state(self->inner);
	clutter_actor_set_easing_mode(self->inner, CLUTTER_LINEAR);
	clutter_actor_set_easing_duration(self->inner, 50);
	clutter_actor_set_width(self->inner, innerWidth * percent);
	clutter_actor_restore_easing_state(self->inner);
}

gfloat graphene_percent_floater_get_percent(GraphenePercentFloater *self)
{
	g_return_val_if_fail(GRAPHENE_IS_PERCENT_FLOATER(self), 0);
	return self->percent;
}
