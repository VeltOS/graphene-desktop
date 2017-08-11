/*
 * This file is part of csk-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "battery.h"
#include <gio/gio.h>

struct _CskBatteryInfo
{
	GObject parent;
	
	GDBusProxy *batteryDeviceProxy;
	guint batteryRefreshTimerId;
};

enum
{
	SIGNAL_0,
	SIGNAL_UPDATE,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static void csk_battery_info_dispose(GObject *self_);
static gboolean refresh_battery_info(CskBatteryInfo *self);
static void on_upproxy_display_device_property_changed(CskBatteryInfo *self, GVariant *changed_properties, GStrv invalidated_properties, GDBusProxy *proxy);
static gchar * get_icon_name(CskBatteryInfo *self);


G_DEFINE_TYPE(CskBatteryInfo, csk_battery_info, G_TYPE_OBJECT)


CskBatteryInfo * csk_battery_info_new(void)
{
	return CSK_BATTERY_INFO(g_object_new(CSK_TYPE_BATTERY_INFO, NULL));
}

CskBatteryInfo * csk_battery_info_get_default(void)
{
	static CskBatteryInfo *self = NULL;
	if(!self)
	{
		self = csk_battery_info_new();
		g_object_add_weak_pointer(G_OBJECT(self), (void **)&self);
		return self;
	}
	return g_object_ref(self);
}

static void csk_battery_info_class_init(CskBatteryInfoClass *class)
{
	G_OBJECT_CLASS(class)->dispose = csk_battery_info_dispose;
	
	/*
	 * Emitted when the status of the battery changes
	 */ 
	signals[SIGNAL_UPDATE] = g_signal_new("update", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void csk_battery_info_init(CskBatteryInfo *self)
{
	GError *error = NULL;
	self->batteryDeviceProxy = g_dbus_proxy_new_for_bus_sync(
		G_BUS_TYPE_SYSTEM,
		0,
		NULL,
		"org.freedesktop.UPower",
		"/org/freedesktop/UPower/devices/DisplayDevice",
		"org.freedesktop.UPower.Device",
		NULL,
		&error);
	
	if(!self->batteryDeviceProxy)
	{
		g_warning("Failed to connect to UPower display device: %s", error->message);
		g_clear_error(&error);
		return;
	}
	
		g_signal_connect_swapped(self->batteryDeviceProxy, "g-properties-changed", G_CALLBACK(on_upproxy_display_device_property_changed), self);
		
		self->batteryRefreshTimerId = g_timeout_add_seconds(10, (GSourceFunc)refresh_battery_info, self);
		refresh_battery_info(self);
}

static void csk_battery_info_dispose(GObject *self_)
{
	CskBatteryInfo *self = CSK_BATTERY_INFO(self_);
	g_clear_object(&self->batteryDeviceProxy);
	if(self->batteryRefreshTimerId)
		g_source_remove(self->batteryRefreshTimerId);
	self->batteryRefreshTimerId = 0;
	G_OBJECT_CLASS(csk_battery_info_parent_class)->dispose(self_);
}

static gboolean refresh_battery_info(CskBatteryInfo *self)
{
	if(self->batteryDeviceProxy)
		g_dbus_proxy_call(self->batteryDeviceProxy, "Refresh", NULL, G_DBUS_CALL_FLAGS_NONE, 100, NULL, NULL, NULL);
	return TRUE; // true to continue the timer callback
}

gboolean csk_battery_info_is_available(CskBatteryInfo *self)
{
	gboolean refSelf = FALSE;
	if(!self)
	{
		self = csk_battery_info_get_default();
		refSelf = TRUE;
	}
	
	if(!G_IS_DBUS_PROXY(self->batteryDeviceProxy))
	{
		if(refSelf)
			g_object_unref(self);
		return FALSE;
	}
	
	guint32 deviceType = 0;
	GVariant *typeVariant = g_dbus_proxy_get_cached_property(self->batteryDeviceProxy, "Type");
	if(typeVariant)
	{
		deviceType = g_variant_get_uint32(typeVariant);
		g_variant_unref(typeVariant);
	}
	
	if(refSelf)
		g_object_unref(self);
	
	// 0: Unknown, 1: Line Power, 2: Battery, 3: Ups, 4: Monitor, 5: Mouse, 6: Keyboard, 7: Pda, 8: Phone
	return deviceType == 2;
}
gdouble csk_battery_info_get_percent(CskBatteryInfo *self)
{
	g_return_val_if_fail(csk_battery_info_is_available(self), 0.0);
	
	gdouble percentage = 0.0;
	GVariant *percentageVariant = g_dbus_proxy_get_cached_property(self->batteryDeviceProxy, "Percentage");
	if(percentageVariant)
	{
		percentage = g_variant_get_double(percentageVariant);
		g_variant_unref(percentageVariant);
	}
	
	return percentage;
}
guint32 csk_battery_info_get_state(CskBatteryInfo *self)
{
	// 0: Unknown, 1: Charging, 2: Discharging, 3: Empty, 4: Fully charged, 5: Pending charge, 6: Pending discharge
	
	g_return_val_if_fail(csk_battery_info_is_available(self), 0);
	
	guint32 state = 0;
	GVariant *stateVariant = g_dbus_proxy_get_cached_property(self->batteryDeviceProxy, "State");
	if(stateVariant)
	{
		state = g_variant_get_uint32(stateVariant);
		g_variant_unref(stateVariant);
	}
	
	return state;
}
const gchar * csk_battery_info_get_state_string(CskBatteryInfo *self)
{
	g_return_val_if_fail(csk_battery_info_is_available(self), "Not Available");
	
	guint32 state = csk_battery_info_get_state(self);
	switch(state)
	{
		case 1: case 5: return "Charging";
		case 2: case 6: return "Discharging";
		case 3: return "Empty";
		case 4: return "Fully Charged";
		default: return "Not Available";
	}
}
gchar * csk_battery_info_get_icon_name(CskBatteryInfo *self)
{
	// Returns a newly-allocated string
	
	g_return_val_if_fail(csk_battery_info_is_available(self), g_strdup("battery-full-charged-symbolic"));
	
	GVariant *iconNameVariant = g_dbus_proxy_get_cached_property(self->batteryDeviceProxy, "IconName");
	if(iconNameVariant)
	{
		gchar *iconName = g_strdup(g_variant_get_string(iconNameVariant, NULL));
		g_variant_unref(iconNameVariant);
		return iconName;
	}
	
	return get_icon_name(self);
}
gint64 csk_battery_info_get_time(CskBatteryInfo *self)
{
	// Time until charged or time until empty, depending on state
	
	g_return_val_if_fail(csk_battery_info_is_available(self), 0);
	
	guint32 state = csk_battery_info_get_state(self);
	
	const gchar *prop = "";
	if     (state == 1) prop = "TimeToFull";
	else if(state == 2) prop = "TimeToEmpty";
	else                return 0;
	
	gint64 time = 0;
	GVariant *timeVariant = g_dbus_proxy_get_cached_property(self->batteryDeviceProxy, prop);
	if(timeVariant)
	{
		time = g_variant_get_int64(timeVariant);
		g_variant_unref(timeVariant);
	}
	
	return time;
}

static void on_upproxy_display_device_property_changed(CskBatteryInfo *self, UNUSED GVariant *changed_properties, UNUSED GStrv invalidated_properties, UNUSED GDBusProxy *proxy)
{
	g_signal_emit_by_name(self, "update");
}

static gchar * get_icon_name(CskBatteryInfo *self)
{
	gdouble percentage = csk_battery_info_get_percent(self);
	guint32 state = csk_battery_info_get_state(self);
	
	const gchar *percentageString = "empty";
	if     (percentage <= 10)  percentageString = "empty";
	else if(percentage <= 35)  percentageString = "low";
	else if(percentage <= 75)  percentageString = "good";
	else                       percentageString = "full";
	
	const gchar *stateString = "";
	if     (state == 4) { percentageString = "full"; stateString = "-charged"; }
	else if(state == 1) stateString = "-charging";

	return g_strdup_printf("battery-%s%s-symbolic", percentageString, stateString);
}
