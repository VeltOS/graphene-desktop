/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * A summary, because this took way too long to figure out by reading various KDE blog posts, documentation pages, and source files.
 * There are a few different methods for creating system tray icons. The legacy method, still used by GNOME, is the System Tray Protocol Specification
 * which requires Xorg. The newer method, created by KDE and used by Ubuntu, is called StatusNotifier and works using DBus. Most apps (using KDE's Qt code
 * or libappindicator for GTK/Ubuntu) access StatusNotifier at org.kde.StatusNotifier*. There is also the freedesktop specification, org.freedesktop.StatusNotifier*,
 * which appears to be exactly the same thing but renamed to freedesktop.
 * Graphene's implementation will register under both of these DBus names, but not use the legacy Xorg-dependant method.
 * (I'm hoping everyone will switch to org.freedesktop instead of org.kde for StatusNotifier eventually...)
 */

#include "status-notifier-watcher.h"
#include "status-notifier-dbus-ifaces.h"

#define STATUSNOTIFIER_WATCHER_DBUS_NAME "org.freedesktop.StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_KDE_DBUS_NAME "org.kde.StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_DBUS_PATH "/StatusNotifierWatcher" // They decided to not include /org/kde (/org/freedesktop too?) at the start apparently, although I can't find documentation on this

#define STATUSNOTIFIER_PROTOCOL_VERSION 0 // This is not documented anywhere. I found it in knotifications/src/kstatusnotifieritem.cpp commit dae4401 (Mar 30 2016).

struct _GrapheneStatusNotifierWatcher
{
	GObject parent;
	
	guint dbusNameId;
	guint kdeDBusNameId;
	DBusFreedesktopStatusNotifierWatcher *watcherObject; // These are both exported at the path STATUSNOTIFIER_WATCHER_DBUS_PATH
	DBusKdeStatusNotifierWatcher *kdeWatcherObject;
	GHashTable *items;
	GHashTable *hosts;
	
	GCancellable *cancellable;
	GDBusConnection *connection;
};

static void graphene_status_notifier_watcher_dispose(GObject *self_);
static gboolean on_dbus_call_register_item(GrapheneStatusNotifierWatcher *self, GDBusMethodInvocation *invocation, const gchar *service, gpointer object);
static void update_item_list(GrapheneStatusNotifierWatcher *self);
static void on_item_vanished(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierWatcher *self);
static void remove_item(GrapheneStatusNotifierWatcher *self, const gchar *service);
static gboolean on_dbus_call_register_host(GrapheneStatusNotifierWatcher *self, GDBusMethodInvocation *invocation, const gchar *service, gpointer object);
static void on_host_vanished(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierWatcher *self);
static void remove_host(GrapheneStatusNotifierWatcher *self, const gchar *service);


G_DEFINE_TYPE(GrapheneStatusNotifierWatcher, graphene_status_notifier_watcher, G_TYPE_OBJECT)


GrapheneStatusNotifierWatcher * graphene_status_notifier_watcher_new(void)
{
	return GRAPHENE_STATUS_NOTIFIER_WATCHER(g_object_new(GRAPHENE_TYPE_STATUS_NOTIFIER_WATCHER, NULL));
}

static void graphene_status_notifier_watcher_class_init(GrapheneStatusNotifierWatcherClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_status_notifier_watcher_dispose;
}

static void on_get_bus_connection(UNUSED GObject *source, GAsyncResult *res, gpointer userdata)
{
	GrapheneStatusNotifierWatcher *self = GRAPHENE_STATUS_NOTIFIER_WATCHER(userdata);

	GError *error = NULL;
	self->connection = g_bus_get_finish(res, &error);
	if(!self->connection || error)
	{
		g_clear_object(&self->connection);
		g_clear_error(&error);
		return;	
	}

	self->watcherObject = dbus_freedesktop_status_notifier_watcher_skeleton_new();
	self->kdeWatcherObject = dbus_kde_status_notifier_watcher_skeleton_new();
	
	g_signal_connect_swapped(self->watcherObject, "handle-register-status-notifier-item", G_CALLBACK(on_dbus_call_register_item), self);
	g_signal_connect_swapped(self->watcherObject, "handle-register-status-notifier-host", G_CALLBACK(on_dbus_call_register_host), self);
	g_signal_connect_swapped(self->kdeWatcherObject, "handle-register-status-notifier-item", G_CALLBACK(on_dbus_call_register_item), self);
	g_signal_connect_swapped(self->kdeWatcherObject, "handle-register-status-notifier-host", G_CALLBACK(on_dbus_call_register_host), self);
	
	dbus_freedesktop_status_notifier_watcher_set_protocol_version(self->watcherObject, STATUSNOTIFIER_PROTOCOL_VERSION);
	dbus_freedesktop_status_notifier_watcher_set_is_status_notifier_host_registered(self->watcherObject, TRUE);
	dbus_kde_status_notifier_watcher_set_protocol_version(self->kdeWatcherObject, STATUSNOTIFIER_PROTOCOL_VERSION);
	dbus_kde_status_notifier_watcher_set_is_status_notifier_host_registered(self->kdeWatcherObject, TRUE);
	update_item_list(self);
	
	if(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->watcherObject), self->connection, STATUSNOTIFIER_WATCHER_DBUS_PATH, NULL))
		self->dbusNameId = g_bus_own_name_on_connection(self->connection, STATUSNOTIFIER_WATCHER_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, self, NULL);
	
	if(g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->kdeWatcherObject), self->connection, STATUSNOTIFIER_WATCHER_DBUS_PATH, NULL))
		self->kdeDBusNameId = g_bus_own_name_on_connection(self->connection, STATUSNOTIFIER_WATCHER_KDE_DBUS_NAME, G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, self, NULL);
}

static void graphene_status_notifier_watcher_init(GrapheneStatusNotifierWatcher *self)
{
	self->items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL); // Call remove_item(key) to remove entries
	self->hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL); // Call remove_host(key) to remove entries

	self->cancellable = g_cancellable_new();
	g_bus_get(G_BUS_TYPE_SESSION, self->cancellable, on_get_bus_connection, self);
}

static void graphene_status_notifier_watcher_dispose(GObject *self_)
{
	GrapheneStatusNotifierWatcher *self = GRAPHENE_STATUS_NOTIFIER_WATCHER(self_);
	
	if(self->items)
	{
		GList *itemKeys = g_hash_table_get_keys(self->items);
		for(GList *key=itemKeys;key!=NULL;key=key->next)
			remove_item(self, (gchar *)key->data);
		g_list_free(itemKeys);
		
		g_hash_table_unref(self->items);
		self->items = NULL;
	}
	if(self->hosts)
	{
		GList *hostKeys = g_hash_table_get_keys(self->hosts);
		for(GList *key=hostKeys;key!=NULL;key=key->next)
			remove_host(self, (gchar *)key->data);
		g_list_free(hostKeys);
		
		g_hash_table_unref(self->hosts);
		self->hosts = NULL;
	}
	
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	
	if(self->dbusNameId)
		g_bus_unown_name(self->dbusNameId);
	self->dbusNameId = 0;
	if(self->kdeDBusNameId)
		g_bus_unown_name(self->kdeDBusNameId);
	self->kdeDBusNameId = 0;
	
	g_clear_object(&self->watcherObject);
	g_clear_object(&self->kdeWatcherObject);
	g_clear_object(&self->connection);

	G_OBJECT_CLASS(graphene_status_notifier_watcher_parent_class)->dispose(self_);
}

// These callbacks can be called from both the freedesktop and KDE versions of the interface. The callbacks use the freedesktop version of the
// methods to complete methods and emit events, but since the freedesktop and KDE versions are the same, either type can be used.

static gboolean on_dbus_call_register_item(GrapheneStatusNotifierWatcher *self, GDBusMethodInvocation *invocation,
  const gchar *service, gpointer object)
{
	// TODO: Test if starts with org.freedesktop/kde.StatusNotifierItem
	
	guint watchId = 0;
	if(self->connection)
		watchId = g_bus_watch_name_on_connection(self->connection, service, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, (GBusNameVanishedCallback)on_item_vanished, self, NULL);
	
	g_hash_table_insert(self->items, g_strdup(service), GUINT_TO_POINTER(watchId));
	update_item_list(self);
	
	dbus_freedesktop_status_notifier_watcher_emit_status_notifier_item_registered(self->watcherObject, service);
	dbus_kde_status_notifier_watcher_emit_status_notifier_item_registered(self->kdeWatcherObject, service);
	
	dbus_freedesktop_status_notifier_watcher_complete_register_status_notifier_item(object, invocation);
	return TRUE;
}

static void update_item_list(GrapheneStatusNotifierWatcher *self)
{
	GHashTableIter iter;
	gpointer key, value;
	guint i = 0;
	g_hash_table_iter_init(&iter, self->items);
	
	gchar **list = g_new0(gchar *, g_hash_table_size(self->items));
	while(g_hash_table_iter_next(&iter, &key, &value))
		list[i++] = g_strdup((gchar *)key);
	
	dbus_freedesktop_status_notifier_watcher_set_registered_status_notifier_items(self->watcherObject, (const gchar * const *)list);
	dbus_kde_status_notifier_watcher_set_registered_status_notifier_items(self->kdeWatcherObject, (const gchar * const *)list);
	g_strfreev(list);
}

static void on_item_vanished(UNUSED GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierWatcher *self)
{
	remove_item(self, name);
}

static void remove_item(GrapheneStatusNotifierWatcher *self, const gchar *service)
{
	gchar *servicedup = g_strdup(service);
	guint watchId = GPOINTER_TO_UINT(g_hash_table_lookup(self->items, servicedup));
	g_hash_table_remove(self->items, servicedup);
	if(watchId)
		g_bus_unwatch_name(watchId);
	update_item_list(self);
	dbus_freedesktop_status_notifier_watcher_emit_status_notifier_item_unregistered(self->watcherObject, servicedup);
	dbus_kde_status_notifier_watcher_emit_status_notifier_item_unregistered(self->kdeWatcherObject, servicedup);
	g_free(servicedup);
}

static gboolean on_dbus_call_register_host(GrapheneStatusNotifierWatcher *self, GDBusMethodInvocation *invocation,
  const gchar *service, gpointer object)
{
	guint watchId = 0;
	if(self->connection)
		watchId = g_bus_watch_name_on_connection(self->connection, service, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, (GBusNameVanishedCallback)on_host_vanished, self, NULL);
	
	g_hash_table_insert(self->hosts, g_strdup(service), GUINT_TO_POINTER(watchId));
	dbus_freedesktop_status_notifier_watcher_set_is_status_notifier_host_registered(self->watcherObject, TRUE);
	dbus_kde_status_notifier_watcher_set_is_status_notifier_host_registered(self->kdeWatcherObject, TRUE);
	
	if(g_hash_table_size(self->hosts) == 1)
	{
		dbus_freedesktop_status_notifier_watcher_emit_status_notifier_host_registered(self->watcherObject);
		dbus_kde_status_notifier_watcher_emit_status_notifier_host_registered(self->kdeWatcherObject);
	}
	
	dbus_freedesktop_status_notifier_watcher_complete_register_status_notifier_host(object, invocation);
	return TRUE;
}

static void on_host_vanished(UNUSED GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierWatcher *self)
{
	remove_host(self, name);
}

static void remove_host(GrapheneStatusNotifierWatcher *self, const gchar *service)
{
	guint watchId = GPOINTER_TO_UINT(g_hash_table_lookup(self->hosts, service));
	g_hash_table_remove(self->hosts, service);
	if(watchId)
		g_bus_unwatch_name(watchId);
	  
	if(g_hash_table_size(self->hosts) == 0)
	{
		dbus_freedesktop_status_notifier_watcher_set_is_status_notifier_host_registered(self->watcherObject, FALSE);
		dbus_freedesktop_status_notifier_watcher_emit_status_notifier_host_unregistered(self->watcherObject);
		dbus_kde_status_notifier_watcher_set_is_status_notifier_host_registered(self->kdeWatcherObject, FALSE);
		dbus_kde_status_notifier_watcher_emit_status_notifier_host_unregistered(self->kdeWatcherObject);
	}
}
