/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "settings-panels.h"
#include <cmk/cmk.h>

struct _GrapheneSettingsPanel
{
	CmkWidget parent;
};

static void enum_settings_widgets(GrapheneSettingsPanel *self);

G_DEFINE_TYPE(GrapheneSettingsPanel, graphene_settings_panel, CMK_TYPE_WIDGET)


GrapheneSettingsPanel * graphene_settings_panel_new()
{
	GrapheneSettingsPanel *self = GRAPHENE_SETTINGS_PANEL(g_object_new(GRAPHENE_TYPE_SETTINGS_PANEL, NULL));
	return self;
}

static void graphene_settings_panel_class_init(UNUSED GrapheneSettingsPanelClass *class)
{
}

static void graphene_settings_panel_init(GrapheneSettingsPanel *self)
{
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), clutter_vertical_box_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(self), TRUE);
	enum_settings_widgets(self);
}

ClutterLayoutManager * clutter_vertical_box_new()
{
	ClutterBoxLayout *layout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(layout, CLUTTER_ORIENTATION_VERTICAL);
	return CLUTTER_LAYOUT_MANAGER(layout);
}

static void waitback(GrapheneSettingsPanel *self)
{
	g_signal_emit_by_name(self, "back");
}

static void on_settings_widget_clicked_n(GrapheneSettingsPanel *self, CmkButton *button)
{
	GType panel = (GType)g_object_get_data(G_OBJECT(button), "panel");
	g_signal_emit_by_name(self, "replace", g_object_new(panel, NULL));
}

static void on_settings_widget_clicked(GrapheneSettingsPanel *self, CmkButton *button)
{
	// Delay so the click animation can be seen
	clutter_threads_add_timeout(200, (GSourceFunc)waitback, self);

	gchar **argsSplit = g_new0(gchar *, 3);
	argsSplit[0] = g_strdup("gnome-control-center");
	argsSplit[1] = g_strdup(clutter_actor_get_name(CLUTTER_ACTOR(button)));
	g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	g_strfreev(argsSplit);
}

UNUSED // Will use later as gnome-control-center becomes replaced
static void add_setting_widget_n(GrapheneSettingsPanel *self, const gchar *title, const gchar *iconName, GType panel, UNUSED gboolean toggleable)
{	
	CmkButton *button = cmk_button_new(CMK_BUTTON_TYPE_FLAT);
	CmkIcon *icon = cmk_icon_new_from_name(iconName, 24);
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, title);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(button));
	
	g_object_set_data(G_OBJECT(button), "panel", (gpointer)panel);
	g_signal_connect_swapped(button, "activate", G_CALLBACK(on_settings_widget_clicked_n), self);
}

static void add_setting_widget(GrapheneSettingsPanel *self, const gchar *title, const gchar *iconName, const gchar *panel, UNUSED gboolean toggleable)
{	
	//clutter_actor_add_child(CLUTTER_ACTOR(self), cmk_separator_new_h());

	CmkButton *button = cmk_button_new(CMK_BUTTON_TYPE_FLAT);
	CmkIcon *icon = cmk_icon_new_from_name(iconName, 24);
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, title);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(button));
	
	clutter_actor_set_name(CLUTTER_ACTOR(button), panel);
	g_signal_connect_swapped(button, "activate", G_CALLBACK(on_settings_widget_clicked), self);
}

CmkLabel * graphene_category_label_new(const gchar *title)
{
	CmkLabel *label = cmk_label_new_full(title, TRUE);
	cmk_widget_set_margin(CMK_WIDGET(label), 25, 20, 10, 10);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(label), TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(label), CLUTTER_ACTOR_ALIGN_START);
	return label;
}

static void add_settings_category_label(CmkWidget *self, const gchar *title)
{
	CmkLabel *label = graphene_category_label_new(title);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(label));
}

static void enum_settings_widgets(GrapheneSettingsPanel *self)
{
	add_settings_category_label(CMK_WIDGET(self), "Personal");
	add_setting_widget(self, "Background",       "preferences-desktop-wallpaper",    "background",    TRUE);
	add_setting_widget(self, "Notifications",    "preferences-system-notifications", "notifications", TRUE);
	add_setting_widget(self, "Privacy",          "preferences-system-privacy",       "privacy",       FALSE);
	add_setting_widget(self, "Region & Language","preferences-desktop-locale",       "region",        FALSE);
	add_setting_widget(self, "Search",           "preferences-system-search",        "search",        FALSE);
	cmk_widget_add_child(CMK_WIDGET(self), cmk_separator_new_h());
	add_settings_category_label(CMK_WIDGET(self), "Hardware");
	add_setting_widget(self, "Bluetooth",        "bluetooth",                        "bluetooth",     TRUE);
	add_setting_widget(self, "Color",            "preferences-color",                "color",         FALSE);
	add_setting_widget(self, "Displays",         "preferences-desktop-display",      "display",       FALSE);
	add_setting_widget(self, "Keyboard",         "input-keyboard",                   "keyboard",      FALSE);
	add_setting_widget(self, "Mouse & Touchpad", "input-mouse",                      "mouse",         FALSE);
	add_setting_widget(self, "Network",          "network-workgroup",                "network",       TRUE);
	//add_setting_widget_n(self, "Network",          "network-workgroup",                GRAPHENE_TYPE_NETWORK_PANEL,       TRUE);
	add_setting_widget(self, "Power",            "gnome-power-manager",              "power",         FALSE);
	add_setting_widget(self, "Printers",         "printer",                          "printers",      FALSE);
	add_setting_widget(self, "Sound",            "multimedia-volume-control",        "sound",         TRUE);
	add_setting_widget(self, "Wacom Tablet",     "input-tablet",                     "wacom",         FALSE);
	cmk_widget_add_child(CMK_WIDGET(self), cmk_separator_new_h());
	add_settings_category_label(CMK_WIDGET(self), "System");
	add_setting_widget(self, "Date & Time",      "preferences-system-time",          "datetime",      FALSE);
	add_setting_widget(self, "Details",          "applications-system",              "info",          FALSE);
	add_setting_widget(self, "Sharing",          "preferences-system-sharing",       "sharing",       FALSE);
	add_setting_widget(self, "Universal",        "preferences-desktop-accessibility","universal-access", FALSE);
	add_setting_widget(self, "Users",            "system-users",                     "user-accounts", FALSE);
}
