/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "settings-panels.h"
#include "../network.h"
#include <libcmk/cmk.h>

struct _GrapheneNetworkPanel
{
	CmkWidget parent;
	
	GDBusConnection *connection;
	GCancellable *cancellable;
	guint nmDaemonWatchId;
	guint nmSignalSubId;
	
	GHashTable *devices;
};

typedef struct
{
	GrapheneNetworkPanel *self;
	gchar *objectPath;
	CmkWidget *box;
	CmkLabel *label;
	guint type;
	
	// Only for WiFi (type 2) devices
	guint apMonitorId;
} Device;

static void graphene_network_panel_dispose(GObject *self_);
// static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);

static void on_nm_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, GrapheneNetworkPanel *self);
static void on_nm_daemon_vanished(GDBusConnection *connection, const gchar *name, GrapheneNetworkPanel *self);
static void on_nm_daemon_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	GrapheneNetworkPanel *self);
static void on_nm_get_devices(GDBusConnection *connection, GAsyncResult *res, GrapheneNetworkPanel *self);
static void on_nm_device_added(GrapheneNetworkPanel *self, const gchar *objectPath);
static void on_nm_device_removed(GrapheneNetworkPanel *self, const gchar *objectPath);
static void free_device(Device *device);

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
	
	self->devices = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)free_device);
	
	self->cancellable = g_cancellable_new();
	self->nmDaemonWatchId = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
		"org.freedesktop.NetworkManager",
		G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
		(GBusNameAppearedCallback)on_nm_daemon_appeared,
		(GBusNameVanishedCallback)on_nm_daemon_vanished,
		self,
		NULL);
	
	// add_settings_category_label(CMK_WIDGET(self), "Wireless");
	// clutter_actor_add_child(CLUTTER_ACTOR(self), separator_new());
	// 
	// self->wirelessBox = cmk_widget_new();
	// clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->wirelessBox), clutter_vertical_box_new());
	// clutter_actor_set_x_expand(CLUTTER_ACTOR(self->wirelessBox), TRUE);
	// clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->wirelessBox));
	// 
	// add_settings_category_label(CMK_WIDGET(self), "Wired");
	// clutter_actor_add_child(CLUTTER_ACTOR(self), separator_new());
	// 
	// nm_client_new_async(NULL, (GAsyncReadyCallback)on_nm_connected, self);
}

static void graphene_network_panel_dispose(GObject *self_)
{
	GrapheneNetworkPanel *self = GRAPHENE_NETWORK_PANEL(self_);
	g_hash_table_unref(self->devices);
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	if(self->nmSignalSubId)
		g_dbus_connection_signal_unsubscribe(self->connection, self->nmSignalSubId);
	self->nmSignalSubId = 0;
	G_OBJECT_CLASS(graphene_network_panel_parent_class)->dispose(self_);
}

static void on_nm_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, GrapheneNetworkPanel *self)
{
	self->connection = connection;
	self->nmSignalSubId = g_dbus_connection_signal_subscribe(connection,
		name,
		"org.freedesktop.NetworkManager",
		NULL, // All signals
		"/org/freedesktop/NetworkManager",
		NULL, // All arg0s
		G_DBUS_SIGNAL_FLAGS_NONE,
		(GDBusSignalCallback)on_nm_daemon_signal,
		self,
		NULL);
	
	g_dbus_connection_call(self->connection,
		"org.freedesktop.NetworkManager",
		"/org/freedesktop/NetworkManager",
		"org.freedesktop.NetworkManager",
		"GetDevices",
		NULL,
		G_VARIANT_TYPE("(ao)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		self->cancellable,
		(GAsyncReadyCallback)on_nm_get_devices,
		self);
}

static void on_nm_daemon_vanished(GDBusConnection *connection, const gchar *name, GrapheneNetworkPanel *self)
{
	self->connection = connection;
	g_dbus_connection_signal_unsubscribe(connection, self->nmSignalSubId); 
}

static void on_nm_daemon_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	GrapheneNetworkPanel *self)
{
	if(g_strcmp0(interface, "org.freedesktop.NetworkManager") == 0)
	{
		if(g_strcmp0(signal, "DeviceAdded") == 0)
		{
			gchar *objectPath;
			g_variant_get(parameters, "(o)", &objectPath);
			on_nm_device_added(self, objectPath);
			g_free(objectPath);
		}
		else if(g_strcmp0(signal, "DeviceRemoved") == 0)
		{
			gchar *objectPath;
			g_variant_get(parameters, "(o)", &objectPath);
			on_nm_device_removed(self, objectPath);
			g_free(objectPath);
		}
	}
	// if(g_strcmp0(signal, "SendStartScanSignal") == 0)
	// 	on_scan_begin(self);
	// else if(g_strcmp0(signal, "SendEndScanSignal") == 0)
	// 	on_scan_end(self);
	// else if(g_strcmp0(signal, "StatusChanged") == 0) // parameters are (uav)
		
}

static void on_nm_get_devices(GDBusConnection *connection, GAsyncResult *res, GrapheneNetworkPanel *self)
{
	GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
	g_return_if_fail(ret != NULL);
	
	GVariantIter *iter;
	gchar *objectPath;
	g_variant_get(ret, "(ao)", &iter);
	while(g_variant_iter_loop(iter, "o", &objectPath))
		on_nm_device_added(self, objectPath);
	g_variant_iter_free(iter);
	g_variant_unref(ret);
}

static void on_nm_device_get_type(GDBusConnection *connection, GAsyncResult *res, Device *device);
static void on_nm_device_added(GrapheneNetworkPanel *self, const gchar *objectPath)
{
	Device *device = g_new0(Device, 1);
	device->self = self;
	device->objectPath = g_strdup(objectPath);
	g_hash_table_insert(self->devices, device->objectPath, device);
	
	device->box = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(device->box), clutter_vertical_box_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(device->box), TRUE);
	device->label = add_settings_category_label(device->box, "");
	// clutter_actor_add_child(CLUTTER_ACTOR(device->box), separator_new());

	g_dbus_connection_call(self->connection,
		"org.freedesktop.NetworkManager",
		objectPath,
		"org.freedesktop.DBus.Properties",
		"Get",
		g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "DeviceType"),
		G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		self->cancellable,
		(GAsyncReadyCallback)on_nm_device_get_type,
		device);
}

static void on_nm_wireless_device_ap_added(Device *device, const gchar *objectPath);
static void on_nm_wireless_device_ap_removed(Device *device, const gchar *objectPath);

static void on_nm_wireless_device_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	Device *device)
{
	if(g_strcmp0(signal, "AccessPointAdded") == 0)
	{
		gchar *objectPath;
		g_variant_get(parameters, "(o)", &objectPath);
		on_nm_wireless_device_ap_added(device, objectPath);
		g_free(objectPath);
	}
	else if(g_strcmp0(signal, "AccessPointRemoved") == 0)
	{
		gchar *objectPath;
		g_variant_get(parameters, "(o)", &objectPath);
		on_nm_wireless_device_ap_removed(device, objectPath);
		g_free(objectPath);
	}
}

static void on_nm_wireless_device_get_aps(GDBusConnection *connection, GAsyncResult *res, Device *device)
{
	GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
	g_return_if_fail(ret != NULL);
	
	GVariantIter *iter;
	gchar *objectPath;
	g_variant_get(ret, "(ao)", &iter);
	while(g_variant_iter_loop(iter, "o", &objectPath))
		on_nm_wireless_device_ap_added(device, objectPath);
	g_variant_iter_free(iter);
	g_variant_unref(ret);
}

static void on_nm_device_get_type(GDBusConnection *connection, GAsyncResult *res, Device *device)
{
	GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
	g_return_if_fail(ret != NULL);
	
	GVariant *v = NULL;
	g_variant_get(ret, "(v)", &v);
	g_variant_get(v, "u", &device->type);
	g_variant_unref(ret);
	g_variant_unref(v);
	
	// if(device->type == 1) // NM_DEVICE_TYPE_ETHERNET
	// {
	// 	cmk_label_set_text(device->label, "Ethernet");
	// }
	// else
	if(device->type == 2) // NM_DEVICE_TYPE_WIFI
	{
		cmk_label_set_text(device->label, "Wi-Fi");
		
		device->apMonitorId = g_dbus_connection_signal_subscribe(device->self->connection,
			"org.freedesktop.NetworkManager",
			"org.freedesktop.NetworkManager.Device.Wireless",
			NULL, // All signals
			device->objectPath,
			NULL, // All arg0s
			G_DBUS_SIGNAL_FLAGS_NONE,
			(GDBusSignalCallback)on_nm_wireless_device_signal,
			device,
			NULL);
		
		g_dbus_connection_call(device->self->connection,
			"org.freedesktop.NetworkManager",
			device->objectPath,
			"org.freedesktop.NetworkManager.Device.Wireless",
			"GetAccessPoints",
			NULL,
			G_VARIANT_TYPE("(ao)"),
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			device->self->cancellable,
			(GAsyncReadyCallback)on_nm_wireless_device_get_aps,
			device);
	}
	else
		return;
	
	clutter_actor_add_child(CLUTTER_ACTOR(device->self), CLUTTER_ACTOR(device->box));
}

static void on_nm_wireless_device_ap_added(Device *device, const gchar *objectPath)
{
	CmkButton *button = cmk_button_new();
	CmkIcon *icon = cmk_icon_new_full("network-wireless-signal-good-symbolic", NULL, 22, TRUE);
	ClutterMargin margin = {20, 0, 0, 0};
	clutter_actor_set_margin(CLUTTER_ACTOR(icon), &margin);
	
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, objectPath);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	// clutter_actor_insert_child_below(CLUTTER_ACTOR(device->box), CLUTTER_ACTOR(button), clutter_actor_get_last_child(device->box));
	// clutter_actor_add_child(CLUTTER_ACTOR(device->box), separator_new());
}

static void on_nm_wireless_device_ap_removed(Device *device, const gchar *objectPath)
{
	
}

// static void update_device_labels(GrapheneNetworkPanel *self)
// {
// 	GHashTableIter iter;
// 	gpointer key, value;
// 	g_hash_table_iter_init(&iter, self->devices);
// 	while(g_hash_table_iter_next(&iter, &key, &value))
// 	{
// 		Device *device = (Device *)value;
// 		
// 	}
// }

static void on_nm_device_removed(GrapheneNetworkPanel *self, const gchar *objectPath)
{
	g_hash_table_remove(self->devices, objectPath);
}

static void free_device(Device *device)
{
	g_dbus_connection_signal_unsubscribe(device->self->connection, device->apMonitorId);
	clutter_actor_destroy(CLUTTER_ACTOR(device->box));
	g_free(device->objectPath);
	g_free(device);
}



// static void on_nm_connected(GObject *sources, GAsyncResult *res, GrapheneNetworkPanel *self)
// {
// 	NMClient *client = nm_client_new_finish(res, NULL);
// 	const GPtrArray *connections = nm_client_get_connections(client);
// 	for(guint i=0;i<connections->len;++i)
// 	{
// 		NMRemoteConnection *con = g_ptr_array_index(connections, i);
// 		NMSettingWireless *w = nm_connection_get_setting_wireless((NMConnection *)con);
// 		const gchar *ssid = "";
// 		if(w)
// 			ssid = (const gchar *)g_bytes_get_data(nm_setting_wireless_get_ssid(w), NULL);
// 		else
// 			ssid = nm_connection_get_uuid((NMConnection *)con);
// 			
// 		CmkButton *button = cmk_button_new();
// 		CmkIcon *icon = cmk_icon_new_full("network-wireless-signal-good-symbolic", NULL, 22, TRUE);
// 		ClutterMargin margin = {20, 0, 0, 0};
// 		clutter_actor_set_margin(CLUTTER_ACTOR(icon), &margin);
// 		
// 		cmk_button_set_content(button, CMK_WIDGET(icon));
// 		cmk_button_set_text(button, ssid);
// 		clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
// 		clutter_actor_add_child(CLUTTER_ACTOR(self->wirelessBox), CLUTTER_ACTOR(button));
// 		clutter_actor_add_child(CLUTTER_ACTOR(self->wirelessBox), separator_new());
// 	}
// 	g_object_unref(client);
// }

// static void graphene_settings_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
// {
// 	
// }
