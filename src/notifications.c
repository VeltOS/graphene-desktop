/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "notifications.h"
#include <cmk/cmk.h>
#include <notifications-dbus-iface.h>

#define NOTIFICATION_DEFAULT_SHOW_TIME 5000 // ms
#define NOTIFICATION_URGENCY_LOW 0
#define NOTIFICATION_URGENCY_NORMAL 1
#define NOTIFICATION_URGENCY_CRITICAL 2
#define NOTIFICATION_SPACING 20 // pixels
#define NOTIFICATION_WIDTH 320
#define NOTIFICATION_HEIGHT 60
#define NOTIFICATION_DBUS_IFACE "org.freedesktop.Notifications"
#define NOTIFICATION_DBUS_PATH "/org/freedesktop/Notifications"

#define GRAPHENE_TYPE_NOTIFICATION  graphene_notification_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNotification, graphene_notification, GRAPHENE, NOTIFICATION, CmkWidget)

struct _GrapheneNotificationBox
{
	CmkWidget parent;

	guint dbusNameId;
	DBusNotifications *dbusObject;
	guint32 nextNotificationId;
	guint32 failNotificationId;

	NotificationAddedCb notificationAddedCb;
	gpointer cbUserdata;
};

struct _GrapheneNotification
{
	CmkWidget parent;
	
	guint32 id;
	gint urgency;
	gint timeout;
	guint timeoutSourceId;

	CmkIcon *icon;
	ClutterText *text;
};

static void graphene_notification_box_dispose(GObject *self_);
static void post_server_fail_notification(GrapheneNotificationBox *self);
static void on_dbus_connection_acquired(GDBusConnection *connection, const gchar *name, GrapheneNotificationBox *self);
static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneNotificationBox *self);
static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, GrapheneNotificationBox *self);
static gboolean on_dbus_call_get_capabilities(GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, DBusNotifications *object);
static gboolean on_dbus_call_notify(GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, const gchar *app_name, guint replaces_id, const gchar *app_icon, const gchar *summary, const gchar *body, const gchar * const *actions, GVariant *hints, gint expire_timeout, DBusNotifications *object);
static gboolean on_dbus_call_close_notification(GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, guint id, DBusNotifications *object);
static gboolean on_dbus_call_get_server_information(GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, DBusNotifications *object);
static void add_notification(GrapheneNotificationBox *self, GrapheneNotification *n);
static gboolean remove_notification(GrapheneNotification *n);
static void graphene_notification_box_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);

GrapheneNotification * graphene_notification_new(void);
static void graphene_notification_set_timeout(GrapheneNotification *self, gint timeout);


G_DEFINE_TYPE(GrapheneNotificationBox, graphene_notification_box, CMK_TYPE_WIDGET)
G_DEFINE_TYPE(GrapheneNotification, graphene_notification, CMK_TYPE_WIDGET)


GrapheneNotificationBox * graphene_notification_box_new(NotificationAddedCb notificationAddedCb, gpointer userdata)
{
	GrapheneNotificationBox *box = GRAPHENE_NOTIFICATION_BOX(g_object_new(GRAPHENE_TYPE_NOTIFICATION_BOX, NULL));
	if(box)
	{
		box->notificationAddedCb = notificationAddedCb;
		box->cbUserdata = userdata;
	}
	return box;
}

static void graphene_notification_box_class_init(GrapheneNotificationBoxClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_notification_box_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_notification_box_allocate;
}

static void graphene_notification_box_init(GrapheneNotificationBox *self)
{
	self->nextNotificationId = 1;

	self->dbusNameId = g_bus_own_name(G_BUS_TYPE_SESSION,
		NOTIFICATION_DBUS_IFACE,
		G_BUS_NAME_OWNER_FLAGS_REPLACE,
		(GBusAcquiredCallback)on_dbus_connection_acquired,
		(GBusNameAcquiredCallback)on_dbus_name_acquired,
		(GBusNameLostCallback)on_dbus_name_lost,
		self,
		NULL);

	if(!self->dbusNameId)
		post_server_fail_notification(self);
}

static void graphene_notification_box_dispose(GObject *self_)
{
	GrapheneNotificationBox *self = GRAPHENE_NOTIFICATION_BOX(self_);

	if(self->dbusNameId)
		g_bus_unown_name(self->dbusNameId);
	self->dbusNameId = 0;

	g_clear_object(&self->dbusObject);
	G_OBJECT_CLASS(graphene_notification_box_parent_class)->dispose(self_);
}

GrapheneNotification * get_notification_by_id(GrapheneNotificationBox *self, guint id)
{
	ClutterActor *child = clutter_actor_get_first_child(CLUTTER_ACTOR(self));
	while(child)
	{
		if(GRAPHENE_IS_NOTIFICATION(child) && GRAPHENE_NOTIFICATION(child)->id == id)
			return GRAPHENE_NOTIFICATION(child);
		child = clutter_actor_get_next_sibling(child);
	}
	return NULL;
}

static void remove_server_fail_notification(GrapheneNotificationBox *self)
{
	if(!self->failNotificationId)
		return;
	GrapheneNotification *n = get_notification_by_id(self, self->failNotificationId);
	if(n)
		remove_notification(n);
	self->failNotificationId = 0;
}

static void post_server_fail_notification(GrapheneNotificationBox *self)
{
	g_warning("Notification server failed");
	remove_server_fail_notification(self);
		
	GrapheneNotification *n = graphene_notification_new();
	n->id = ++self->nextNotificationId;
	n->urgency = NOTIFICATION_URGENCY_CRITICAL;

	cmk_icon_set_icon(n->icon, "dialog-warning-symbolic");
	clutter_text_set_markup(n->text, "<b>System Notifications Failed</b>\nYou may need to relog.");

	add_notification(self, n);
}

static void on_dbus_connection_acquired(GDBusConnection *connection, G_GNUC_UNUSED const gchar *name, GrapheneNotificationBox *self)
{
	self->dbusObject = dbus_notifications_skeleton_new();
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->dbusObject), connection, NOTIFICATION_DBUS_PATH, NULL))
		post_server_fail_notification(self);
	  
	g_signal_connect_swapped(self->dbusObject, "handle-get-capabilities", G_CALLBACK(on_dbus_call_get_capabilities), self);
	g_signal_connect_swapped(self->dbusObject, "handle-notify", G_CALLBACK(on_dbus_call_notify), self);
	g_signal_connect_swapped(self->dbusObject, "handle-close-notification", G_CALLBACK(on_dbus_call_close_notification), self);
	g_signal_connect_swapped(self->dbusObject, "handle-get-server-information", G_CALLBACK(on_dbus_call_get_server_information), self);
}

static void on_dbus_name_acquired(G_GNUC_UNUSED GDBusConnection *connection, UNUSED const gchar *name, GrapheneNotificationBox *self)
{
	remove_server_fail_notification(self);
}

static void on_dbus_name_lost(UNUSED GDBusConnection *connection, UNUSED const gchar *name, GrapheneNotificationBox *self)
{
	post_server_fail_notification(self);
}


static gboolean on_dbus_call_get_capabilities(UNUSED GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, DBusNotifications *object)
{
	const gchar * const capabilities[] = {"body", "persistance", "body-markup", NULL};
	dbus_notifications_complete_get_capabilities(object, invocation, capabilities);
	return TRUE;
}

static gboolean on_dbus_call_notify(GrapheneNotificationBox *self,
	GDBusMethodInvocation *invocation,
	UNUSED const gchar *app_name, // TODO
	UNUSED guint replaces_id, // TODO
	const gchar *app_icon,
	const gchar *summary,
	const gchar *body,
	UNUSED const gchar * const *actions, // TODO
	UNUSED GVariant *hints, // TODO
	gint expire_timeout,
	DBusNotifications *object)
{
	remove_server_fail_notification(self);

	GrapheneNotification *n = graphene_notification_new();
	n->id = ++self->nextNotificationId;
	n->urgency = NOTIFICATION_URGENCY_NORMAL; // TODO: Get from hints

	cmk_icon_set_icon(n->icon, app_icon);
	gchar *text = g_strdup_printf("<b>%s</b>  %s", summary, body);
	clutter_text_set_markup(n->text, text);
	g_free(text);

	add_notification(self, n);
	graphene_notification_set_timeout(n, expire_timeout);
	
	dbus_notifications_complete_notify(object, invocation, n->id);
	return TRUE;
}

static gboolean on_dbus_call_close_notification(GrapheneNotificationBox *self, GDBusMethodInvocation *invocation, guint id, DBusNotifications *object)
{
	GrapheneNotification *n = get_notification_by_id(self, id);
	if(n)
		remove_notification(n);

	dbus_notifications_complete_close_notification(object, invocation);
	return TRUE;
}

#ifndef GRAPHENE_VERSION_STR
#define GRAPHENE_VERSION_STR ""
#endif

static gboolean on_dbus_call_get_server_information(UNUSED GrapheneNotificationBox *self, UNUSED GDBusMethodInvocation *invocation, DBusNotifications *object)
{
	dbus_notifications_complete_get_server_information(object,
		invocation,
		"Graphene Desktop", // Name
		"Velt", // Vendor
		GRAPHENE_VERSION_STR, // Version
		"1.2"); // Spec Version 
	return TRUE;
}

static void add_notification(GrapheneNotificationBox *self, GrapheneNotification *n)
{
	ClutterEffect *shadow = cmk_shadow_effect_new_drop_shadow(10, 0, 0, 1, 0);
	clutter_actor_add_effect(CLUTTER_ACTOR(n), shadow);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(n));
	if(self->notificationAddedCb)
		self->notificationAddedCb(self->cbUserdata, CLUTTER_ACTOR(n));
}

static gboolean remove_notification(GrapheneNotification *n)
{
	clutter_actor_destroy(CLUTTER_ACTOR(n));
	return G_SOURCE_REMOVE;
}

static gint notification_compare_func(gconstpointer a, gconstpointer b)
{
	// TODO: Sort "critical" notifications to the top
	return (((const GrapheneNotification *)a)->id < ((const GrapheneNotification *)b)->id) ? 1 : -1; // Sort newest to the top
}

static void graphene_notification_box_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GList *children = clutter_actor_get_children(self_);
	children = g_list_sort(children, (GCompareFunc)notification_compare_func);
	
	gfloat dp = cmk_widget_get_dp_scale(CMK_WIDGET(self_));

	guint i=0;
	for(GList *it=children; it!=NULL; it=it->next)
	{
		ClutterActor *n_ = CLUTTER_ACTOR(it->data);
		ClutterActorBox box;
		box.x1 = NOTIFICATION_SPACING;
		box.y1 = NOTIFICATION_SPACING + i*(NOTIFICATION_HEIGHT + NOTIFICATION_SPACING);
		box.x2 = box.x1 + NOTIFICATION_WIDTH;
		box.y2 = box.y1 + NOTIFICATION_HEIGHT;
		cmk_scale_actor_box(&box, dp, TRUE);

		clutter_actor_save_easing_state(n_);
		clutter_actor_set_easing_mode(n_, CLUTTER_EASE_OUT_SINE);
		clutter_actor_set_easing_duration(n_, 200); // WM_TRANSITION_TIME
		clutter_actor_allocate(n_, &box, flags);
		clutter_actor_restore_easing_state(n_);
		++i;
	}
	
	g_list_free(children);

	CLUTTER_ACTOR_CLASS(graphene_notification_box_parent_class)->allocate(self_, box, flags);
}



static void graphene_notification_dispose(GObject *self_);
static void graphene_notification_stop_timeout(GrapheneNotification *self);
static void graphene_notification_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static gboolean graphene_notification_press(ClutterActor *self_, ClutterButtonEvent *event);
static gboolean graphene_notification_enter(ClutterActor *self_, ClutterCrossingEvent *event);
static gboolean graphene_notification_leave(ClutterActor *self_, ClutterCrossingEvent *event);
static void on_styles_changed(CmkWidget *self_, guint flags);

GrapheneNotification * graphene_notification_new(void)
{
	return GRAPHENE_NOTIFICATION(g_object_new(GRAPHENE_TYPE_NOTIFICATION, NULL));
}

static void graphene_notification_class_init(GrapheneNotificationClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_notification_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_notification_allocate;
	CLUTTER_ACTOR_CLASS(class)->button_press_event = graphene_notification_press;
	CLUTTER_ACTOR_CLASS(class)->enter_event = graphene_notification_enter;
	CLUTTER_ACTOR_CLASS(class)->leave_event = graphene_notification_leave;
	CMK_WIDGET_CLASS(class)->styles_changed = on_styles_changed;
}

static void graphene_notification_init(GrapheneNotification *self)
{
	self->text = CLUTTER_TEXT(clutter_text_new());
	clutter_text_set_line_wrap(self->text, TRUE);
	clutter_text_set_ellipsize(self->text, PANGO_ELLIPSIZE_END);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->text));

	self->icon = cmk_icon_new(48);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->icon));

	clutter_actor_set_reactive(CLUTTER_ACTOR(self), TRUE);

	cmk_widget_set_draw_background_color(CMK_WIDGET(self), TRUE);
}

static void graphene_notification_dispose(GObject *self_)
{
	GrapheneNotification *self = GRAPHENE_NOTIFICATION(self_);

	graphene_notification_stop_timeout(self);

	G_OBJECT_CLASS(graphene_notification_box_parent_class)->dispose(self_);
}

static void graphene_notification_stop_timeout(GrapheneNotification *self)
{
	if(self->timeoutSourceId)
    	g_source_remove(self->timeoutSourceId);
	self->timeoutSourceId = 0;
}

static void graphene_notification_set_timeout(GrapheneNotification *self, gint timeout)
{
	graphene_notification_stop_timeout(self);
	if(timeout < 0)
		timeout = NOTIFICATION_DEFAULT_SHOW_TIME;
	self->timeout = timeout;
	if(timeout > 0)
		self->timeoutSourceId = g_timeout_add(timeout, (GSourceFunc)remove_notification, self);
}

#define WIDTH_PADDING 10
#define HEIGHT_PADDING 10

static void graphene_notification_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GrapheneNotification *self = GRAPHENE_NOTIFICATION(self_);
	gfloat padMul = cmk_widget_get_padding_multiplier(CMK_WIDGET(self_));
	gfloat dp = cmk_widget_get_dp_scale(CMK_WIDGET(self_));
	gfloat wPad = WIDTH_PADDING * dp * padMul;
	gfloat hPad = HEIGHT_PADDING * dp * padMul;

	ClutterActorBox padBox = {wPad, hPad, (box->x2-box->x1)-wPad, (box->y2-box->y1)-hPad};

	ClutterActorBox iconBox = {padBox.x1, padBox.y1, padBox.x1+48*dp, padBox.y2};
	padBox.x1 = iconBox.x2 + wPad;

	clutter_actor_allocate(CLUTTER_ACTOR(self->icon), &iconBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->text), &padBox, flags);

	return CLUTTER_ACTOR_CLASS(graphene_notification_parent_class)->allocate(self_, box, flags);
}

static gboolean graphene_notification_press(ClutterActor *self_, UNUSED ClutterButtonEvent *event)
{
	remove_notification(GRAPHENE_NOTIFICATION(self_));
	return TRUE;
}

static gboolean graphene_notification_enter(ClutterActor *self_, UNUSED ClutterCrossingEvent *event)
{
	graphene_notification_stop_timeout(GRAPHENE_NOTIFICATION(self_));
	return TRUE;
}

static gboolean graphene_notification_leave(ClutterActor *self_, UNUSED ClutterCrossingEvent *event)
{
	graphene_notification_set_timeout(GRAPHENE_NOTIFICATION(self_), GRAPHENE_NOTIFICATION(self_)->timeout);
	return TRUE;
}

static void on_styles_changed(CmkWidget *self_, guint flags)
{
	CMK_WIDGET_CLASS(graphene_notification_parent_class)->styles_changed(self_, flags);
	if(flags & CMK_STYLE_FLAG_COLORS)
	{
		const ClutterColor *color =
			cmk_widget_get_default_named_color(self_, "foreground");
		clutter_text_set_color(GRAPHENE_NOTIFICATION(self_)->text, color);
	}
}
