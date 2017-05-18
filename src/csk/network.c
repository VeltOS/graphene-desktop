/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "network.h"
#include <gio/gio.h>

struct _CskNetworkSettings
{
	GObject parent;
	
	GCancellable *cancellable;
	GDBusConnection *connection;
	// guint daemonWatchId;
	// guint signalSubId;
	// gboolean scanning;
	// GList *networks;
	// gint propUpdateQueue;
};

struct _CskNetworkInfo
{
	CskNetworkType type;
	gchar *name;
	gchar *ip;
	gfloat signalStrength;
	gchar *iconName;
	
	gint id;
	CskNetworkSettings *settings;
	guint flags;
};

enum
{
	CSK_NETWORK_FLAG_SET_NAME = 1,
	CSK_NETWORK_FLAG_SET_MAC = 2,
	CSK_NETWORK_FLAG_SET_IP = 4,
	CSK_NETWORK_FLAG_SET_STRENGTH = 8,
};

enum
{
	PROP_CURRENT_NETWORK = 1,
	PROP_LAST
};

enum
{
	SIGNAL_NETWORK_ADDED = 1,
	SIGNAL_NETWORK_REMOVED,
	SIGNAL_NETWORK_CHANGED,
	SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static void csk_network_settings_dispose(GObject *self_);
// static void on_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, CskNetworkSettings *self);
// static void on_daemon_vanished(GDBusConnection *connection, const gchar *name, CskNetworkSettings *self);
// static void on_daemon_signal(GDBusConnection *connection,
// 	const gchar *sender,
// 	const gchar *object,
// 	const gchar *interface,
// 	const gchar *signal,
// 	GVariant    *parameters,
// 	CskNetworkSettings *self);
// static void on_scan_begin(CskNetworkSettings *self);
// static void on_scan_end(CskNetworkSettings *self);
// static void get_networks(CskNetworkSettings *self);

G_DEFINE_POINTER_TYPE(CskNetworkInfo, csk_network_info);
G_DEFINE_TYPE(CskNetworkSettings, csk_network_settings, G_TYPE_OBJECT)


static CskNetworkSettings * csk_network_settings_new(void)
{
	return CSK_NETWORK_SETTINGS(g_object_new(CSK_TYPE_NETWORK_SETTINGS, NULL));
}

CskNetworkSettings * csk_network_settings_get_default(void)
{
	static CskNetworkSettings *self = NULL;
	if(!CSK_IS_NETWORK_SETTINGS(self))
		return (self = csk_network_settings_new());
	return g_object_ref(self);
}

static void csk_network_settings_class_init(CskNetworkSettingsClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_settings_dispose;

	properties[PROP_CURRENT_NETWORK] = g_param_spec_param("current-network",
		"current network", "current network", csk_network_info_get_type(), G_PARAM_READWRITE);

	g_object_class_install_properties(base, PROP_LAST, properties);

	signals[SIGNAL_NETWORK_ADDED] = g_signal_new("network-added", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST,
	0, NULL, NULL, NULL, G_TYPE_NONE, 1, csk_network_info_get_type());
	signals[SIGNAL_NETWORK_REMOVED] = g_signal_new("network-removed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST,
	0, NULL, NULL, NULL, G_TYPE_NONE, 1, csk_network_info_get_type());
	signals[SIGNAL_NETWORK_CHANGED] = g_signal_new("network-changed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST,
	0, NULL, NULL, NULL, G_TYPE_NONE, 1, csk_network_info_get_type());
}

static void csk_network_settings_init(CskNetworkSettings *self)
{
	self->cancellable = g_cancellable_new();
	self->daemonWatchId = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
		"org.wicd.daemon",
		G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
		(GBusNameAppearedCallback)on_daemon_appeared,
		(GBusNameVanishedCallback)on_daemon_vanished,
		self,
		NULL);
}

static void csk_network_settings_dispose(GObject *self_)
{
	CskNetworkSettings *self = CSK_NETWORK_SETTINGS(self_);
	// g_cancellable_cancel(self->cancellable);
	// g_clear_object(&self->cancellable);
	// if(self->signalSubId)
	// 	g_dbus_connection_signal_unsubscribe(self->connection, self->signalSubId);
	// if(self->daemonWatchId)
	// 	g_bus_unwatch_name(self->daemonWatchId);
	// self->signalSubId = 0;
	// self->daemonWatchId = 0;
	G_OBJECT_CLASS(csk_network_settings_parent_class)->dispose(self_);
}

static void on_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, CskNetworkSettings *self)
{
	self->connection = connection;
	// self->signalSubId = g_dbus_connection_signal_subscribe(connection,
	// 	name,
	// 	NULL, // All interfaces
	// 	NULL, // All signals
	// 	NULL, // All objects
	// 	NULL, // All arg0s
	// 	G_DBUS_SIGNAL_FLAGS_NONE,
	// 	(GDBusSignalCallback)on_daemon_signal,
	// 	self,
	// 	NULL);
	// // get_networks(self);
}

static void on_daemon_vanished(GDBusConnection *connection, const gchar *name, CskNetworkSettings *self)
{
	// self->connection = connection;
	// g_dbus_connection_signal_unsubscribe(connection, self->signalSubId); 
}

// static void on_daemon_signal(GDBusConnection *connection,
// 	const gchar *sender,
// 	const gchar *object,
// 	const gchar *interface,
// 	const gchar *signal,
// 	GVariant    *parameters,
// 	CskNetworkSettings *self)
// {
// 	if(g_strcmp0(signal, "SendStartScanSignal") == 0)
// 		on_scan_begin(self);
// 	else if(g_strcmp0(signal, "SendEndScanSignal") == 0)
// 		on_scan_end(self);
// 	// else if(g_strcmp0(signal, "StatusChanged") == 0) // parameters are (uav)
// 		
// }

// static void on_scan_begin(CskNetworkSettings *self)
// {
// 	self->scanning = TRUE;
// }
// 
// static void on_scan_end(CskNetworkSettings *self)
// {
// 	self->scanning = FALSE;
// 	get_networks(self);
// }
// 
// static void get_networks_cb1_get_num(GObject *source, GAsyncResult *res, CskNetworkSettings *self);
// static void get_networks(CskNetworkSettings *self)
// {
// 	g_return_if_fail(self->connection != NULL);
// 	g_dbus_connection_call(self->connection,
// 		"org.wicd.daemon",
// 		"/org/wicd/daemon/wireless",
// 		"org.wicd.daemon.wireless",
// 		"GetNumberOfNetworks",
// 		NULL,
// 		G_VARIANT_TYPE("(i)"),
// 		G_DBUS_CALL_FLAGS_NONE,
// 		-1,
// 		self->cancellable,
// 		(GAsyncReadyCallback)get_networks_cb1_get_num,
// 		self);
// }
// 
// typedef struct
// {
// 	CskNetworkInfo *network;
// 	guint propertyFlag;
// 	gboolean newNetwork;
// } PropertyUpdateInfo;
// 
// static void replace_existing_network(CskNetworkSettings *self, CskNetworkInfo *newNetwork)
// {
// 	for(GList *it=self->networks; it!=NULL; it=it->next)
// 	{
// 		CskNetworkInfo *network = (CskNetworkInfo *)it->data;
// 		if(network->type == newNetwork->type && g_strcmp0(network->mac, newNetwork->mac) == 0)
// 		{
// 			gboolean changed = FALSE;
// 			network->id = newNetwork->id;
// 			if(network->signalStrength != network->signalStrength)
// 			{
// 				network->signalStrength = newNetwork->signalStrength;
// 				changed = TRUE;
// 			}
// 			if(g_strcmp0(network->name, newNetwork->name) != 0)
// 			{
// 				g_free(network->name);
// 				network->name = newNetwork->name;
// 				changed = TRUE;
// 			}
// 			if(g_strcmp0(network->ip, newNetwork->ip) != 0)
// 			{
// 				g_free(network->ip);
// 				network->ip = newNetwork->ip;
// 				changed = TRUE;
// 			}
// 			if(g_strcmp0(network->iconName, newNetwork->iconName) != 0)
// 			{
// 				g_free(network->iconName);
// 				network->iconName = newNetwork->iconName;
// 				changed = TRUE;
// 			}
// 			if(changed)
// 				g_signal_emit(self, signals[SIGNAL_NETWORK_CHANGED], 0, network);
// 			g_free(newNetwork->mac);
// 			g_free(newNetwork);
// 			return;
// 		}
// 	}
// 	
// 	self->networks = g_list_prepend(self->networks, newNetwork);
// 	g_signal_emit(self, signals[SIGNAL_NETWORK_ADDED], 0, newNetwork);
// }
// 
// static void remove_network(CskNetworkInfo *network)
// {
// 	g_signal_emit(network->settings, signals[SIGNAL_NETWORK_REMOVED], 0, network);
// 	g_free(network->name);
// 	g_free(network->mac);
// 	g_free(network->ip);
// 	g_free(network->iconName);
// 	g_free(network);
// }
// 
// static void remove_old_networks(CskNetworkSettings *self)
// {
// 	for(GList *it=self->networks; it!=NULL; )
// 	{
// 		CskNetworkInfo *network = (CskNetworkInfo *)it->data;
// 		if(network->id < 0)
// 		{
// 			remove_network(network);
// 			GList *delete = it;
// 			it=it->next;
// 			self->networks = g_list_delete_link(self->networks, delete);
// 		}
// 		else
// 			it=it->next;
// 	}
// }
// 
// static void update_wireless_network_property_cb(GObject *source, GAsyncResult *res, PropertyUpdateInfo *pui)
// {
// 	CskNetworkInfo *network = pui->network;
// 	CskNetworkSettings *self = network->settings;
// 	guint propertyFlag = pui->propertyFlag;
// 	gboolean newNetwork = pui->newNetwork;
// 	g_free(pui);
// 	
// 	self->propUpdateQueue --;
// 	
// 	// Even if the property request fails, set the SET flag anyway; it informs
// 	// that an attempt was made to get the flag.
// 	network->flags |= propertyFlag;
// 	
// 	GVariant *r = g_dbus_connection_call_finish(self->connection, res, NULL);
// 	if(!r)
// 	{
// 		if(self->propUpdateQueue <= 0)
// 			remove_old_networks(self);
// 		return;
// 	}
// 	
// 	gchar *val;
// 	g_variant_get(r, "(s)", &val);
// 	g_variant_unref(r);
// 	
// 	if(propertyFlag == CSK_NETWORK_FLAG_SET_NAME)
// 		network->name = val;
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_MAC)
// 		network->mac = val;
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_IP)
// 		network->ip = val;
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_STRENGTH)
// 	{
// 		network->signalStrength = atol(val);
// 		g_free(val);
// 	}
// 	else
// 		g_free(val);
// 	
// 	// If we've just got a new network from get_networks, we need to wait until
// 	// all the properties come in before updating the list of networks.
// 	if(newNetwork
// 	&&(network->flags & CSK_NETWORK_FLAG_SET_NAME)
// 	&&(network->flags & CSK_NETWORK_FLAG_SET_MAC)
// 	&&(network->flags & CSK_NETWORK_FLAG_SET_IP)
// 	&&(network->flags & CSK_NETWORK_FLAG_SET_STRENGTH))
// 	{
// 		// Once they're all in, try to replace an existing network if possible.
// 		// If this network is completely new, it will add it as usual.
// 		// replace_existing_network may free 'network'. 
// 		replace_existing_network(self, network);
// 	}
// 	else if(!newNetwork)
// 	{
// 		// This network is already in the list, so just send out a changed
// 		// notification.
// 		g_signal_emit(self, signals[SIGNAL_NETWORK_CHANGED], 0, network);
// 	}
// 	
// 	if(self->propUpdateQueue <= 0)
// 		remove_old_networks(self);
// }
// 
// static void update_wireless_network_property(CskNetworkInfo *network, guint propertyFlag, gboolean newNetwork)
// {
// 	PropertyUpdateInfo *pui = g_new0(PropertyUpdateInfo, 1);
// 	pui->network = network;
// 	pui->propertyFlag = propertyFlag;
// 	pui->newNetwork = newNetwork;
// 	
// 	const gchar *propName = NULL;
// 	if(propertyFlag == CSK_NETWORK_FLAG_SET_NAME) propName = "essid";
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_MAC) propName = "bssid";
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_IP) propName = "ip";
// 	else if(propertyFlag == CSK_NETWORK_FLAG_SET_STRENGTH) propName = "strength";
// 	else
// 		return;
// 	
// 	network->settings->propUpdateQueue++;
// 	g_dbus_connection_call(network->settings->connection,
// 		"org.wicd.daemon",
// 		"/org/wicd/daemon/wireless",
// 		"org.wicd.daemon.wireless",
// 		"GetWirelessProperty",
// 		g_variant_new("(is)", network->id, propName),
// 		G_VARIANT_TYPE("(s)"),
// 		G_DBUS_CALL_FLAGS_NONE,
// 		-1,
// 		network->settings->cancellable,
// 		(GAsyncReadyCallback)update_wireless_network_property_cb,
// 		pui);
// }
// 
// static void get_networks_cb1_get_num(GObject *source, GAsyncResult *res, CskNetworkSettings *self)
// {
// 	GVariant *r = g_dbus_connection_call_finish(self->connection, res, NULL);
// 	g_return_if_fail(r != NULL);
// 	
// 	gint numWirelessNetworks;
// 	g_variant_get(r, "(i)", &numWirelessNetworks);
// 	g_variant_unref(r);
// 	
// 	// Clear all the networks' ids so that no operations can be made on them
// 	// until the updated properties are applied.
// 	for(GList *it=self->networks; it!=NULL; it=it->next)
// 		((CskNetworkInfo *)it->data)->id = -1;
// 	
// 	self->propUpdateQueue = 0;
// 	
// 	for(gint i=0;i<numWirelessNetworks;++i)
// 	{
// 		CskNetworkInfo *network = g_new0(CskNetworkInfo, 1);
// 		network->id = i;
// 		network->type = CSK_NETWORK_TYPE_WIRELESS;
// 		network->signalStrength = -1;
// 		network->settings = self;
// 		// Will automatically add the network to the list and emit network
// 		// added signal after all 3 of these properties come in.
// 		update_wireless_network_property(network, CSK_NETWORK_FLAG_SET_NAME, TRUE);
// 		update_wireless_network_property(network, CSK_NETWORK_FLAG_SET_MAC, TRUE);
// 		update_wireless_network_property(network, CSK_NETWORK_FLAG_SET_IP, TRUE);
// 		update_wireless_network_property(network, CSK_NETWORK_FLAG_SET_STRENGTH, TRUE);
// 	}
// }