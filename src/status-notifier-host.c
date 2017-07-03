/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * See status-notifier-watcher.c for a summary of the strangeness that is the status notificer spec(s).
 * 
 * This class uses DBus to communicate with another object (the watcher)
 * within the same GrapheneDesktop program. It could be optimized to
 * communicate directly, although that would make the code work less like the
 * StatusNotifier spec and also more spaghetti code.
 * 
 * TODO: Support more features than just the app's icon
 */

#include "status-notifier-host.h"
#include "status-notifier-dbus-ifaces.h"
#include <libcmk/cmk.h>

#define STATUSNOTIFIER_WATCHER_DBUS_NAME "org.freedesktop.StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_DBUS_PATH "/StatusNotifierWatcher"

#define STATUSNOTIFIER_HOST_DBUS_NAME_BASE "org.freedesktop.StatusNotifierHost"

#define STATUSNOTIFIER_ITEM_DBUS_IFACE "org.freedesktop.StatusNotifierItem"
#define STATUSNOTIFIER_KDE_ITEM_DBUS_IFACE "org.kde.StatusNotifierItem"
#define STATUSNOTIFIER_ITEM_DBUS_PATH "/StatusNotifierItem"

struct _GrapheneStatusNotifierHost
{
	CmkWidget parent;

	gchar *dbusName;
	gboolean ownsName;
	guint dbusNameId;
	DBusFreedesktopStatusNotifierWatcher *dbusWatcherProxy;
	GCancellable *cancellable;
	GHashTable *items; // string (item dbus name) : StatusNotifierItem
};

typedef struct
{
	GrapheneStatusNotifierHost *host;
	gchar *service;
	CmkButton *button;
	CmkIcon *icon; // Not refed
	GDBusConnection *connection;
	guint newIconSignalId, styleChangedSignalId;
	guint activateSignalId, scrollSignalId;
	GCancellable *iconUpdateCancellable;
	gchar *interface; // It could be freedesktop or KDE
} StatusNotifierItem;


static void graphene_status_notifier_host_dispose(GObject *self_);
static void on_watcher_proxy_ready(GObject *source, GAsyncResult *res, GrapheneStatusNotifierHost *self);
static void on_watcher_proxy_owner_changed(GrapheneStatusNotifierHost *self, GParamSpec *spec, DBusFreedesktopStatusNotifierWatcher *proxy);
static void on_host_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierHost *self);
static void on_host_name_lost(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierHost *self);
static void tryRegisterHost(GrapheneStatusNotifierHost *self);
static void unregisterHost(GrapheneStatusNotifierHost *self);
static void on_item_registered(GrapheneStatusNotifierHost *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy);
static void on_item_unregistered(GrapheneStatusNotifierHost *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy);
static void free_item(StatusNotifierItem *item);

G_DEFINE_TYPE(GrapheneStatusNotifierHost, graphene_status_notifier_host, CMK_TYPE_WIDGET)


GrapheneStatusNotifierHost * graphene_status_notifier_host_new(void)
{
	return GRAPHENE_STATUS_NOTIFIER_HOST(g_object_new(GRAPHENE_TYPE_STATUS_NOTIFIER_HOST, NULL));
}

static void graphene_status_notifier_host_class_init(GrapheneStatusNotifierHostClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_status_notifier_host_dispose;
}

static void graphene_status_notifier_host_init(GrapheneStatusNotifierHost *self)
{
	self->cancellable = g_cancellable_new();
	self->items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)free_item);

	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), clutter_box_layout_new());
	
	self->dbusName = g_strdup_printf("%s-%i-%i", STATUSNOTIFIER_HOST_DBUS_NAME_BASE, getpid(), g_random_int());
	self->dbusNameId = g_bus_own_name(G_BUS_TYPE_SESSION,
		self->dbusName,
		G_BUS_NAME_OWNER_FLAGS_REPLACE,
		NULL,
		(GBusNameAcquiredCallback)on_host_name_acquired,
		(GBusNameLostCallback)on_host_name_lost,
		self,
		NULL);
	dbus_freedesktop_status_notifier_watcher_proxy_new_for_bus(
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		STATUSNOTIFIER_WATCHER_DBUS_NAME,
		STATUSNOTIFIER_WATCHER_DBUS_PATH,
		self->cancellable,
		(GAsyncReadyCallback)on_watcher_proxy_ready,
		self);
}

static void graphene_status_notifier_host_dispose(GObject *self_)
{
	GrapheneStatusNotifierHost *self = GRAPHENE_STATUS_NOTIFIER_HOST(self_);
	
	if(self->cancellable)
		g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	g_clear_object(&self->dbusWatcherProxy);
	if(self->dbusNameId)
		g_bus_unown_name(self->dbusNameId);
	self->dbusNameId = 0;
	g_clear_pointer(&self->dbusName, g_free);
	
	unregisterHost(self);
	g_hash_table_destroy(self->items);

	G_OBJECT_CLASS(graphene_status_notifier_host_parent_class)->dispose(self_);
}

static void on_watcher_proxy_ready(GObject *source, GAsyncResult *res, GrapheneStatusNotifierHost *self)
{
	self->dbusWatcherProxy = dbus_freedesktop_status_notifier_watcher_proxy_new_finish(res, NULL);
	if(!self->dbusWatcherProxy)
		return;

	g_signal_connect_swapped(self->dbusWatcherProxy, "notify::g-name-owner", G_CALLBACK(on_watcher_proxy_owner_changed), self);
	g_signal_connect_swapped(self->dbusWatcherProxy, "status-notifier-item-registered", G_CALLBACK(on_item_registered), self);
	g_signal_connect_swapped(self->dbusWatcherProxy, "status-notifier-item-unregistered", G_CALLBACK(on_item_unregistered), self);

	tryRegisterHost(self);
}

static void on_watcher_proxy_owner_changed(GrapheneStatusNotifierHost *self, GParamSpec *spec, DBusFreedesktopStatusNotifierWatcher *proxy)
{
	tryRegisterHost(self);
}

static void on_host_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierHost *self)
{
	self->ownsName = TRUE;
	tryRegisterHost(self);
}

static void on_host_name_lost(GDBusConnection *connection, const gchar *name, GrapheneStatusNotifierHost *self)
{
	self->ownsName = FALSE;
	tryRegisterHost(self);
}

static void tryRegisterHost(GrapheneStatusNotifierHost *self)
{
	if(!self->dbusWatcherProxy
	|| !g_dbus_proxy_get_name_owner(G_DBUS_PROXY(self->dbusWatcherProxy))
	|| !self->ownsName)
	{
		// Make sure we're unregistered
		unregisterHost(self);
		return;
	}

	// Add any items that already exist
	const gchar * const *items = dbus_freedesktop_status_notifier_watcher_get_registered_status_notifier_items(self->dbusWatcherProxy);
	for(guint i=0; items[i] != NULL; ++i)
		on_item_registered(self, items[i], self->dbusWatcherProxy);
	
	// Register as a host
	dbus_freedesktop_status_notifier_watcher_call_register_status_notifier_host(
		self->dbusWatcherProxy,
		self->dbusName,
		NULL, NULL, NULL);
}

static void unregisterHost(GrapheneStatusNotifierHost *self)
{
	// There is no way to "unregister" from the Watcher, but the only times this
	// will be called is if the Watcher doesn't/no longer exists or if the
	// host's name is lost. In both cases, the Watcher has already unregistered
	// us, so we just need to drop all the items we own.
	g_hash_table_remove_all(self->items);
}


static void item_update_icon(GDBusConnection *connection, const gchar *senderName, const gchar *objectPath, const gchar *interfaceName, const gchar *signalName, GVariant *parameters, StatusNotifierItem *item);
static void item_update_icon_try_named(GDBusConnection *connection, GAsyncResult *res, StatusNotifierItem *item);
static void item_update_icon_try_pixmap(GDBusConnection *connection, GAsyncResult *res, StatusNotifierItem *item);
static guchar * icon_variant_array_to_best_icon(GVariant *variant, guint sizeRequest, guint *oSize, guint *oFrames);
static void on_icon_style_changed(CmkIcon *icon, StatusNotifierItem *item);
static void on_item_activate(CmkButton *button, StatusNotifierItem *item);
static gboolean on_item_scroll(CmkButton *button, const ClutterEvent *event, StatusNotifierItem *item);

static void free_item(StatusNotifierItem *item)
{
	g_signal_handler_disconnect(item->icon, item->styleChangedSignalId);
	g_signal_handler_disconnect(item->button, item->activateSignalId);
	g_signal_handler_disconnect(item->button, item->scrollSignalId);
	g_cancellable_cancel(item->iconUpdateCancellable);
	g_clear_object(&item->iconUpdateCancellable);
	clutter_actor_destroy(CLUTTER_ACTOR(item->button));
	g_dbus_connection_signal_unsubscribe(item->connection, item->newIconSignalId);
	g_free(item->service);
	g_free(item->interface);
	g_free(item);
}

static void on_item_registered(GrapheneStatusNotifierHost *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy)
{
	StatusNotifierItem *item = g_new0(StatusNotifierItem, 1);
	item->host = self;
	item->service = g_strdup(service);
	item->connection = g_dbus_proxy_get_connection(G_DBUS_PROXY(proxy));

	item->icon = cmk_icon_new_from_name("", 24);

	item->button = cmk_button_new();
	clutter_actor_hide(CLUTTER_ACTOR(item->button));
	cmk_button_set_content(item->button, CMK_WIDGET(item->icon));
	item->activateSignalId = g_signal_connect(item->button, "activate", G_CALLBACK(on_item_activate), item);
	item->scrollSignalId = g_signal_connect(item->button, "scroll-event", G_CALLBACK(on_item_scroll), item);

	item->styleChangedSignalId = g_signal_connect(item->icon, "style-changed", G_CALLBACK(on_icon_style_changed), item);

	item->newIconSignalId = g_dbus_connection_signal_subscribe(
		item->connection,
		service,
		NULL, // Passing NULL for interface allows signals from both freedesktop and KDE interfaces
		"NewIcon",
		STATUSNOTIFIER_ITEM_DBUS_PATH,
		NULL,
		G_DBUS_SIGNAL_FLAGS_NONE,
		(GDBusSignalCallback)item_update_icon,
		item,
		NULL);

	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(item->button));
	g_hash_table_insert(self->items, g_strdup(service), item);

	item_update_icon(item->connection, NULL, NULL, STATUSNOTIFIER_ITEM_DBUS_IFACE, NULL, NULL, item);
	item_update_icon(item->connection, NULL, NULL, STATUSNOTIFIER_KDE_ITEM_DBUS_IFACE, NULL, NULL, item);
}

static void on_item_unregistered(GrapheneStatusNotifierHost *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy)
{
	g_hash_table_remove(self->items, service);
}

static void on_icon_style_changed(CmkIcon *icon, StatusNotifierItem *item)
{
	item_update_icon(item->connection, NULL, NULL, STATUSNOTIFIER_ITEM_DBUS_IFACE, NULL, NULL, item);
	item_update_icon(item->connection, NULL, NULL, STATUSNOTIFIER_KDE_ITEM_DBUS_IFACE, NULL, NULL, item);
}

static void item_update_icon(GDBusConnection *connection,
	const gchar *senderName,
	const gchar *objectPath,
	const gchar *interfaceName,
	const gchar *signalName,
	GVariant *parameters,
	StatusNotifierItem *item)
{
	if(!item)
		return;

	if(item->iconUpdateCancellable)
		g_cancellable_cancel(item->iconUpdateCancellable);
	g_clear_object(&item->iconUpdateCancellable);
	item->iconUpdateCancellable = g_cancellable_new();
	
	g_free(item->interface);
	item->interface = g_strdup(interfaceName);

	// First try to get the icon from a name. If that fails, the try_named
	// will call try_pixmap instead.
	g_dbus_connection_call(connection,
		item->service,
		STATUSNOTIFIER_ITEM_DBUS_PATH,
		"org.freedesktop.DBus.Properties",
		"Get",
		g_variant_new("(ss)", interfaceName, "IconName"),
		G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		item->iconUpdateCancellable,
		(GAsyncReadyCallback)item_update_icon_try_named,
		item);
}

static gboolean str_null_or_whitespace(const gchar *str)
{
	if(!str)
		return TRUE;
	
	gchar *dup = g_strdup(str);
	dup = g_strstrip(dup); // Modifies string in-place
	
	gboolean ret = (g_strcmp0(dup, "") == 0);
	g_free(dup);
	return ret;
}

// See if the SN item has a named icon. If not, request a pixmap icon/animation.
static void item_update_icon_try_named(GDBusConnection *connection, GAsyncResult *res, StatusNotifierItem *item)
{
	// Check return value for a valid icon name
	GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
	if(ret && g_variant_is_of_type(ret, G_VARIANT_TYPE("(v)")))
	{
		GVariant *iconNameVariantBoxed = g_variant_get_child_value(ret, 0);
		GVariant *iconNameVariant = g_variant_get_variant(iconNameVariantBoxed);
		if(g_variant_is_of_type(iconNameVariant, G_VARIANT_TYPE("s")))
		{
			const gchar *iconName = g_variant_get_string(iconNameVariant, NULL);
			if(!str_null_or_whitespace(iconName))
			{
				// Set the icon
				cmk_icon_set_icon(item->icon, iconName);
				clutter_actor_show(CLUTTER_ACTOR(item->button));
				g_variant_unref(iconNameVariant);
				g_variant_unref(iconNameVariantBoxed);
				g_variant_unref(ret);
				return;
			}
		}
		g_clear_pointer(&iconNameVariant, g_variant_unref);
		g_clear_pointer(&iconNameVariantBoxed, g_variant_unref);
	}
	g_clear_pointer(&ret, g_variant_unref);
	
	// If the icon wasn't valid, try a pixmap instead
	g_dbus_connection_call(connection,
		item->service,
		STATUSNOTIFIER_ITEM_DBUS_PATH,
		"org.freedesktop.DBus.Properties",
		"Get",
		g_variant_new("(ss)", item->interface, "IconPixmap"),
		G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		item->iconUpdateCancellable,
		(GAsyncReadyCallback)item_update_icon_try_pixmap,
		item);
}

static void item_update_icon_try_pixmap(GDBusConnection *connection, GAsyncResult *res, StatusNotifierItem *item)
{
	// Check return value for a valid pixmap
	GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
	if(ret && g_variant_is_of_type(ret, G_VARIANT_TYPE("(v)")))
	{
		GVariant *iconPixmapVariantBoxed = g_variant_get_child_value(ret, 0);
		GVariant *iconPixmapVariant = g_variant_get_variant(iconPixmapVariantBoxed);
		if(iconPixmapVariant && g_variant_is_of_type(iconPixmapVariant, G_VARIANT_TYPE("a(iiay)")))
		{
			guint size, frames;
			guchar *anim = icon_variant_array_to_best_icon(iconPixmapVariant, CMK_DP(item->icon, 24), &size, &frames);

			if(anim)
			{
				cmk_icon_set_pixmap(item->icon, anim, CAIRO_FORMAT_ARGB32, size, frames, 12);
				cmk_icon_set_size(item->icon, MIN(size, 20));

				clutter_actor_show(CLUTTER_ACTOR(item->button));
				g_free(anim);
				g_variant_unref(iconPixmapVariant);
				g_variant_unref(iconPixmapVariantBoxed);
				g_variant_unref(ret);
				return;
			}
		}
		g_clear_pointer(&iconPixmapVariant, g_variant_unref);
		g_clear_pointer(&iconPixmapVariantBoxed, g_variant_unref);
	}
	g_clear_pointer(&ret, g_variant_unref);
}

guchar * icon_variant_array_to_best_icon(GVariant *variant, guint sizeRequest, guint *oSize, guint *oFrames)
{
	*oSize = 0;
	*oFrames = 0;
	if(!g_variant_check_format_string(variant, "a(iiay)", FALSE))
		return NULL;
	if(g_variant_n_children(variant) == 0)
		return NULL;

	// Find the icon with the best width and height
	gint bestWidth = 0, bestHeight = 0;
	{
    	guint bestMatchDist = G_MAXUINT;
    	gint width=-1, height=-1;
    	GVariantIter iter;
    	g_variant_iter_init(&iter, variant);
    	while(g_variant_iter_loop(&iter, "(iiay)", &width, &height, NULL))
		{
			gint size = MAX(width, height);
			guint dist = ABS(size - (gint)sizeRequest);
			if(dist < bestMatchDist)
			{
				bestMatchDist = dist;
				bestWidth = width;
				bestHeight = height;
			}
		}
	}

	if(bestWidth < 0 || bestHeight < 0)
		return NULL;

	*oSize = MAX(bestWidth, bestHeight);
	if(*oSize <= 0)
		return NULL;

	// Find how many icons have the best width and height in
	// order to see how many frames the animation will have.
	{
		gint width=0, height=0;
		GVariantIter iter;
		g_variant_iter_init(&iter, variant);
		while(g_variant_iter_loop(&iter, "(iiay)", &width, &height, NULL))
		{
			if(width != bestWidth || height != bestHeight)
				continue;
			(*oFrames)++;
		}
	}

	if(*oFrames == 0)
		return NULL;
	
	gboolean vCenter = (bestWidth < bestHeight);
	guint frameSize = (*oSize)*(*oSize)*4;
	guint dataLen = frameSize*(*oFrames);
	guchar *data = g_new0(guchar, dataLen);
	g_message("size: %i", *oSize);

	{
		guint i=0;
		gint width=0, height=0;
		GVariantIter iter;
		GVariantIter *byteIter;
		g_variant_iter_init(&iter, variant);
		while(g_variant_iter_loop(&iter, "(iiay)", &width, &height, &byteIter))
		{
			if(width != bestWidth || height != bestHeight)
				continue;
			if(i>=(*oFrames))
				break;
			g_message("(w: %i, h: %i)", width, height);
			
			gsize numBytes = g_variant_iter_n_children(byteIter);
			if(numBytes > frameSize)
				continue;
		
			guint stride = width*4;

			guint j=0;
			guchar byte=0;
			while(g_variant_iter_loop(byteIter, "y", &byte))
			{
				guint row = j/stride;
				guint col = j%stride;
				// TODO: Center in square
				//data[frameSize*i + row*(*oSize) + col] = byte;
				data[frameSize*i + j] = byte;
				j++;
			}

			i++;
		}
	}
	
	// Match the CAIRO_FORMAT_ARGB32 format
	uint32_t *data32 = (uint32_t *)data;
	guint sqsize = (*oSize)*(*oSize);
	uint32_t a,r,g,b;
	for(guint i=0, j=0;i<sqsize; ++i, j+=4)
	{
		// Premultiply alpha and reorder from ARGB network byte order to native-endian
		a = data[j+0], r = data[j+1], g = data[j+2], b = data[j+3];
		r = (r*a)/255;
		g = (g*a)/255;
		b = (b*a)/255;
		data32[i] = (a << 24) | (r << 16) | (g << 8) | b;
	}

	return data;
}

static void on_item_activate(CmkButton *button, StatusNotifierItem *item)
{
	const ClutterEvent *event = clutter_get_current_event();
	if(clutter_event_type(event) != CLUTTER_BUTTON_RELEASE)
		return;
	
	const gchar *method = NULL;
	
	guint32 num = clutter_event_get_button(event);
	if(num == CLUTTER_BUTTON_PRIMARY)
		method = "Activate";
	else if(num == CLUTTER_BUTTON_MIDDLE)
		method = "SecondaryActivate";
	else if(num == CLUTTER_BUTTON_SECONDARY)
		method = "ContextMenu";
	
	if(method)
	{
		ClutterPoint point;
		clutter_event_get_position(event, &point);

		g_dbus_connection_call(
			item->connection,
			item->service,
			STATUSNOTIFIER_ITEM_DBUS_PATH,
			STATUSNOTIFIER_ITEM_DBUS_IFACE,
			method,
			g_variant_new("(ii)", point.x, point.y),
			NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

		g_dbus_connection_call(
			item->connection,
			item->service,
			STATUSNOTIFIER_ITEM_DBUS_PATH,
			STATUSNOTIFIER_KDE_ITEM_DBUS_IFACE,
			method,
			g_variant_new("(ii)", point.x, point.y),
			NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
	}
}

static gboolean on_item_scroll(CmkButton *button, const ClutterEvent *event, StatusNotifierItem *item)
{
	if(clutter_event_type(event) != CLUTTER_SCROLL)
		return TRUE;

	ClutterScrollDirection direction = clutter_event_get_scroll_direction(event);

	gint delta = 0;
	const gchar *orientation = NULL;
	
	if(direction == CLUTTER_SCROLL_UP || direction == CLUTTER_SCROLL_DOWN)
	{
		orientation = "vertical";
		delta = (direction == CLUTTER_SCROLL_UP) ? 1 : -1;
	}
	else if(direction == CLUTTER_SCROLL_LEFT || direction == CLUTTER_SCROLL_RIGHT)
	{
		orientation = "horizontal";
		delta = (direction == CLUTTER_SCROLL_RIGHT) ? 1 : -1;
	}
	else if(direction == CLUTTER_SCROLL_SMOOTH)
	{
		gdouble dx, dy;
		clutter_event_get_scroll_delta(event, &dx, &dy);
		orientation = (dx > dy) ? "horizontal" : "vertical";
		delta = (dx > dy) ? dx : dy;
	}
	
	if(orientation)
	{
		g_dbus_connection_call(
			item->connection,
			item->service,
			STATUSNOTIFIER_ITEM_DBUS_PATH,
			STATUSNOTIFIER_ITEM_DBUS_IFACE,
			"Scroll",
			g_variant_new("(is)", delta, orientation),
			NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

		g_dbus_connection_call(
			item->connection,
			item->service,
			STATUSNOTIFIER_ITEM_DBUS_PATH,
			STATUSNOTIFIER_KDE_ITEM_DBUS_IFACE,
			"Scroll",
			g_variant_new("(is)", delta, orientation),
			NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
		return TRUE;
	}
	return FALSE;
}
