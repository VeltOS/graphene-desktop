/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "panel-internal.h"
#include <libcmk/cmk-label.h>
#include <libcmk/button.h>
#include <libcmk/cmk-icon.h>
#include <libcmk/shadow.h>
#include <libcmk/cmk-scroll-box.h>
#include <act/act.h>

#define PANEL_WIDTH 300

struct _GrapheneSettingsPopup
{
	CmkWidget parent;

	CSettingsLogoutCallback logoutCb;
	gpointer cbUserdata;
	
	CmkShadow *sdc;
	CmkWidget *window;
	CmkScrollBox *scroll;
	CmkWidget *infoBox;
	CmkButton *logoutButton;
	
	CmkLabel *usernameLabel;
	ActUserManager *userManager;
	ActUser *user;
	guint notifyUserChangedId;
	guint notifyIsLoadedId;
};

G_DEFINE_TYPE(GrapheneSettingsPopup, graphene_settings_popup, CMK_TYPE_WIDGET)


static void graphene_settings_popup_dispose(GObject *self_);
static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_style_changed(CmkWidget *self_);
static void on_logout_button_activate(CmkButton *button, GrapheneSettingsPopup *self);
static void on_user_manager_notify_loaded(GrapheneSettingsPopup *self);
static ClutterActor * separator_new();
static void enum_settings_widgets(GrapheneSettingsPopup *self);

GrapheneSettingsPopup * graphene_settings_popup_new(CSettingsLogoutCallback logoutCb, gpointer userdata)
{
	GrapheneSettingsPopup *popup = GRAPHENE_SETTINGS_POPUP(g_object_new(GRAPHENE_TYPE_SETTINGS_POPUP, NULL));
	if(popup)
	{
		popup->logoutCb = logoutCb;
		popup->cbUserdata = userdata;
	}
}

static void graphene_settings_popup_class_init(GrapheneSettingsPopupClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_settings_popup_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_settings_popup_allocate;
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;
}

static ClutterLayoutManager * clutter_vertical_box_new()
{
	ClutterBoxLayout *layout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(layout, CLUTTER_ORIENTATION_VERTICAL); 
	return CLUTTER_LAYOUT_MANAGER(layout);
}

static void graphene_settings_popup_init(GrapheneSettingsPopup *self)
{
	self->sdc = cmk_shadow_new_full(CMK_SHADOW_MASK_LEFT | CMK_SHADOW_MASK_BOTTOM, 40);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->sdc));

	self->window = cmk_widget_new();
	cmk_widget_set_draw_background_color(self->window, TRUE);
	cmk_widget_set_background_color_name(self->window, "background");
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->window));

	self->infoBox = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->infoBox), clutter_vertical_box_new());
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->infoBox));

	self->usernameLabel = cmk_label_new();
	clutter_actor_set_x_expand(CLUTTER_ACTOR(self->usernameLabel), TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(self->usernameLabel), CLUTTER_ACTOR_ALIGN_CENTER);
	clutter_actor_add_child(CLUTTER_ACTOR(self->infoBox), CLUTTER_ACTOR(self->usernameLabel));

	self->logoutButton = cmk_button_new();
	cmk_button_set_type(self->logoutButton, CMK_BUTTON_TYPE_CIRCLE);
	cmk_button_set_content(self->logoutButton, CMK_WIDGET(cmk_icon_new_full("system-shutdown-symbolic", NULL, 48, TRUE)));
	cmk_widget_style_set_padding(CMK_WIDGET(self->logoutButton), 0);
	cmk_widget_set_style_parent(CMK_WIDGET(self->logoutButton), CMK_WIDGET(self->window));
	g_signal_connect(self->logoutButton, "activate", G_CALLBACK(on_logout_button_activate), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self->infoBox), CLUTTER_ACTOR(self->logoutButton));

	clutter_actor_add_child(CLUTTER_ACTOR(self->infoBox), separator_new());

	self->scroll = cmk_scroll_box_new(CLUTTER_SCROLL_VERTICALLY);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->scroll), clutter_vertical_box_new());
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->scroll), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->scroll));
	
	enum_settings_widgets(self);

	self->userManager = act_user_manager_get_default();
	gboolean isLoaded = FALSE;
	g_object_get(self->userManager, "is-loaded", &isLoaded, NULL);
	if(isLoaded)
		on_user_manager_notify_loaded(self);
	else
		self->notifyIsLoadedId = g_signal_connect_swapped(self->userManager, "notify::is-loaded", G_CALLBACK(on_user_manager_notify_loaded), self);
}

static void graphene_settings_popup_dispose(GObject *self_)
{
	GrapheneSettingsPopup *self = GRAPHENE_SETTINGS_POPUP(self_);
	G_OBJECT_CLASS(graphene_settings_popup_parent_class)->dispose(self_);
}

static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GrapheneSettingsPopup *self = GRAPHENE_SETTINGS_POPUP(self_);
	
	gfloat width = PANEL_WIDTH * cmk_widget_style_get_scale_factor(CMK_WIDGET(self_));
	width = MAX(box->x2-width, box->x1+(box->x2-box->x1)/2);

	ClutterActorBox windowBox = {width, box->y1, box->x2, box->y2};
	ClutterActorBox sdcBox = {windowBox.x1-40, windowBox.y1-40, windowBox.x2 + 40, box->y2 + 40};

	gfloat infoMin, infoNat;
	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->infoBox), width, &infoMin, &infoNat);

	ClutterActorBox infoBox = {windowBox.x1, windowBox.y1, windowBox.x2, windowBox.y1 + infoNat};
	ClutterActorBox scrollBox = {windowBox.x1, infoBox.y2, windowBox.x2, windowBox.y2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->window), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->sdc), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->infoBox), &infoBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->scroll), &scrollBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_settings_popup_parent_class)->allocate(self_, box, flags);
}

static void on_style_changed(CmkWidget *self_)
{
	GrapheneSettingsPopup *self = GRAPHENE_SETTINGS_POPUP(self_);

	float padding = cmk_widget_style_get_padding(self_);
	ClutterMargin margin = {0, 0, padding, padding};
	ClutterMargin margin2 = {0, 0, 0, padding};
	clutter_actor_set_margin(CLUTTER_ACTOR(self->usernameLabel), &margin);
	clutter_actor_set_margin(CLUTTER_ACTOR(self->logoutButton), &margin2);

	clutter_actor_queue_relayout(CLUTTER_ACTOR(self_));
	CMK_WIDGET_CLASS(graphene_settings_popup_parent_class)->style_changed(self_);
}

static void on_logout_button_activate(CmkButton *button, GrapheneSettingsPopup *self)
{
	// Don't destroy after delay, it doesn't look very good
	clutter_actor_destroy(CLUTTER_ACTOR(self));
	if(self->logoutCb)
		self->logoutCb(self->cbUserdata);
}

static void on_user_updated(GrapheneSettingsPopup *self, ActUser *user)
{
	const gchar *realName = NULL;

	if(!user || !(realName = act_user_get_real_name(user)))
	{
		cmk_label_set_text(self->usernameLabel, "");
		return;
	}

	gchar *markup = g_strdup_printf("<span font='16'><b>%s</b></span>", realName);
	cmk_label_set_markup(self->usernameLabel, markup);
	g_free(markup);
}

static void on_user_manager_notify_loaded(GrapheneSettingsPopup *self)
{
	if(self->user && self->notifyUserChangedId)
		g_signal_handler_disconnect(self->user, self->notifyUserChangedId);
	self->user = NULL;
	self->notifyUserChangedId = 0;

	const gchar *username = g_getenv("USER");
	if(username)
	{
		self->user = act_user_manager_get_user(self->userManager, username);
		self->notifyUserChangedId = g_signal_connect_swapped(self->user, "changed", G_CALLBACK(on_user_updated), self);
	}

	on_user_updated(self, self->user);
}

static ClutterActor * separator_new()
{
	ClutterActor *sep = clutter_actor_new();
	ClutterColor c = {0,0,0,25};
	clutter_actor_set_background_color(sep, &c);
	clutter_actor_set_x_expand(sep, TRUE);
	clutter_actor_set_height(sep, 2);
	return sep;
}

static void on_settings_widget_clicked(GrapheneSettingsPopup *self, CmkButton *button)
{
	const gchar *args = clutter_actor_get_name(CLUTTER_ACTOR(button));
	
	// Delay so the click animation can be seen
	clutter_threads_add_timeout(200, (GSourceFunc)clutter_actor_destroy, self);

	gchar **argsSplit = g_new0(gchar *, 3);
	argsSplit[0] = g_strdup("gnome-control-center");
	argsSplit[1] = g_strdup(clutter_actor_get_name(CLUTTER_ACTOR(button)));
	g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	g_strfreev(argsSplit);
}

static void add_setting_widget(GrapheneSettingsPopup *self, const gchar *title, const gchar *iconName, gboolean toggleable, const gchar *panel, gboolean bottomSeparator)
{	
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), separator_new());

	CmkButton *button = cmk_button_new();
	CmkIcon *icon = cmk_icon_new_from_name(iconName, 24);
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, title);
	cmk_widget_set_style_parent(CMK_WIDGET(button), CMK_WIDGET(self->window));
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(button));
	
	clutter_actor_set_name(CLUTTER_ACTOR(button), panel);
	g_signal_connect_swapped(button, "activate", G_CALLBACK(on_settings_widget_clicked), self);
	
	if(bottomSeparator)
		clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), separator_new());
}

static void add_settings_category_label(GrapheneSettingsPopup *self, const gchar *title)
{
	CmkLabel *label = cmk_label_new_with_text(title);
	cmk_widget_set_style_parent(CMK_WIDGET(label), CMK_WIDGET(self));
	clutter_actor_set_x_expand(CLUTTER_ACTOR(label), TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(label), CLUTTER_ACTOR_ALIGN_START);
	ClutterMargin margin = {50, 40, 20, 20};
	clutter_actor_set_margin(CLUTTER_ACTOR(label), &margin);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(label));

	//ClutterActor *sep = separator_new();
	//clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), sep);
}

static void enum_settings_widgets(GrapheneSettingsPopup *self)
{
	add_settings_category_label(self, "Personal");
	add_setting_widget(self, "Background",       "preferences-desktop-wallpaper",    TRUE,  "background",     FALSE);
	add_setting_widget(self, "Notifications",    "preferences-system-notifications", TRUE,  "notifications",  FALSE);
	add_setting_widget(self, "Privacy",          "preferences-system-privacy",       FALSE, "privacy",        FALSE);
	add_setting_widget(self, "Region & Language","preferences-desktop-locale",       FALSE, "region",         FALSE);
	add_setting_widget(self, "Search",           "preferences-system-search",        FALSE, "search",         TRUE);
	add_settings_category_label(self, "Hardware");
	add_setting_widget(self, "Bluetooth",        "bluetooth",                        TRUE,  "bluetooth",      FALSE);
	add_setting_widget(self, "Color",            "preferences-color",                FALSE, "color",          FALSE);
	add_setting_widget(self, "Displays",         "preferences-desktop-display",      FALSE, "display",        FALSE);
	add_setting_widget(self, "Keyboard",         "input-keyboard",                   FALSE, "keyboard",       FALSE);
	add_setting_widget(self, "Mouse & Touchpad", "input-mouse",                      FALSE, "mouse",          FALSE);
	add_setting_widget(self, "Network",          "network-workgroup",                TRUE,  "network",        FALSE);
	add_setting_widget(self, "Power",            "gnome-power-manager",              FALSE, "power",          FALSE);
	add_setting_widget(self, "Printers",         "printer",                          FALSE, "printers",       FALSE);
	add_setting_widget(self, "Sound",            "multimedia-volume-control",        TRUE,  "sound",          FALSE);
	add_setting_widget(self, "Wacom Tablet",     "input-tablet",                     FALSE, "wacom",          TRUE);
	add_settings_category_label(self, "System");
	add_setting_widget(self, "Date & Time",      "preferences-system-time",          FALSE, "datetime",       FALSE);
	add_setting_widget(self, "Details",          "applications-system",              FALSE, "info",           FALSE);
	add_setting_widget(self, "Sharing",          "preferences-system-sharing",       FALSE, "sharing",        FALSE);
	add_setting_widget(self, "Universal",        "preferences-desktop-accessibility",FALSE, "universal-access",FALSE);
	add_setting_widget(self, "Users",            "system-users",                     FALSE, "user-accounts",  TRUE);
}
