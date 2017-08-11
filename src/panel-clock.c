/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "panel-internal.h"

#define FORMAT_STRING_LENGTH 25

struct _GrapheneClockLabel
{
	CmkWidget parent;
	GSettings *interfaceSettings;
	GSource *source;
	gchar *format;
};

static void graphene_clock_label_dispose(GObject *self_);
static void on_interface_settings_changed(GrapheneClockLabel *self, gchar *key, GSettings *settings);
static gboolean update(GSource *source, GSourceFunc callback, gpointer userdata);

G_DEFINE_TYPE(GrapheneClockLabel, graphene_clock_label, CMK_TYPE_LABEL);

GrapheneClockLabel * graphene_clock_label_new(void)
{
	return GRAPHENE_CLOCK_LABEL(g_object_new(GRAPHENE_TYPE_CLOCK_LABEL, NULL));
}

static void graphene_clock_label_class_init(GrapheneClockLabelClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_clock_label_dispose;
}

static void graphene_clock_label_init(GrapheneClockLabel *self)
{
	cmk_label_set_bold(CMK_LABEL(self), TRUE);
	
	self->format = g_new(gchar, FORMAT_STRING_LENGTH);
	self->format[0] = '\0'; // Empty string
	
	self->interfaceSettings = g_settings_new("org.gnome.desktop.interface");
	g_signal_connect_swapped(self->interfaceSettings, "changed", G_CALLBACK(on_interface_settings_changed), self);
	on_interface_settings_changed(self, "clock-", self->interfaceSettings);
	
	static GSourceFuncs funcs = { NULL, NULL, update, NULL, NULL, NULL };
	self->source = g_source_new(&funcs, sizeof(GSource));
	g_source_set_callback(self->source, NULL, self, NULL); // Sets the userdata passed to update - the callback itself is ignored
	g_source_set_ready_time(self->source, 0);
	g_source_attach(self->source, NULL);
}

static void graphene_clock_label_dispose(GObject *self_)
{
	GrapheneClockLabel *self = GRAPHENE_CLOCK_LABEL(self_);
	g_clear_object(&self->interfaceSettings);
	g_clear_pointer(&self->source, g_source_destroy);
	g_clear_pointer(&self->format, g_free);
	G_OBJECT_CLASS(graphene_clock_label_parent_class)->dispose(self_);
}

static void on_interface_settings_changed(GrapheneClockLabel *self, gchar *key, GSettings *settings)
{
	if(!g_str_has_prefix(key, "clock-"))
		return;

	int format = g_settings_get_enum(settings, "clock-format");
	gboolean showDate = g_settings_get_boolean(settings, "clock-show-date");
	gboolean showSeconds = g_settings_get_boolean(settings, "clock-show-seconds");
	
	self->format[0] = '\0';
	
	if(showDate)
		g_strlcat(self->format, "%a %b %e ", FORMAT_STRING_LENGTH); // Mon Jan 1
	if(format == 1) // 12 hour time
		g_strlcat(self->format, "%l", FORMAT_STRING_LENGTH); // 5
	else
		g_strlcat(self->format, "%H", FORMAT_STRING_LENGTH); // 17
	g_strlcat(self->format, ":%M", FORMAT_STRING_LENGTH); // :30
	if(showSeconds)
		g_strlcat(self->format, ":%S", FORMAT_STRING_LENGTH); // :55
	if(format == 1)
		g_strlcat(self->format, " %p", FORMAT_STRING_LENGTH); // PM
	
	if(self->source)
		g_source_set_ready_time(self->source, 0); // Update label now
}

static gboolean update(GSource *source, UNUSED GSourceFunc callback, gpointer userdata)
{
	GrapheneClockLabel *self = GRAPHENE_CLOCK_LABEL(userdata);
	
	// Get the time as a formatted string
	GDateTime *dt = g_date_time_new_now_local();
	gchar *formatted = g_date_time_format(dt, self->format);
	g_date_time_unref(dt);
	
	// Don't call set_text unless the string changed
	if(g_strcmp0(formatted, cmk_label_get_text(CMK_LABEL(self))) == 0)
		g_free(formatted);
	else
		cmk_label_set_text(CMK_LABEL(self), formatted);
	
	// Get monotonic time of the start of the next second
	// This keeps it from falling out of sync
	gint64 realNow = g_get_real_time(); // wall-clock time
	gint64 usUntilNextSecond = G_USEC_PER_SEC - (realNow - ((realNow / G_USEC_PER_SEC) * G_USEC_PER_SEC));
	usUntilNextSecond = CLAMP(usUntilNextSecond, 0, G_USEC_PER_SEC);
	gint64 updateTime = g_source_get_time(source) + usUntilNextSecond; // monotonic time
	
	// Set source to dispatch at the next second
	g_source_set_ready_time(source, updateTime);
	return G_SOURCE_CONTINUE;
}
