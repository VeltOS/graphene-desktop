/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "settings-panels.h"
#include "../csk/network.h"
#include <cmk/cmk.h>

struct _GrapheneNetworkPanel
{
	CmkWidget parent;
	CskNetworkManager *manager;
};

G_DECLARE_FINAL_TYPE(NDeviceGroup, ndevice_group, NDEVICE, GROUP, CmkWidget)
struct _NDeviceGroup
{
	CmkWidget parent;
	CskNetworkDevice *device;
	ClutterActor *sep;
	CmkLabel *label;
	guint scanTimerId;
};

G_DECLARE_FINAL_TYPE(NApButton, nap_button, NAP, BUTTON, CmkButton)
struct _NApButton
{
	CmkButton parent;
	CskNetworkAccessPoint *ap;
};

static void graphene_network_panel_dispose(GObject *self_);
static void on_device_added(GrapheneNetworkPanel *self, CskNetworkDevice *device);
static void on_device_removed(GrapheneNetworkPanel *self, CskNetworkDevice *device);
static void on_primary_device_changed(GrapheneNetworkPanel *self);
static void on_device_name_changed(NDeviceGroup *group, GParamSpec *spec, CskNetworkDevice *device);
static void on_ap_added(CmkWidget *group, CskNetworkAccessPoint *ap, CskNetworkDevice *device);
static void on_ap_removed(CmkWidget *group, CskNetworkAccessPoint *ap);
static void on_active_ap_changed(CmkWidget *group, GParamSpec *spec, CskNetworkDevice *device);


G_DEFINE_TYPE(GrapheneNetworkPanel, graphene_network_panel, CMK_TYPE_WIDGET)
G_DEFINE_TYPE(NDeviceGroup, ndevice_group, CMK_TYPE_WIDGET)
G_DEFINE_TYPE(NApButton, nap_button, CMK_TYPE_BUTTON)


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
	g_signal_connect_swapped(self->manager, "notify::primary-device", G_CALLBACK(on_primary_device_changed), self);
	
	const GList *devices = csk_network_manager_get_devices(self->manager);
	for(const GList *it=devices; it!=NULL; it=it->next)
		on_device_added(self, it->data);
	
	on_primary_device_changed(self);
}

static void graphene_network_panel_dispose(GObject *self_)
{
	GrapheneNetworkPanel *self = GRAPHENE_NETWORK_PANEL(self_);
	if(self->manager)
		g_signal_handlers_disconnect_by_data(self->manager, self);
	g_clear_object(&self->manager);
	G_OBJECT_CLASS(graphene_network_panel_parent_class)->dispose(self_);
}

static void ndevice_group_dispose(GObject *self_)
{
	NDeviceGroup *self = NDEVICE_GROUP(self_);
	if(self->device)
		g_signal_handlers_disconnect_by_data(self->device, self);
	if(self->scanTimerId)
		g_source_remove(self->scanTimerId);
	self->scanTimerId = 0;
	g_clear_object(&self->device);
	G_OBJECT_CLASS(ndevice_group_parent_class)->dispose(self_);
}

static void ndevice_group_class_init(NDeviceGroupClass *class)
{
	G_OBJECT_CLASS(class)->dispose = ndevice_group_dispose;
}

static void ndevice_group_init(UNUSED NDeviceGroup *self)
{
}

static void nap_button_dispose(GObject *self_)
{
	NApButton *self = NAP_BUTTON(self_);
	if(self->ap)
		g_signal_handlers_disconnect_by_data(self->ap, self);
	g_clear_object(&self->ap);
	G_OBJECT_CLASS(nap_button_parent_class)->dispose(self_);
}

static void nap_button_class_init(NApButtonClass *class)
{
	G_OBJECT_CLASS(class)->dispose = nap_button_dispose;
}

static void nap_button_init(UNUSED NApButton *self)
{
}

static void on_device_added(GrapheneNetworkPanel *self, CskNetworkDevice *device)
{
	NDeviceGroup *group = NDEVICE_GROUP(g_object_new(ndevice_group_get_type(), NULL));
	g_object_set_data(G_OBJECT(group), "dev", device);
	group->device = g_object_ref(device);
	
	const gchar *name = csk_network_device_get_name(device);
	group->label = graphene_category_label_new(name);
	group->sep = CLUTTER_ACTOR(cmk_separator_new_h());

	if(clutter_actor_get_n_children(CLUTTER_ACTOR(self)) == 0)
		clutter_actor_hide(group->sep);

	clutter_actor_set_layout_manager(CLUTTER_ACTOR(group), clutter_vertical_box_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(group), TRUE);

	clutter_actor_add_child(CLUTTER_ACTOR(group), group->sep);
	clutter_actor_add_child(CLUTTER_ACTOR(group), CLUTTER_ACTOR(group->label));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(group));
	
	g_signal_connect_swapped(device, "ap-added", G_CALLBACK(on_ap_added), group);
	g_signal_connect_swapped(device, "ap-removed", G_CALLBACK(on_ap_removed), group);
	g_signal_connect_swapped(device, "notify::active-ap", G_CALLBACK(on_active_ap_changed), group);
	g_signal_connect_swapped(device, "notify::name", G_CALLBACK(on_device_name_changed), group);
	
	const GList *aps = csk_network_device_get_access_points(device);
	for(const GList *it=aps; it!=NULL; it=it->next)
		on_ap_added(CMK_WIDGET(group), it->data, device);
	
	on_active_ap_changed(CMK_WIDGET(group), NULL, device);
	
	csk_network_device_scan(device);
	group->scanTimerId = g_timeout_add_seconds(5, (GSourceFunc)csk_network_device_scan, device);
}

static void on_device_removed(GrapheneNetworkPanel *self, CskNetworkDevice *device)
{
	guint n = clutter_actor_get_n_children(CLUTTER_ACTOR(self));
	for(guint i=0;i<n;++i)
	{
		ClutterActor *group = clutter_actor_get_child_at_index(CLUTTER_ACTOR(self), i);
		CskNetworkDevice *dev = g_object_get_data(G_OBJECT(group), "dev");
		if(dev == device)
		{
			clutter_actor_destroy(group);
			if(i == 0)
			{
				NDeviceGroup *newfirst = NDEVICE_GROUP(clutter_actor_get_child_at_index(CLUTTER_ACTOR(self), 0));
				if(newfirst)
					clutter_actor_hide(newfirst->sep);
			}
			break;
		}
	}
}

static void on_primary_device_changed(GrapheneNetworkPanel *self)
{
	CskNetworkDevice *primary = csk_network_manager_get_primary_device(self->manager);
	guint n = clutter_actor_get_n_children(CLUTTER_ACTOR(self));
	for(guint i=0;i<n;++i)
	{
		ClutterActor *group = clutter_actor_get_child_at_index(CLUTTER_ACTOR(self), i);
		CskNetworkDevice *dev = g_object_get_data(G_OBJECT(group), "dev");
		if(dev == primary)
		{
			NDeviceGroup *prevfirst = NDEVICE_GROUP(clutter_actor_get_child_at_index(CLUTTER_ACTOR(self), 0));
			if(prevfirst)
				clutter_actor_show(prevfirst->sep);
			clutter_actor_hide(NDEVICE_GROUP(group)->sep);
			clutter_actor_set_child_below_sibling(CLUTTER_ACTOR(self), group, NULL);
			break;
		}
	}
}

static void on_device_name_changed(NDeviceGroup *group, UNUSED GParamSpec *spec, CskNetworkDevice *device)
{
	const gchar *name = csk_network_device_get_name(device);
	cmk_label_set_text(group->label, name);
}


static void on_best_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button);
static void on_name_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button);
static void on_icon_changed(CskNetworkAccessPoint *ap, GParamSpec *spec, CmkButton *button);

static void on_ap_added(CmkWidget *group, CskNetworkAccessPoint *ap, UNUSED CskNetworkDevice *device)
{
	const gchar *icon = csk_network_access_point_get_icon(ap);
	
	NApButton *button = NAP_BUTTON(g_object_new(nap_button_get_type(), NULL));
	g_object_set_data(G_OBJECT(button), "ap", ap);
	button->ap = g_object_ref(ap);
	
	CmkIcon *content = cmk_icon_new_full(icon, NULL, 24, TRUE);
	cmk_button_set_content(CMK_BUTTON(button), CMK_WIDGET(content));
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	
	g_signal_connect(ap, "notify::best", G_CALLBACK(on_best_changed), button);
	g_signal_connect(ap, "notify::icon", G_CALLBACK(on_icon_changed), button);
	g_signal_connect(ap, "notify::name", G_CALLBACK(on_name_changed), button);
	g_signal_connect(ap, "notify::mac", G_CALLBACK(on_name_changed), button);
	
	on_name_changed(ap, NULL, CMK_BUTTON(button));
	on_best_changed(ap, NULL, CMK_BUTTON(button));
	
	clutter_actor_add_child(CLUTTER_ACTOR(group), CLUTTER_ACTOR(button));
}

static void on_ap_removed(CmkWidget *group, CskNetworkAccessPoint *ap)
{
	guint n = clutter_actor_get_n_children(CLUTTER_ACTOR(group));
	for(guint i=0;i<n;++i)
	{
		ClutterActor *button = clutter_actor_get_child_at_index(CLUTTER_ACTOR(group), i);
		CskNetworkAccessPoint *ap_ = g_object_get_data(G_OBJECT(button), "ap");
		if(ap == ap_)
		{
			clutter_actor_destroy(button);
			break;
		}
	}
}

static void on_active_ap_changed(CmkWidget *group, UNUSED GParamSpec *spec, CskNetworkDevice *device)
{
	CskNetworkAccessPoint *activeAp = csk_network_device_get_active_access_point(device);
	
	guint n = clutter_actor_get_n_children(CLUTTER_ACTOR(group));
	for(guint i=0;i<n;++i)
	{
		ClutterActor *button = clutter_actor_get_child_at_index(CLUTTER_ACTOR(group), i);
		CskNetworkAccessPoint *ap = g_object_get_data(G_OBJECT(button), "ap");
		if(!ap)
			continue;
		if(csk_network_access_point_matches(ap, activeAp))
		{
			cmk_button_set_selected(CMK_BUTTON(button), TRUE);
			clutter_actor_set_child_above_sibling(CLUTTER_ACTOR(group), button, CLUTTER_ACTOR(NDEVICE_GROUP(group)->label));
		}
		else
			cmk_button_set_selected(CMK_BUTTON(button), FALSE);
	}
}

static void on_best_changed(CskNetworkAccessPoint *ap, UNUSED GParamSpec *spec, CmkButton *button)
{
	if(csk_network_access_point_is_best(ap))
		clutter_actor_show(CLUTTER_ACTOR(button));
	else
		clutter_actor_hide(CLUTTER_ACTOR(button));
}

static void on_name_changed(CskNetworkAccessPoint *ap, UNUSED GParamSpec *spec, CmkButton *button)
{
	const gchar *name = csk_network_access_point_get_name(ap);
	if(!name)
		name = csk_network_access_point_get_mac(ap);
	if(!name)
		name = "<no name>";
	cmk_button_set_text(button, name);
}

static void on_icon_changed(CskNetworkAccessPoint *ap, UNUSED GParamSpec *spec, CmkButton *button)
{
	const gchar *icon = csk_network_access_point_get_icon(ap);
	CmkIcon *content = CMK_ICON(cmk_button_get_content(button));
	cmk_icon_set_icon(content, icon);
}
