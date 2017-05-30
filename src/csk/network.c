/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "network.h"
#include <gio/gio.h>

#define NM_DAEMON_NAME "org.freedesktop.NetworkManager"
#define NM_DAEMON_PATH "/org/freedesktop/NetworkManager"
#define NM_DAEMON_INTERFACE "org.freedesktop.NetworkManager"

#define DO_ON_INVALID_FORMAT_STRING(v, format, d, free) \
{ \
	if(!g_variant_check_format_string((v), (format), FALSE)) \
	{ \
		g_warning("Invalid variant type string %s at " G_STRLOC " (should be %s)", (format), g_variant_get_type_string(v)); \
		if(free) g_variant_unref(v); \
		{d;} \
	} \
}

struct _CskNetworkManager
{
	GObject parent;

	GCancellable *cancellable;
	GDBusConnection *connection;
	
	gchar *icon;
	GList *devices;
	GList *readyDevices; // Devices that have completed initializing

	gchar *nmDaemonOwner;
	guint nmDaemonWatchId;
	guint nmSignalSubId;
};

struct _CskNetworkDevice
{
	GObject parent;
	
	// CskNetworkDevices should always be prepared for manager to be
	// NULL, as manger == NULL means the device has been removed but
	// someone still has a reference to it.
	CskNetworkManager *manager;
	GCancellable *cancellable;
	gboolean ready;
	
	CskNDeviceType type;
	CskNConnectionStatus status;
	gchar *interface;
	gchar *mac;
	gchar *name;
	GList *aps;
	GList *readyAps;
	
	guint nmSignalSubId;
	gchar *nmDevicePath;
};

struct _CskNetworkAccessPoint
{
	GObject parent;
	
	// CskNetworkAccessPoints should always be prepared for device
	// to be NULL.
	CskNetworkDevice *device;
	GCancellable *cancellable;
	
	CskNSecurityType security;
	CskNConnectionStatus status;
	gchar *icon;
	gchar *name;
	gchar *remoteMac;
	guint strength;
	gboolean best;
	
	guint nmSignalSubId;
	gchar *nmApPath;
};

enum
{
	MN_PROP_ICON = 1,
	MN_PROP_LAST,
	MN_SIGNAL_DEVICE_ADDED = 1,
	MN_SIGNAL_DEVICE_REMOVED,
	MN_SIGNAL_LAST,
	
	DV_PROP_DEVICE_TYPE = 1,
	DV_PROP_NAME,
	DV_PROP_MAC,
	DV_PROP_CONNECTION_STATUS,
	DV_PROP_LAST,
	DV_SIGNAL_AP_ADDED = 1,
	DV_SIGNAL_AP_REMOVED,
	DV_SIGNAL_IPS_CHANGED,
	DV_SIGNAL_LAST,
	
	AP_PROP_NAME = 1,
	AP_PROP_MAC,
	AP_PROP_STRENGTH,
	AP_PROP_SECURITY,
	AP_PROP_CONNECTION_STATUS,
	AP_PROP_ICON,
	AP_PROP_BEST,
	AP_PROP_LAST,
	AP_SIGNAL_LAST = 1,
};

static GParamSpec *managerProperties[MN_PROP_LAST];
static guint managerSignals[MN_SIGNAL_LAST];
static GParamSpec *deviceProperties[DV_PROP_LAST];
static guint deviceSignals[DV_SIGNAL_LAST];
static GParamSpec *apProperties[AP_PROP_LAST];
static guint apSignals[AP_SIGNAL_LAST];

G_DEFINE_TYPE(CskNetworkManager, csk_network_manager, G_TYPE_OBJECT)
G_DEFINE_TYPE(CskNetworkDevice, csk_network_device, G_TYPE_OBJECT)
G_DEFINE_TYPE(CskNetworkAccessPoint, csk_network_access_point, G_TYPE_OBJECT)


static void csk_network_manager_dispose(GObject *self_);
static void csk_network_manager_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void csk_network_manager_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void on_nm_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, CskNetworkManager *self);
static void on_nm_daemon_vanished(GDBusConnection *connection, const gchar *name, CskNetworkManager *self);
static void on_nm_daemon_signal(GDBusConnection *connection, const gchar *sender, const gchar *object, const gchar *interface, const gchar *signal, GVariant *parameters, CskNetworkManager *self);
static void on_nm_get_all_devices(GDBusConnection *connection, GAsyncResult *res, CskNetworkManager *self);
static void add_nm_device(CskNetworkManager *self, const gchar *devicePath);
static void remove_nm_device(CskNetworkManager *self, const gchar *devicePath);
static void csk_network_manager_remove_all_devices(CskNetworkManager *self, gboolean emit);

static void csk_network_device_self_destruct(CskNetworkDevice *self);
static void cnm_device_update_type(CskNetworkDevice *self, guint32 nmType);

static void csk_network_access_point_self_destruct(CskNetworkAccessPoint *self);


static CskNetworkManager * csk_network_manager_new(void)
{
	return CSK_NETWORK_MANAGER(g_object_new(CSK_TYPE_NETWORK_MANAGER, NULL));
}

CskNetworkManager * csk_network_manager_get_default(void)
{
	static CskNetworkManager *self = NULL;
	if(!self)
	{
		self = csk_network_manager_new();
		g_object_add_weak_pointer(G_OBJECT(self), (void **)&self);
		return self;
	}
	return g_object_ref(self);
}

static void csk_network_manager_class_init(CskNetworkManagerClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_manager_dispose;
	base->set_property = csk_network_manager_set_property;
	base->get_property = csk_network_manager_get_property;
	
	managerProperties[MN_PROP_ICON] =
		g_param_spec_string("icon",
		                    "Icon",
		                    "Icon representing overall connection status",
		                    NULL,
		                    G_PARAM_READABLE);
	
	g_object_class_install_properties(base, MN_PROP_LAST, managerProperties);
	
	managerSignals[MN_SIGNAL_DEVICE_ADDED] =
		g_signal_new("device-added",
		             G_TYPE_FROM_CLASS(class),
		             G_SIGNAL_RUN_FIRST,
		             0, NULL, NULL, NULL,
		             G_TYPE_NONE,
		             1, csk_network_device_get_type());
	
	managerSignals[MN_SIGNAL_DEVICE_REMOVED] =
		g_signal_new("device-removed",
		             G_TYPE_FROM_CLASS(class),
		             G_SIGNAL_RUN_FIRST,
		             0, NULL, NULL, NULL,
		             G_TYPE_NONE,
		             1, csk_network_device_get_type());
}

static void csk_network_manager_init(CskNetworkManager *self)
{
	self->cancellable = g_cancellable_new();
	
	self->nmDaemonWatchId = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
		NM_DAEMON_NAME,
		G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
		(GBusNameAppearedCallback)on_nm_daemon_appeared,
		(GBusNameVanishedCallback)on_nm_daemon_vanished,
		self,
		NULL);
}

static void csk_network_manager_dispose(GObject *self_)
{
	CskNetworkManager *self = CSK_NETWORK_MANAGER(self_);
	
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	if(self->nmSignalSubId)
		g_dbus_connection_signal_unsubscribe(self->connection, self->nmSignalSubId);
	if(self->nmDaemonWatchId)
		g_bus_unwatch_name(self->nmDaemonWatchId);
	self->nmSignalSubId = 0;
	self->nmDaemonWatchId = 0;
	
	csk_network_manager_remove_all_devices(self, FALSE);
	
	G_OBJECT_CLASS(csk_network_manager_parent_class)->dispose(self_);
}

static void csk_network_manager_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	switch(propertyId)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void csk_network_manager_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	CskNetworkManager *self = CSK_NETWORK_MANAGER(self_);
	switch(propertyId)
	{
	case MN_PROP_ICON:
		g_value_set_string(value, self->icon);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

const GList * csk_network_manager_get_devices(CskNetworkManager *self)
{
	g_return_val_if_fail(CSK_IS_NETWORK_MANAGER(self), NULL);
	return self->readyDevices;
}

// Multiple daemons (ie. NetworkManager and WICD) being available is a user
// error, and will likely cause neither daemon to work properly as both
// fight for control. If this happens, CskNetwork will just stop doing
// anything until the secondary daemon(s) are disabled.
static gboolean multiple_network_daemons_available(CskNetworkManager *self)
{
	// Currently only NetworkManager is checked, so there can't be multiple
	return FALSE;
}

static void on_nm_daemon_appeared(GDBusConnection *connection, const gchar *name, const gchar *owner, CskNetworkManager *self)
{
	g_message("nm daemon appeared");
	csk_network_manager_remove_all_devices(self, TRUE);
	
	// Connect to the daemon and get signals from everything the daemon
	// owns (including signals from Device and other NM objects)
	self->connection = connection;
	self->nmDaemonOwner = g_strdup(owner);
	self->nmSignalSubId = g_dbus_connection_signal_subscribe(connection,
		owner,
		NM_DAEMON_INTERFACE,
		NULL, // All signals
		NM_DAEMON_PATH,
		NULL, // All arg0s
		G_DBUS_SIGNAL_FLAGS_NONE,
		(GDBusSignalCallback)on_nm_daemon_signal,
		self,
		NULL);
	
	// Get all current devices
	g_dbus_connection_call(connection,
		NM_DAEMON_NAME,
		NM_DAEMON_PATH,
		NM_DAEMON_INTERFACE,
		"GetAllDevices",
		NULL,
		G_VARIANT_TYPE("(ao)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		self->cancellable,
		(GAsyncReadyCallback)on_nm_get_all_devices,
		self);
}

static void on_nm_daemon_vanished(GDBusConnection *connection, const gchar *name, CskNetworkManager *self)
{
	self->connection = connection;
	g_dbus_connection_signal_unsubscribe(connection, self->nmSignalSubId); 
	self->nmSignalSubId = 0;
	g_clear_pointer(&self->nmDaemonOwner, g_free);
	csk_network_manager_remove_all_devices(self, TRUE);
}

// Only for the org.freedesktop.NetworkManager interface
static void on_nm_daemon_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	CskNetworkManager *self)
{
	if(g_strcmp0(sender, self->nmDaemonOwner) != 0)
	{
		g_warning("Bad NM signal: %s, %s, %s, %s, %s", self->nmDaemonOwner, sender, object, interface, signal);
		return;
	}
	
	if(g_strcmp0(signal, "DeviceAdded") == 0)
	{
		DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return, FALSE);
		const gchar *path;
		g_variant_get(parameters, "(&o)", &path);
		add_nm_device(self, path);
	}
	else if(g_strcmp0(signal, "DeviceRemoved") == 0)
	{
		DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return, FALSE);
		const gchar *path;
		g_variant_get(parameters, "(&o)", &path);
		remove_nm_device(self, path);
	}
}

static void on_nm_get_all_devices(GDBusConnection *connection, GAsyncResult *res, CskNetworkManager *self)
{
	GError *error = NULL;
	GVariant *devicesV = g_dbus_connection_call_finish(connection, res, &error);
	if(error)
	{
		g_warning("Failed to list all NetworkManager devices: %s", error->message);
		g_error_free(error);
		return;
	}
	
	g_message("Get all nm devices");
	GVariantIter *iter = NULL;
	const gchar *devicePath = NULL;
	g_variant_get(devicesV, "(ao)", &iter);
	while(g_variant_iter_next(iter, "&o", &devicePath))
		add_nm_device(self, devicePath);
}

static void add_nm_device(CskNetworkManager *self, const gchar *devicePath)
{
	// Sometimes on_nm_get_all_devices can duplicate a DeviceAdded signal,
	// or vice versa, or check for duplicates.
	for(GList *it=self->devices; it!=NULL; it=it->next)
		if(g_strcmp0(devicePath, CSK_NETWORK_DEVICE(it->data)->nmDevicePath) == 0)
			return;
	
	g_message("Add device: %s", devicePath);
	CskNetworkDevice *device = CSK_NETWORK_DEVICE(g_object_new(CSK_TYPE_NETWORK_DEVICE, NULL));
	device->manager = self;
	device->nmDevicePath = g_strdup(devicePath);
	self->devices = g_list_prepend(self->devices, device);
	csk_network_device_init(device);
	
	// The device will become ready once the device's info has been gathered
	// and all its access points have become enumerated and their info has
	// been gathered as well. When the device becomes ready, the device-added
	// signal will be emitted.
	// The last AP to become ready will set the ready flag on the device.
	// A device may never become ready if its setup fails (for example, if it
	// is a device type unsupported by CskNetwork).
}

static void remove_nm_device(CskNetworkManager *self, const gchar *devicePath)
{
	g_message("Remove device: %s", devicePath);
	for(GList *it=self->devices; it!=NULL; it=it->next)
	{
		CskNetworkDevice *device = it->data;
		if(g_strcmp0(devicePath, device->nmDevicePath) == 0)
		{
			self->devices = g_list_delete_link(self->devices, it);
			self->readyDevices = g_list_remove(self->readyDevices, device);
			csk_network_device_self_destruct(device);
			g_signal_emit(self, managerSignals[MN_SIGNAL_DEVICE_REMOVED], 0, device);
			g_object_unref(device);
			return;
		}
	}
}

static void csk_network_manager_remove_all_devices(CskNetworkManager *self, gboolean emit)
{
	for(GList *it=self->devices; it!=NULL; it=it->next)
	{
		CskNetworkDevice *device = it->data;
		csk_network_device_self_destruct(device);
		if(emit)
			g_signal_emit(self, managerSignals[MN_SIGNAL_DEVICE_REMOVED], 0, device);
		g_object_unref(device);
	}
	
	g_clear_pointer(&self->devices, g_list_free);
	g_clear_pointer(&self->readyDevices, g_list_free);
}

// When a new device appears, check to see if any other devices
// are of the same type. If so, update them all to contain their
// interface name after their regular name so that users can tell
// them apart.
static void set_device_name_and_update_others(CskNetworkManager *self, CskNetworkDevice *device)
{
	if(!self)
		return;
	const gchar *name;
	if(device->type == CSK_NDEVICE_TYPE_WIRED)
		name = "Wired";
	else if(device->type == CSK_NDEVICE_TYPE_WIFI)
		name = "Wi-Fi";
	else if(device->type == CSK_NDEVICE_TYPE_BLUETOOTH)
		name = "Bluetooth";
	else
	{
		g_clear_pointer(&device->name, g_free);
		return;
	}
	
	gboolean others = FALSE;
	for(GList *it=self->devices; it!=NULL; it=it->next)
	{
		CskNetworkDevice *other = it->data;
		if(other == device)
			continue;
		if(other->type == device->type)
		{
			others = TRUE;
			
			const gchar *interface = other->interface;
			if(!interface)
				interface = "unknown";
			
			other->name = g_strdup_printf("%s (%s)", name, interface);
			if(other->ready)
				g_object_notify_by_pspec(G_OBJECT(other), deviceProperties[DV_PROP_NAME]);
		}
	}
	
	if(others)
		device->name = g_strdup_printf("%s (%s)", name, device->interface);
	else
		device->name = g_strdup(name);
	
	if(device->ready)
		g_object_notify_by_pspec(G_OBJECT(device), deviceProperties[DV_PROP_NAME]);
}


/*
 * Code flow for NetworkManager devices:
 *
 * 1. Get generic device properties at the
 *    org.freedesktop.NetworkManager.Device interface
 * 2. Load generic device properties in on_nm_device_get_properties.
 *    Determine the type of device, and get specific device
 *    properties at its specific interface.
 * 3. Load specific device properties again in
 *    on_nm_device_get_properties.
 * 4. Run type-specific access point object initialization.
 * 5. When each access point completes its initialization, it will
 *    set itself ready and ask the device to check if all other APs
 *    are ready. If so, the device becomes ready.
 *
 * Note that no signal emissions occur during this init phase,
 * only after once the PropertiesChanged dbus signals start coming in.
 */

static void csk_network_device_dispose(GObject *self_);
static void csk_network_device_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void csk_network_device_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void csk_network_device_remove_all_aps(CskNetworkDevice *self, gboolean emit);
static void on_nm_device_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self);
static void on_nm_device_get_wifi_aps(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self);
static void nm_device_add_wifi_ap(CskNetworkDevice *self, const gchar *apPath);
static void nm_device_remove_wifi_ap(CskNetworkDevice *self, const gchar *apPath);
static void csk_network_device_maybe_set_ready(CskNetworkDevice *self);
static void on_nm_device_signal(GDBusConnection *connection, const gchar *sender, const gchar *object, const gchar *interface, const gchar *signal, GVariant *parameters, CskNetworkDevice *self);

static void csk_network_device_class_init(CskNetworkDeviceClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_device_dispose;
	base->set_property = csk_network_device_set_property;
	base->get_property = csk_network_device_get_property;
	
	deviceProperties[DV_PROP_DEVICE_TYPE] =
		g_param_spec_uint("device-type",
		                  "Device Type",
		                  "Type of network device, CskNDeviceType",
		                  0, G_MAXINT, CSK_NDEVICE_TYPE_UNKNOWN,
		                  G_PARAM_READABLE);
	
	deviceProperties[DV_PROP_NAME] =
		g_param_spec_string("name",
		                    "Name",
		                    "Human-readable name of device",
		                    NULL,
		                    G_PARAM_READABLE);
	
	deviceProperties[DV_PROP_MAC] =
		g_param_spec_string("mac",
		                    "MAC",
		                    "Non-permanent MAC address of device",
		                    NULL,
		                    G_PARAM_READABLE);
	
	deviceProperties[DV_PROP_CONNECTION_STATUS] =
		g_param_spec_uint("connection-status",
		                  "Connection Status",
		                  "Status of device, CskNConnectionStatus",
		                  0, G_MAXINT, CSK_NETWORK_DISCONNECTED,
		                  G_PARAM_READABLE);
	
	g_object_class_install_properties(base, DV_PROP_LAST, deviceProperties);
	
	deviceSignals[DV_SIGNAL_AP_ADDED] =
		g_signal_new("ap-added",
		             G_TYPE_FROM_CLASS(class),
		             G_SIGNAL_RUN_FIRST,
		             0, NULL, NULL, NULL,
		             G_TYPE_NONE,
		             1, csk_network_access_point_get_type());
	
	deviceSignals[DV_SIGNAL_AP_REMOVED] =
		g_signal_new("ap-removed",
		             G_TYPE_FROM_CLASS(class),
		             G_SIGNAL_RUN_FIRST,
		             0, NULL, NULL, NULL,
		             G_TYPE_NONE,
		             1, csk_network_access_point_get_type());
}

static void csk_network_device_init(CskNetworkDevice *self)
{
	self->type = CSK_NDEVICE_TYPE_UNKNOWN;
	self->status = CSK_NETWORK_DISCONNECTED;
	self->ready = FALSE;
	
	// Init will be called again after the manager sets some variables
	if(!CSK_IS_NETWORK_MANAGER(self->manager))
		return;
	
	g_message("Device init %s", self->nmDevicePath);
	self->cancellable = g_cancellable_new();
	
	if(self->nmDevicePath)
	{
		g_dbus_connection_call(self->manager->connection,
			NM_DAEMON_NAME,
			self->nmDevicePath,
			"org.freedesktop.DBus.Properties",
			"GetAll",
			g_variant_new("(s)", "org.freedesktop.NetworkManager.Device"),
			G_VARIANT_TYPE("(a{sv})"),
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			self->cancellable,
			(GAsyncReadyCallback)on_nm_device_get_properties,
			self);
	}
}

static void csk_network_device_dispose(GObject *self_)
{
	CskNetworkDevice *self = CSK_NETWORK_DEVICE(self_);
	g_clear_pointer(&self->name, g_free);
	g_clear_pointer(&self->mac, g_free);
	csk_network_device_remove_all_aps(self, FALSE);
	csk_network_device_self_destruct(self);
	G_OBJECT_CLASS(csk_network_device_parent_class)->dispose(self_);
}

static void csk_network_device_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	switch(propertyId)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void csk_network_device_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	CskNetworkDevice *self = CSK_NETWORK_DEVICE(self_);
	switch(propertyId)
	{
	case DV_PROP_DEVICE_TYPE:
		g_value_set_uint(value, self->type);
		break;
	case DV_PROP_NAME:
		g_value_set_string(value, self->name);
		break;
	case DV_PROP_MAC:
		g_value_set_string(value, self->mac);
		break;
	case DV_PROP_CONNECTION_STATUS:
		g_value_set_uint(value, self->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void csk_network_device_self_destruct(CskNetworkDevice *self)
{
	g_clear_pointer(&self->nmDevicePath, g_free);
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	if(self->nmSignalSubId && self->manager)
		g_dbus_connection_signal_unsubscribe(self->manager->connection, self->nmSignalSubId);
	self->nmSignalSubId = 0;
	csk_network_device_remove_all_aps(self, TRUE);
	self->manager = NULL;
}

static void csk_network_device_remove_all_aps(CskNetworkDevice *self, gboolean emit)
{
	for(GList *it=self->aps; it!=NULL; it=it->next)
	{
		CskNetworkAccessPoint *ap = it->data;
		csk_network_access_point_self_destruct(ap);
		if(emit)
			g_signal_emit(self, deviceSignals[DV_SIGNAL_AP_REMOVED], 0, ap);
		g_object_unref(ap);
	}
	g_clear_pointer(&self->aps, g_list_free);
	g_clear_pointer(&self->readyAps, g_list_free);
}

static void on_nm_device_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self)
{
	GError *error = NULL;
	GVariant *propsVT = g_dbus_connection_call_finish(connection, res, &error);
	if(error)
	{
		g_warning("Failed to get NetworkManager Device properties: %s", error->message);
		g_error_free(error);
		return;
	}
	
	if(!self->manager)
	{
		g_variant_unref(propsVT);
		return;
	}
	
	// (a{sv}) -> a{sv}
	GVariant *propsV = g_variant_get_child_value(propsVT, 0);
	g_variant_unref(propsVT);
	
	GVariantDict dict;
	g_variant_dict_init(&dict, propsV);

	// Inital device property lookup
	if(self->type == CSK_NDEVICE_TYPE_UNKNOWN)
	{
		g_clear_pointer(&self->interface, g_free);
		g_variant_dict_lookup(&dict, "Interface", "s", &self->interface);
		
		// Get device type and run device-type-specific init
		guint32 deviceType;
		if(g_variant_dict_lookup(&dict, "DeviceType", "u", &deviceType))
			cnm_device_update_type(self, deviceType);
		else
			g_warning("Failed to get DeviceType from NetworkManager device: %s", self->nmDevicePath);
	}
	else if(self->type == CSK_NDEVICE_TYPE_WIRED)
	{
		g_clear_pointer(&self->mac, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->mac);
		
		gboolean carrier = FALSE;
		g_variant_dict_lookup(&dict, "Carrier", "b", &carrier);
		
		// Only create an AP if this device has the ability to connect
		// to anything. This will be updated on the device's PropertiesChanged
		if(carrier)
		{
			CskNetworkAccessPoint *ap = CSK_NETWORK_ACCESS_POINT(g_object_new(CSK_TYPE_NETWORK_ACCESS_POINT, NULL));
			ap->device = self;
			ap->name = g_strdup("ethernet");
			self->aps = g_list_prepend(self->aps, ap);
			csk_network_access_point_init(ap);
		}
		else
			csk_network_device_maybe_set_ready(self);			
	}
	else if(self->type == CSK_NDEVICE_TYPE_WIFI)
	{
		g_clear_pointer(&self->mac, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->mac);
		
		// List wifi access points
		g_dbus_connection_call(self->manager->connection,
			NM_DAEMON_NAME,
			self->nmDevicePath,
			"org.freedesktop.NetworkManager.Device.Wireless",
			"GetAllAccessPoints",
			NULL,
			G_VARIANT_TYPE("(ao)"),
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			self->cancellable,
			(GAsyncReadyCallback)on_nm_device_get_wifi_aps,
			self);
	}
	else if(self->type == CSK_NDEVICE_TYPE_BLUETOOTH)
	{
		g_clear_pointer(&self->mac, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->mac);
		
		gchar *name = NULL;
		g_variant_dict_lookup(&dict, "Name", "s", &name);
		
		// TODO
		// It seems like a Bluetooth device should be able to have
		// multiple "access points," but NM acts like its only one.
		// Are the different possible access points handled in the
		// bluetooth pairing setup? Does the bluetooth driver pick
		// which device to use for internet? If so, maybe add in
		// functionality to change the paired bluetooth device by
		// talking to bluez. For now though, only one AP is necessary.
		CskNetworkAccessPoint *ap = CSK_NETWORK_ACCESS_POINT(g_object_new(CSK_TYPE_NETWORK_ACCESS_POINT, NULL));
		ap->device = self;
		ap->name = name;
		self->aps = g_list_prepend(self->aps, ap);
		csk_network_access_point_init(ap);
	}
	
	g_variant_unref(propsV);
}

static void cnm_device_update_type(CskNetworkDevice *self, guint32 nmType)
{
	csk_network_device_remove_all_aps(self, TRUE);
	
	const gchar *interfaceName = NULL;
	
	CskNDeviceType prevType = self->type;
	
	if(nmType == 1) // NM_DEVICE_TYPE_ETHERNET
	{
		self->type = CSK_NDEVICE_TYPE_WIRED;
		interfaceName = "org.freedesktop.NetworkManager.Device.Wired";
	}
	else if(nmType == 2) // NM_DEVICE_TYPE_WIFI
	{
		self->type = CSK_NDEVICE_TYPE_WIFI;
		interfaceName = "org.freedesktop.NetworkManager.Device.Wireless";
	}
	else if(nmType == 5) // NM_DEVICE_TYPE_BT
	{
		self->type = CSK_NDEVICE_TYPE_BLUETOOTH;
		interfaceName = "org.freedesktop.NetworkManager.Device.Bluetooth";
	}
	else
	{
		self->type = CSK_NDEVICE_TYPE_UNKNOWN;
		g_message("Unsupported NetworkManager Device type: %i", (int)nmType);
		return;
	}

	if(self->type == prevType)
		return;
	
	set_device_name_and_update_others(self->manager, self);
	
	if(self->ready)
	{
		// I don't think this will ever actually happen, but
		// whatever, just in case
		g_object_notify_by_pspec(G_OBJECT(self), apProperties[DV_PROP_DEVICE_TYPE]);
	}
	
	// Send another property request for the device-type specific
	// properties. These go to the same generic device property handler.
	g_dbus_connection_call(self->manager->connection,
		NM_DAEMON_NAME,
		self->nmDevicePath,
		"org.freedesktop.DBus.Properties",
		"GetAll",
		g_variant_new("(s)", interfaceName ? interfaceName : ""),
		G_VARIANT_TYPE("(a{sv})"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		self->cancellable,
		(GAsyncReadyCallback)on_nm_device_get_properties,
		self);
	
	if(!self->nmSignalSubId)
		self->nmSignalSubId = g_dbus_connection_signal_subscribe(self->manager->connection,
			self->manager->nmDaemonOwner,
			NULL, // All interfaces
			NULL, // All signals
			self->nmDevicePath,
			NULL, // All arg0s
			G_DBUS_SIGNAL_FLAGS_NONE,
			(GDBusSignalCallback)on_nm_device_signal,
			self,
			NULL);
}

static void on_nm_device_get_wifi_aps(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self)
{
	GError *error = NULL;
	GVariant *apsV = g_dbus_connection_call_finish(connection, res, &error);
	if(error)
	{
		g_warning("Failed to list all NetworkManager AccessPoints: %s", error->message);
		g_error_free(error);
		return;
	}
	
	GVariantIter *iter = NULL;
	const gchar *apPath = NULL;
	g_variant_get(apsV, "(ao)", &iter);
	while(g_variant_iter_next(iter, "&o", &apPath))
		nm_device_add_wifi_ap(self, apPath);
	csk_network_device_maybe_set_ready(self);
}

static void nm_device_add_wifi_ap(CskNetworkDevice *self, const gchar *apPath)
{
	CskNetworkAccessPoint *ap = CSK_NETWORK_ACCESS_POINT(g_object_new(CSK_TYPE_NETWORK_ACCESS_POINT, NULL));
	ap->device = self;
	ap->nmApPath = g_strdup(apPath);
	self->aps = g_list_prepend(self->aps, ap);
	csk_network_access_point_init(ap);
}

static void nm_device_remove_wifi_ap(CskNetworkDevice *self, const gchar *apPath)
{
	for(GList *it=self->aps; it!=NULL; it=it->next)
	{
		CskNetworkAccessPoint *ap = it->data;
		if(g_strcmp0(apPath, ap->nmApPath) == 0)
		{
			self->aps = g_list_delete_link(self->aps, it);
			self->readyAps = g_list_remove(self->readyAps, ap);
			csk_network_access_point_self_destruct(ap);
			g_signal_emit(self, deviceSignals[DV_SIGNAL_AP_REMOVED], 0, ap);
			g_object_unref(ap);
			return;
		}
	}
}

static void csk_network_device_maybe_set_ready(CskNetworkDevice *self)
{
	if(self->ready || !self->manager)
		return;
	if(g_list_length(self->readyAps) >= g_list_length(self->aps))
	{
		self->ready = TRUE;
		self->manager->readyDevices = g_list_prepend(self->manager->readyDevices, self);
		g_signal_emit(self->manager, managerSignals[MN_SIGNAL_DEVICE_ADDED], 0, self);
		g_message("Device ready %s", self->nmDevicePath);
	}
}

// All interfaces on the device object
static void on_nm_device_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	CskNetworkDevice *self)
{
	if(!self->manager)
		return;
	if(g_strcmp0(sender, self->manager->nmDaemonOwner) != 0)
	{
		g_warning("Bad NM device signal: %s, %s, %s, %s, %s", self->manager->nmDaemonOwner, sender, object, interface, signal);
		return;
	}
	
	if(g_strcmp0(interface, "org.freedesktop.DBus.Properties") == 0)
	{
		if(g_strcmp0(signal, "PropertiesChanged") != 0)
			return;
		
		DO_ON_INVALID_FORMAT_STRING(parameters, "(sa{sv}as)", return, FALSE);
		
		const gchar *iface;
		GVariantIter *iter;
		g_variant_get(parameters, "(&sa{sv}as)", &iface, &iter, NULL);
		
		if(g_strcmp0(iface, "org.freedesktop.NetworkManager.Device.Wired") == 0)
		{
			const gchar *key;
			GVariant *val;
			while(g_variant_iter_next(iter, "{&sv}", &key, &val))
			{
				if(g_strcmp0(key, "HwAddress") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					g_free(self->mac);
					self->mac = g_variant_dup_string(val, NULL);
					g_object_notify_by_pspec(G_OBJECT(self), deviceProperties[DV_PROP_MAC]);
				}
				else if(g_strcmp0(key, "Carrier") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "b", continue, TRUE);
					gboolean carrier = g_variant_get_boolean(val);
					// Only have an access point when the carrier (an actual cable)
					// is attached
					if(!carrier)
						csk_network_device_remove_all_aps(self, TRUE);
					else
					{
						CskNetworkAccessPoint *ap = CSK_NETWORK_ACCESS_POINT(g_object_new(CSK_TYPE_NETWORK_ACCESS_POINT, NULL));
						ap->device = self;
						ap->name = g_strdup("ethernet");
						self->aps = g_list_prepend(self->aps, ap);
						csk_network_access_point_init(ap);
					}
				}
				g_variant_unref(val);
			}
		}
		else if(g_strcmp0(iface, "org.freedesktop.NetworkManager.Device.Wireless") == 0)
		{
			const gchar *key;
			GVariant *val;
			while(g_variant_iter_next(iter, "{&sv}", &key, &val))
			{
				if(g_strcmp0(key, "HwAddress") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					g_free(self->mac);
					self->mac = g_variant_dup_string(val, NULL);
					g_object_notify_by_pspec(G_OBJECT(self), deviceProperties[DV_PROP_MAC]);
				}
				g_variant_unref(val);
			}
		}
		else if(g_strcmp0(iface, "org.freedesktop.NetworkManager.Device.Bluetooth") == 0)
		{
			const gchar *key;
			GVariant *val;
			while(g_variant_iter_next(iter, "{&sv}", &key, &val))
			{
				if(g_strcmp0(key, "HwAddress") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					g_free(self->mac);
					self->mac = g_variant_dup_string(val, NULL);
					g_object_notify_by_pspec(G_OBJECT(self), deviceProperties[DV_PROP_MAC]);
				}
				else if(g_strcmp0(key, "Name") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					if(self->aps && self->aps->data)
					{
						CskNetworkAccessPoint *ap = self->aps->data;
						g_free(ap->name);
						ap->name = g_variant_dup_string(val, NULL);
						g_object_notify_by_pspec(G_OBJECT(self), apProperties[AP_PROP_NAME]);
					}
				}
				g_variant_unref(val);
			}
		}
		else if(g_strcmp0(iface, "org.freedesktop.NetworkManager.Device") == 0)
		{
			const gchar *key;
			GVariant *val;
			// I don't know if either of these will ever actually be changed, but
			// just in case.
			while(g_variant_iter_next(iter, "{&sv}", &key, &val))
			{
				if(g_strcmp0(key, "Interface") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					g_free(self->interface);
					self->interface = g_variant_dup_string(val, NULL);
					set_device_name_and_update_others(self->manager, self);
				}
				else if(g_strcmp0(key, "DeviceType") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "u", continue, TRUE);
					guint deviceType = g_variant_get_uint32(val);
					cnm_device_update_type(self, deviceType);
				}
				g_variant_unref(val);
			}
		}
		g_variant_iter_free(iter);
	}
	else if(g_strcmp0(interface, "org.freedesktop.NetworkManager.Device.Wireless") == 0)
	{
		if(g_strcmp0(signal, "AccessPointAdded") == 0)
		{
			DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return, FALSE);
			const gchar *path;
			g_variant_get(parameters, "(&o)", &path);
			nm_device_add_wifi_ap(self, path);
		}
		else if(g_strcmp0(signal, "AccessPointRemoved") == 0)
		{
			DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return, FALSE);
			const gchar *path;
			g_variant_get(parameters, "(&o)", &path);
			nm_device_remove_wifi_ap(self, path);
		}
	}
}

CskNDeviceType csk_network_device_get_device_type(CskNetworkDevice *self)
{
	return self->type;
}

const gchar * csk_network_device_get_name(CskNetworkDevice *self)
{
	return self->name;
}

const gchar * csk_network_device_get_mac(CskNetworkDevice *self)
{
	return self->mac;
}

GArray * csk_network_device_get_ips(CskNetworkDevice *self)
{
	return NULL;
}

void csk_network_device_scan(CskNetworkDevice *self)
{
	// TODO
}

const GList * csk_network_device_get_access_points(CskNetworkDevice *self)
{
	return self->readyAps;
}



/*
 * Access point
 *
 * Represents a place to connect to a network. These are mostly only
 * useful for Wi-Fi connections, but just making every type of device
 * work through the concept of an access point makes API marginally
 * less confusing.
 */

static void csk_network_access_point_dispose(GObject *self_);
static void csk_network_access_point_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void csk_network_access_point_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void on_nm_wifi_ap_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkAccessPoint *self);
static void csk_network_access_point_set_ready(CskNetworkAccessPoint *self);
static void csk_network_access_point_update_best(CskNetworkAccessPoint *self);
static void csk_network_access_point_update_icon(CskNetworkAccessPoint *self);
static void on_nm_ap_signal(GDBusConnection *connection, const gchar *sender, const gchar *object, const gchar *interface, const gchar *signal, GVariant *parameters, CskNetworkAccessPoint *self);

static void csk_network_access_point_class_init(CskNetworkAccessPointClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_access_point_dispose;
	base->get_property = csk_network_access_point_get_property;
	
	apProperties[AP_PROP_NAME] =
		g_param_spec_string("name",
		                    "Name",
		                    "Name of the access point (ssid for Wi-Fi)",
		                    NULL,
		                    G_PARAM_READABLE);
	
	apProperties[AP_PROP_MAC] =
		g_param_spec_string("mac",
		                    "MAC",
		                    "Remote MAC of the access point (for Wi-Fi)",
		                    NULL,
		                    G_PARAM_READABLE);
	
	apProperties[AP_PROP_STRENGTH] =
		g_param_spec_uint("strength",
		                  "Strength",
		                  "Signal strength in range [0,100]",
		                  0, 100, 0,
		                  G_PARAM_READABLE);
	
	apProperties[AP_PROP_SECURITY] =
		g_param_spec_uint("security",
		                  "Security",
		                  "Security in use by the access point (Wi-Fi only)",
		                  0, G_MAXINT, CSK_NSECURITY_NONE,
		                  G_PARAM_READABLE);
	
	apProperties[AP_PROP_CONNECTION_STATUS] =
		g_param_spec_uint("connection-status",
		                  "Connection Status",
		                  "Connection status of the access point",
		                  0, G_MAXINT, CSK_NETWORK_DISCONNECTED,
		                  G_PARAM_READABLE);
	
	apProperties[AP_PROP_ICON] =
		g_param_spec_string("icon",
		                    "Icon",
		                    "Icon name to represent the access point",
		                    NULL,
		                    G_PARAM_READABLE);
	
	apProperties[AP_PROP_BEST] =
		g_param_spec_boolean("best",
		                     "Best",
		                     "If this access point is the best out of same-named aps",
		                     FALSE,
		                     G_PARAM_READABLE);
	
	g_object_class_install_properties(base, AP_PROP_LAST, apProperties);
}

static void csk_network_access_point_init(CskNetworkAccessPoint *self)
{
	if(!CSK_IS_NETWORK_DEVICE(self->device) || !CSK_IS_NETWORK_MANAGER(self->device->manager))
		return;
	
	self->cancellable = g_cancellable_new();
	
	if(self->device->type == CSK_NDEVICE_TYPE_WIFI)
	{
		if(self->nmApPath)
		{
			g_dbus_connection_call(self->device->manager->connection,
				NM_DAEMON_NAME,
				self->nmApPath,
				"org.freedesktop.DBus.Properties",
				"GetAll",
				g_variant_new("(s)", "org.freedesktop.NetworkManager.AccessPoint"),
				G_VARIANT_TYPE("(a{sv})"),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				self->cancellable,
				(GAsyncReadyCallback)on_nm_wifi_ap_get_properties,
				self);
			
			self->nmSignalSubId = g_dbus_connection_signal_subscribe(self->device->manager->connection,
				self->device->manager->nmDaemonOwner,
				"org.freedesktop.DBus.Properties",
				NULL, // All signals
				self->nmApPath,
				NULL, // All arg0s
				G_DBUS_SIGNAL_FLAGS_NONE,
				(GDBusSignalCallback)on_nm_ap_signal,
				self,
				NULL);
		}
	}
	else
	{
		csk_network_access_point_set_ready(self);
	}
}

static void csk_network_access_point_dispose(GObject *self_)
{
	CskNetworkAccessPoint *self = CSK_NETWORK_ACCESS_POINT(self_);
	g_message("ap dispose: %s", self->name);
	csk_network_access_point_self_destruct(self);
	g_clear_pointer(&self->name, g_free);
	G_OBJECT_CLASS(csk_network_access_point_parent_class)->dispose(self_);
}

static void csk_network_access_point_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	CskNetworkAccessPoint *self = CSK_NETWORK_ACCESS_POINT(self_);
	switch(propertyId)
	{
	case AP_PROP_NAME:
		g_value_set_string(value, self->name);
		break;
	case AP_PROP_MAC:
		g_value_set_string(value, self->remoteMac);
		break;
	case AP_PROP_STRENGTH:
		g_value_set_uint(value, self->strength);
		break;
	case AP_PROP_SECURITY:
		g_value_set_uint(value, self->security);
		break;
	case AP_PROP_CONNECTION_STATUS:
		g_value_set_uint(value, self->status);
		break;
	case AP_PROP_ICON:
		g_value_set_string(value, self->icon);
		break;
	case AP_PROP_BEST:
		g_value_set_boolean(value, self->best);
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self_, propertyId, pspec);
		break;
	}
}

static void csk_network_access_point_self_destruct(CskNetworkAccessPoint *self)
{
	g_message("Ap self destruct: %s, %s", self->name, self->nmApPath);
	self->strength = 0;
	g_clear_pointer(&self->remoteMac, g_free);
	g_clear_pointer(&self->nmApPath, g_free);
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	if(self->nmSignalSubId && self->device->manager)
		g_dbus_connection_signal_unsubscribe(self->device->manager->connection, self->nmSignalSubId);
	self->nmSignalSubId = 0;
	if(self->name && self->device)
		csk_network_access_point_update_best(self);
	self->device = NULL;
}

// The ssid property does not contain a nul terminator for whatever
// reason, so g_variant_dup_bytestring doesn't work.
static gchar * string_from_ay_iter(GVariantIter *iter)
{
	gchar *str = NULL;
	guint n = g_variant_iter_n_children(iter);
	if(n > 0)
	{
		str = g_new0(gchar, n + 1);
		
		guchar y;
		guint i = 0;
		while(g_variant_iter_next(iter, "y", &y) && i < n)
			str[i++] = (gchar)y;
	}
	return str;
}

static void on_nm_wifi_ap_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkAccessPoint *self)
{
	GError *error = NULL;
	GVariant *propsVT = g_dbus_connection_call_finish(connection, res, &error);
	if(error)
	{
		g_warning("Failed to get NetworkManager AccessPoint properties: %s", error->message);
		g_error_free(error);
		return;
	}
	
	// (a{sv}) -> a{sv}
	GVariant *propsV = g_variant_get_child_value(propsVT, 0);
	g_variant_unref(propsVT);
	
	GVariantDict dict;
	g_variant_dict_init(&dict, propsV);
	
	g_clear_pointer(&self->name, g_free);
	GVariantIter *iter;
	if(g_variant_dict_lookup(&dict, "Ssid", "ay", &iter))
	{
		self->name = string_from_ay_iter(iter);
		g_variant_iter_free(iter);
	}
	
	self->strength = 0;
	guchar strength;
	if(g_variant_dict_lookup(&dict, "Strength", "y", &strength))
		self->strength = strength;
	
	g_clear_pointer(&self->remoteMac, g_free);
	g_variant_dict_lookup(&dict, "HwAddress", "s", &self->remoteMac);
	
	// Once the AP's properties have been determined, it is ready
	csk_network_access_point_set_ready(self);
	g_variant_unref(propsV);
}

static void csk_network_access_point_set_ready(CskNetworkAccessPoint *self)
{
	g_message("Ap ready: %p, %s, %s, %s, %i, %i", self, self->nmApPath, self->name, self->remoteMac, self->strength, self->best);
	if(!self->device)
		return;
	self->device->readyAps = g_list_prepend(self->device->readyAps, self);
	csk_network_access_point_update_best(self);
	csk_network_access_point_update_icon(self);
	if(self->device->ready)
		g_signal_emit(self->device, deviceSignals[DV_SIGNAL_AP_ADDED], 0, self);
	else
		csk_network_device_maybe_set_ready(self->device);
}

// Finds the "best" access point (based on signal strength alone) out of
// all access points that match the name and security type of self.
// Updates this value on all the other access points too, and automatically
// emits the notify signals.
// This is how GUI lists know which access point to show.
static void csk_network_access_point_update_best(CskNetworkAccessPoint *self)
{
	if(!self->device)
		return;
	g_message("update best");
	guint strength = self->strength;
	CskNetworkAccessPoint *prevBest = self->best ? self : NULL;
	CskNetworkAccessPoint *best = self;
	
	if(self->name) // APs with no set name should not be grouped together
	{
		for(GList *it=self->device->readyAps; it!=NULL; it=it->next)
		{
			CskNetworkAccessPoint *ap = it->data;
			if(ap == self)
				continue;
			
			// Only check APs that are the "same"
			if(ap->security == self->security
			&& g_strcmp0(ap->name, self->name) == 0)
			{
				if(ap->best)
					prevBest = ap;
				
				if(ap->strength > strength)
				{
					strength = ap->strength;
					best = ap;
				}
			}
		}
	}
	
	if(!prevBest)
	{
		best->best = TRUE;
		if(self->device->ready)
			g_object_notify_by_pspec(G_OBJECT(best), apProperties[AP_PROP_BEST]);
	}
	else if(best != prevBest)
	{
		prevBest->best = FALSE;
		best->best = TRUE;
		if(self->device->ready)
		{
			g_object_notify_by_pspec(G_OBJECT(prevBest), apProperties[AP_PROP_BEST]);
			g_object_notify_by_pspec(G_OBJECT(best), apProperties[AP_PROP_BEST]);
		}
	}
	// else, don't do anything -- best didn't change
}

static void csk_network_access_point_update_icon(CskNetworkAccessPoint *self)
{
	if(!self->device)
		return;
	
	if(self->device->type == CSK_NDEVICE_TYPE_WIRED)
	{
		g_free(self->icon);
		self->icon = g_strdup("network-wired-symbolic");
	}
	else if(self->device->type == CSK_NDEVICE_TYPE_BLUETOOTH)
	{
		g_free(self->icon);
		self->icon = g_strdup("bluetooth-symbolic");
	}
	else if(self->device->type == CSK_NDEVICE_TYPE_WIFI)
	{
		const gchar *name = "none";
		if(self->strength > 80)
			name = "excellent";
		else if(self->strength > 60)
			name = "good";
		else if(self->strength > 40)
			name = "ok";
		else if(self->strength > 20)
			name = "weak";
		
		g_free(self->icon);
		self->icon = g_strdup_printf("network-wireless-signal-%s-symbolic", name);
		g_object_notify_by_pspec(G_OBJECT(self), apProperties[AP_PROP_ICON]);
	}
}

// Only for org.freedesktop.DBus.Properties interface
static void on_nm_ap_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	CskNetworkAccessPoint *self)
{
	if(!self->device || !self->device->manager)
		return;
	if(g_strcmp0(sender, self->device->manager->nmDaemonOwner) != 0)
	{
		g_warning("Bad NM ap signal: %s, %s, %s, %s, %s", self->device->manager->nmDaemonOwner, sender, object, interface, signal);
		return;
	}
	
	if(g_strcmp0(signal, "PropertiesChanged") == 0)
	{
		DO_ON_INVALID_FORMAT_STRING(parameters, "(sa{sv}as)", return, FALSE);
		
		const gchar *iface;
		GVariantIter *iter;
		//g_message("otype: %s", g_variant_get_type_string(parameters));
		g_variant_get(parameters, "(&sa{sv}as)", &iface, &iter, NULL);
		
		if(g_strcmp0(iface, "org.freedesktop.NetworkManager.AccessPoint") == 0)
		{
			// TODO: For some reason, using get_child_value to extract
			// the tuple wasn't working right. GVariants are weird...
			// So until I can figure that out, using an iterator
			// instead of a dictionary.
			const gchar *key;
			GVariant *val;
			while(g_variant_iter_next(iter, "{&sv}", &key, &val))
			{
				if(g_strcmp0(key, "Strength") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "y", continue, TRUE);
					self->strength = g_variant_get_byte(val);
					g_object_notify_by_pspec(G_OBJECT(self), apProperties[AP_PROP_STRENGTH]);
					csk_network_access_point_update_best(self);
					csk_network_access_point_update_icon(self);
				}
				else if(g_strcmp0(key, "Ssid") == 0)
				{
					DO_ON_INVALID_FORMAT_STRING(val, "ay", continue, TRUE);
					GVariantIter iter;
					g_variant_iter_init(&iter, val);
					g_clear_pointer(&self->name, g_free);
					self->name = string_from_ay_iter(&iter);
				}
				else if(g_strcmp0(key, "HwAddress") == 0)
				{
					// Not sure if this can actually happen; NM probably
					// just creates a new AccessPoint object. But just in case
					DO_ON_INVALID_FORMAT_STRING(val, "s", continue, TRUE);
					g_clear_pointer(&self->remoteMac, g_free);
					self->remoteMac = g_variant_dup_string(val, NULL);
					g_object_notify_by_pspec(G_OBJECT(self), apProperties[AP_PROP_MAC]);
				}
				g_variant_unref(val);
			}
		}
		
		g_variant_iter_free(iter);
	}
}

CskNetworkDevice * csk_network_access_point_get_device(CskNetworkAccessPoint *self)
{
	return self->device;
}

CskNConnectionStatus csk_network_access_point_get_connection_status(CskNetworkAccessPoint *self)
{
	return self->status;
}

const gchar * csk_network_access_point_get_name(CskNetworkAccessPoint *self)
{
	return self->name;
}

const gchar * csk_network_access_point_get_mac(CskNetworkAccessPoint *self)
{
	return self->remoteMac;
}

guint csk_network_access_point_get_strength(CskNetworkAccessPoint *self)
{
	return self->strength;
}

gboolean csk_network_access_point_is_best(CskNetworkAccessPoint *self)
{
	return self->best;
}

CskNSecurityType csk_network_access_point_get_security(CskNetworkAccessPoint *self)
{
	return self->security;
}

const gchar * csk_network_access_point_get_icon(CskNetworkAccessPoint *self)
{
	return self->icon;
}

void csk_network_access_point_connect(CskNetworkAccessPoint *self)
{
	// TODO
}
