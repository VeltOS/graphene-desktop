/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "settings-panels.h"
#include "../csk/network.h"
#include <libcmk/cmk.h>

struct _GrapheneNetworkPanel
{
	CmkWidget parent;
	CskNetworkManager *manager;
};

static void graphene_network_panel_dispose(GObject *self_);
static void on_device_added(GrapheneNetworkPanel *self, CskNetworkDevice *device);
static void on_device_removed(GrapheneNetworkPanel *self, CskNetworkDevice *device);
static void on_ap_added(CmkWidget *group, CskNetworkAccessPoint *ap);
static void on_ap_removed(CmkWidget *group, CskNetworkAccessPoint *ap);


G_DEFINE_TYPE(GrapheneNetworkPanel, graphene_network_panel, CMK_TYPE_WIDGET)


GrapheneNetworkPanel * graphene_network_panel_new()
{
	GrapheneNetworkPanel *self = GRAPHENE_NETWORK_PANEL(g_object_new(GRAPHENE_TYPE_NETWORK_PANEL, NULL));
	return self;
}

static void graphene_network_panel_class_init(GrapheneNetworkPanelClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_network_panel_dispose;
}

static void graphene_network_panel_init(GrapheneNetworkPanel *self)
{
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), clutter_vertical_box_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(self), TRUE);
	clutter_actor_set_name(CLUTTER_ACTOR(self), "Network");
	
	self->manager = csk_network_manager_get_default();
	
	g_signal_connect_swapped(self->manager, "device-added", G_CALLBACK(on_device_added), self);
	g_signal_connect_swapped(self->manager, "device-removed", G_CALLBACK(on_device_removed), self);
	
	const GList *devices = csk_network_manager_get_devices(self->manager);
	for(const GList *it=devices; it!=NULL; it=it->next)
		on_device_added(self, it->data);
}

static void graphene_network_panel_dispose(GObject *self_)
{
	GrapheneNetworkPanel *self = GRAPHENE_NETWORK_PANEL(self_);
	g_clear_object(&self->manager);
	G_OBJECT_CLASS(graphene_network_panel_parent_class)->dispose(self_);
}

static void on_device_added(GrapheneNetworkPanel *self, CskNetworkDevice *device)
{
	CmkWidget *group = cmk_widget_new();
	g_object_set_data(G_OBJECT(device), "g", group);
	
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(group), clutter_vertical_box_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(group), TRUE);
	
	clutter_actor_add_child(CLUTTER_ACTOR(group), separator_new());
	
	const gchar *name = csk_network_device_get_name(device);
	CmkLabel *label = graphene_category_label_new(name);
	clutter_actor_add_child(CLUTTER_ACTOR(group), CLUTTER_ACTOR(label));
	
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(group));
	
	g_signal_connect_swapped(device, "ap-added", G_CALLBACK(on_ap_added), group);
	g_signal_connect_swapped(device, "ap-removed", G_CALLBACK(on_ap_removed), group);
	
	const GList *aps = csk_network_device_get_access_points(device);
	for(const GList *it=aps; it!=NULL; it=it->next)
		on_ap_added(group, it->data);
}

static void on_device_removed(GrapheneNetworkPanel *self, CskNetworkDevice *device)
{
	clutter_actor_destroy(g_object_get_data(G_OBJECT(device), "g"));
}

static void on_best_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button)
{
	if(csk_network_access_point_is_best(ap))
		clutter_actor_show(CLUTTER_ACTOR(button));
	else
		clutter_actor_hide(CLUTTER_ACTOR(button));
}

static void on_signal_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button)
{
	const gchar *name = csk_network_access_point_get_name(ap);
	const gchar *mac = csk_network_access_point_get_mac(ap);
	guint strength = csk_network_access_point_get_strength(ap);
	gchar *disp = g_strdup_printf("%s (%i, %s)", name, strength, mac);
	cmk_button_set_text(button, disp);
	g_free(disp);
}

static void on_icon_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button)
{
	const gchar *icon = csk_network_access_point_get_icon(ap);
	CmkIcon *content = CMK_ICON(cmk_button_get_content(button));
	cmk_icon_set_icon(content, icon);
}

static void on_ap_added(CmkWidget *group, CskNetworkAccessPoint *ap)
{
	const gchar *name = csk_network_access_point_get_name(ap);
	//const gchar *mac = csk_network_access_point_get_mac(ap);
	//guint strength = csk_network_access_point_get_strength(ap);
	//gchar *disp = g_strdup_printf("%s (%i, %s)", name, strength, mac);
	
	const gchar *icon = csk_network_access_point_get_icon(ap);
	
	CmkButton *button = cmk_button_new_with_text(name);
	//g_free(disp);
	g_object_set_data(G_OBJECT(ap), "b", button);
	CmkIcon *content = cmk_icon_new_full(icon, NULL, 24, TRUE);
	cmk_button_set_content(button, CMK_WIDGET(content));
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	
	if(!csk_network_access_point_is_best(ap))
		clutter_actor_hide(CLUTTER_ACTOR(button));
	
	g_signal_connect(ap, "notify::signal", G_CALLBACK(on_signal_changed), button);
	g_signal_connect(ap, "notify::best", G_CALLBACK(on_best_changed), button);
	g_signal_connect(ap, "notify::icon", G_CALLBACK(on_icon_changed), button);
	
	clutter_actor_add_child(CLUTTER_ACTOR(group), CLUTTER_ACTOR(button));
}


static void on_ap_removed(CmkWidget *group, CskNetworkAccessPoint *ap)
{
	clutter_actor_destroy(g_object_get_data(G_OBJECT(ap), "b"));
}
