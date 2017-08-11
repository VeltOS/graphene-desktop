/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "panel-internal.h"
#include <cmk/cmk.h>
#include <act/act.h>
#include "settings-panels/settings-panels.h"

#define PANEL_WIDTH 300

struct _GrapheneSettingsPopup
{
	CmkWidget parent;

	CSettingsLogoutCallback logoutCb;
	gpointer cbUserdata;
	
	CmkWidget *window;
	CmkScrollBox *scroll;
	CmkWidget *infoBox;
	CmkButton *logoutButton;
	
	CmkLabel *usernameLabel;
	ActUserManager *userManager;
	ActUser *user;
	guint notifyUserChangedId;
	guint notifyIsLoadedId;
	GList *panelStack;
};

G_DEFINE_TYPE(GrapheneSettingsPopup, graphene_settings_popup, CMK_TYPE_WIDGET)


static void graphene_settings_popup_dispose(GObject *self_);
static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_logout_button_activate(CmkButton *button, GrapheneSettingsPopup *self);
static void on_user_manager_notify_loaded(GrapheneSettingsPopup *self);
static void on_panel_replace(GrapheneSettingsPopup *self, CmkWidget *replacement, CmkWidget *top);
static void on_panel_back(GrapheneSettingsPopup *self, CmkWidget *top);

GrapheneSettingsPopup * graphene_settings_popup_new(CSettingsLogoutCallback logoutCb, gpointer userdata)
{
	GrapheneSettingsPopup *popup = GRAPHENE_SETTINGS_POPUP(g_object_new(GRAPHENE_TYPE_SETTINGS_POPUP, NULL));
	if(popup)
	{
		popup->logoutCb = logoutCb;
		popup->cbUserdata = userdata;
	}
	return popup;
}

static void graphene_settings_popup_class_init(GrapheneSettingsPopupClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_settings_popup_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_settings_popup_allocate;
}

static void graphene_settings_popup_init(GrapheneSettingsPopup *self)
{
	self->window = cmk_widget_new();
	cmk_widget_set_draw_background_color(self->window, TRUE);
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->window), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->window));
	
	CmkShadowEffect *shadow = cmk_shadow_effect_new(20);
	cmk_shadow_effect_set(shadow, 10, -10, 1, 10);
	clutter_actor_add_effect(CLUTTER_ACTOR(self->window), CLUTTER_EFFECT(shadow));

	self->infoBox = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->infoBox), clutter_vertical_box_new());
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->infoBox));

	self->usernameLabel = cmk_label_new();
	cmk_widget_set_margin(CMK_WIDGET(self->usernameLabel), 0, 0, 10, 10);
	cmk_label_set_font_size(self->usernameLabel, 21);
	cmk_label_set_bold(self->usernameLabel, TRUE);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(self->usernameLabel), TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(self->usernameLabel), CLUTTER_ACTOR_ALIGN_CENTER);
	cmk_widget_set_style_parent(CMK_WIDGET(self->usernameLabel), CMK_WIDGET(self->window));
	clutter_actor_add_child(CLUTTER_ACTOR(self->infoBox), CLUTTER_ACTOR(self->usernameLabel));

	self->logoutButton = cmk_button_new(CMK_BUTTON_TYPE_FLAT_CIRCLE);
	cmk_widget_set_margin(CMK_WIDGET(self->logoutButton), 0, 0, 0, 10);
	cmk_button_set_content(self->logoutButton, CMK_WIDGET(cmk_icon_new_full("system-shutdown-symbolic", NULL, 48, TRUE)));
	cmk_widget_set_padding_multiplier(CMK_WIDGET(self->logoutButton), 0);
	cmk_widget_set_style_parent(CMK_WIDGET(self->logoutButton), CMK_WIDGET(self->window));
	g_signal_connect(self->logoutButton, "activate", G_CALLBACK(on_logout_button_activate), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self->infoBox), CLUTTER_ACTOR(self->logoutButton));

	cmk_widget_add_child(CMK_WIDGET(self->infoBox), cmk_separator_new_h());

	GrapheneSettingsPanel *panel = graphene_settings_panel_new();
	g_signal_connect_swapped(panel, "replace", G_CALLBACK(on_panel_replace), self);
	g_signal_connect_swapped(panel, "back", G_CALLBACK(on_panel_back), self);
	self->panelStack = g_list_prepend(self->panelStack, panel);
	cmk_widget_set_style_parent(CMK_WIDGET(panel), self->window);
	
	self->scroll = cmk_scroll_box_new(CLUTTER_SCROLL_VERTICALLY);
	cmk_scroll_box_set_use_shadow(self->scroll, FALSE, FALSE, TRUE, FALSE);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->scroll), clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_START,CLUTTER_BIN_ALIGNMENT_START));
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->scroll), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(panel));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->scroll));

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
	
	if(self->panelStack)
		g_list_free_full(self->panelStack, (GDestroyNotify)clutter_actor_destroy);
	self->panelStack = NULL;
	
	if(self->user && self->notifyUserChangedId)
		g_signal_handler_disconnect(self->user, self->notifyUserChangedId);
	self->notifyUserChangedId = 0;
	self->user = NULL;
	
	if(self->userManager && self->notifyIsLoadedId)
		g_signal_handler_disconnect(self->userManager, self->notifyIsLoadedId);
	self->userManager = NULL;
	self->notifyIsLoadedId = 0;
	
	G_OBJECT_CLASS(graphene_settings_popup_parent_class)->dispose(self_);
}

static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GrapheneSettingsPopup *self = GRAPHENE_SETTINGS_POPUP(self_);
	
	gfloat width = CMK_DP(self_, PANEL_WIDTH);
	width = MAX(box->x2-width, box->x1+(box->x2-box->x1)/2);

	ClutterActorBox windowBox = {width, box->y1, box->x2, box->y2};

	gfloat infoMin, infoNat;
	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->infoBox), width, &infoMin, &infoNat);

	ClutterActorBox infoBox = {windowBox.x1, windowBox.y1, windowBox.x2, windowBox.y1 + infoNat};
	ClutterActorBox scrollBox = {windowBox.x1, infoBox.y2, windowBox.x2, windowBox.y2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->window), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->infoBox), &infoBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->scroll), &scrollBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_settings_popup_parent_class)->allocate(self_, box, flags);
}

static void on_logout_button_activate(UNUSED CmkButton *button, GrapheneSettingsPopup *self)
{
	if(g_list_length(self->panelStack) > 1)
	{
		on_panel_back(self, CMK_WIDGET(self->panelStack->data));
		return;
	}
	
	if(self->logoutCb)
		self->logoutCb(self->cbUserdata);
	
	// Don't destroy after delay, it doesn't look very good
	clutter_actor_destroy(CLUTTER_ACTOR(self));
}

static void on_user_updated(GrapheneSettingsPopup *self, ActUser *user)
{
	if(g_list_length(self->panelStack) > 1)
		return;
	const gchar *realName = NULL;

	if(!user || !(realName = act_user_get_real_name(user)))
	{
		cmk_label_set_text(self->usernameLabel, "");
		return;
	}

	//gchar *markup = g_strdup_printf("<span font='16'><b>%s</b></span>", realName);
	cmk_label_set_text(self->usernameLabel, realName);
	//g_free(markup);
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

static void on_panel_replace(GrapheneSettingsPopup *self, CmkWidget *replacement, CmkWidget *top)
{
	if(!self->panelStack || self->panelStack->data != top)
		return;
	cmk_widget_fade_out(CMK_WIDGET(top), FALSE);
	// clutter_actor_hide(CLUTTER_ACTOR(top));
	g_signal_connect_swapped(replacement, "replace", G_CALLBACK(on_panel_replace), self);
	g_signal_connect_swapped(replacement, "back", G_CALLBACK(on_panel_back), self);
	cmk_widget_set_style_parent(replacement, self->window);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(replacement));
	cmk_widget_fade_in(CMK_WIDGET(replacement));
	//gchar *markup = g_strdup_printf("<span font='16'><b>%s</b></span>", clutter_actor_get_name(CLUTTER_ACTOR(replacement)));
	cmk_label_set_text(self->usernameLabel, clutter_actor_get_name(CLUTTER_ACTOR(replacement)));
	//g_free(markup);
	CmkIcon *buttonIcon = CMK_ICON(cmk_button_get_content(self->logoutButton));
	cmk_icon_set_icon(buttonIcon, "back");
	self->panelStack = g_list_prepend(self->panelStack, replacement);
}

static void on_panel_back(GrapheneSettingsPopup *self, CmkWidget *top)
{
	if(!self->panelStack || self->panelStack->data != top)
		return;
	
	cmk_widget_fade_out(CMK_WIDGET(top), TRUE);
	self->panelStack = g_list_delete_link(self->panelStack, self->panelStack);
	if(self->panelStack)
	{
		cmk_widget_fade_in(CMK_WIDGET(self->panelStack->data));
		if(g_list_length(self->panelStack) == 1)
		{
			on_user_updated(self, self->user);
			CmkIcon *buttonIcon = CMK_ICON(cmk_button_get_content(self->logoutButton));
			cmk_icon_set_icon(buttonIcon, "system-shutdown-symbolic");
			// cmk_icon_set_size(buttonIcon, 32);
		}
	}
	else
		clutter_actor_destroy(CLUTTER_ACTOR(self));
}
