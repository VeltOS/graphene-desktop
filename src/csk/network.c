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

#define DO_ON_INVALID_FORMAT_STRING(v, format, d) {if(!g_variant_check_format_string(v, format, FALSE)) { g_warning("Invalid variant type string %s at " G_STRLOC " (should be %s)", format, g_variant_get_type_string(v)); d; }}

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
	
	gchar *nmDevicePath;
};

struct _CskNetworkAccessPoint
{
	GObject parent;
	
	CskNetworkDevice *device;
	GCancellable *cancellable;
	
	gchar *icon;
	gchar *name;
	gchar *remoteMac;
	gfloat strength;
	
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
	AP_PROP_STRENGTH,
	AP_PROP_SECURITY,
	AP_PROP_CONNECTION_STATUS,
	AP_PROP_ICON,
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
	if(!CSK_IS_NETWORK_MANAGER(self))
		return (self = csk_network_manager_new());
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

static void on_nm_daemon_signal(GDBusConnection *connection,
	const gchar *sender,
	const gchar *object,
	const gchar *interface,
	const gchar *signal,
	GVariant    *parameters,
	CskNetworkManager *self)
{
	if(g_strcmp0(sender, self->nmDaemonOwner) != 0
	|| g_strcmp0(object, NM_DAEMON_PATH) != 0 
	|| g_strcmp0(interface, NM_DAEMON_INTERFACE) != 0)
	{
		g_warning("Bad NM signal: %s, %s, %s, %s", sender, object, interface, signal);
		return;
	}
	
	if(g_strcmp0(signal, "DeviceAdded") == 0)
	{
		DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return);
		const gchar *path;
		g_variant_get(parameters, "(&o)", &path);
		add_nm_device(self, path);
	}
	else if(g_strcmp0(signal, "DeviceRemoved") == 0)
	{
		DO_ON_INVALID_FORMAT_STRING(parameters, "(&o)", return);
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
			{
				// TODO: Emit name changed signal
			}
		}
	}
	
	if(others)
		device->name = g_strdup_printf("%s (%s)", name, device->interface);
	else
		device->name = g_strdup(name);
	
	if(device->ready)
	{
		// TODO: Emit name changed signal
	}
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
static void csk_network_device_remove_all_aps(CskNetworkDevice *self, gboolean emit);
static void on_nm_device_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self);
static void on_nm_device_get_wifi_aps(GDBusConnection *connection, GAsyncResult *res, CskNetworkDevice *self);
static void csk_network_device_maybe_set_ready(CskNetworkDevice *self);

static void csk_network_device_class_init(CskNetworkDeviceClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_device_dispose;
	
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

static void csk_network_device_self_destruct(CskNetworkDevice *self)
{
	self->manager = NULL;
	g_clear_pointer(&self->nmDevicePath, g_free);
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
	csk_network_device_remove_all_aps(self, TRUE);
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
	
	// (a{sv}) -> a{sv}
	GVariant *propsV = g_variant_get_child_value(propsVT, 0);
	
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
		g_clear_pointer(&self->interface, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->interface);
		
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
	}
	else if(self->type == CSK_NDEVICE_TYPE_WIFI)
	{
		g_clear_pointer(&self->interface, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->interface);
		
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
		g_clear_pointer(&self->interface, g_free);
		g_variant_dict_lookup(&dict, "HwAddress", "s", &self->interface);
		
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
		// TODO: Emit type changed. I don't think this will ever actually
		// happen, but whatever, just in case
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
	
	// TODO: Update signals
	//self->nmSignalSubId = g_dbus_connection_signal_subscribe(connection,
	//	owner,
	//	NM_DAEMON_INTERFACE,
	//	NULL, // All signals
	//	NM_DAEMON_PATH,
	//	NULL, // All arg0s
	//	G_DBUS_SIGNAL_FLAGS_NONE,
	//	(GDBusSignalCallback)on_nm_daemon_signal,
	//	self,
	//	NULL);
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
	{
		CskNetworkAccessPoint *ap = CSK_NETWORK_ACCESS_POINT(g_object_new(CSK_TYPE_NETWORK_ACCESS_POINT, NULL));
		ap->device = self;
		ap->nmApPath = g_strdup(apPath);
		self->aps = g_list_prepend(self->aps, ap);
		csk_network_access_point_init(ap);
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



static void csk_network_access_point_dispose(GObject *self_);
static void on_nm_wifi_ap_get_properties(GDBusConnection *connection, GAsyncResult *res, CskNetworkAccessPoint *self);
static void csk_network_access_point_set_ready(CskNetworkAccessPoint *self);

static void csk_network_access_point_class_init(CskNetworkAccessPointClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_network_access_point_dispose;
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
	g_clear_pointer(&self->name, g_free);
	csk_network_access_point_self_destruct(self);
	G_OBJECT_CLASS(csk_network_access_point_parent_class)->dispose(self_);
}

static void csk_network_access_point_self_destruct(CskNetworkAccessPoint *self)
{
	g_message("Ap self destruct: %s, %s", self->name, self->nmApPath);
	self->device = NULL;
	self->strength = 0;
	g_clear_pointer(&self->remoteMac, g_free);
	g_clear_pointer(&self->nmApPath, g_free);
	g_cancellable_cancel(self->cancellable);
	g_clear_object(&self->cancellable);
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
	
	GVariantDict dict;
	g_variant_dict_init(&dict, propsV);
	
	g_clear_pointer(&self->name, g_free);
	GVariantIter *iter;
	if(g_variant_dict_lookup(&dict, "Ssid", "ay", &iter))
	{
		// The ssid property does not contain a nul terminator for whatever
		// reason, so g_variant_dup_bytestring doesn't work.
		guint n = g_variant_iter_n_children(iter);
		if(n > 0)
		{
			self->name = g_new0(gchar, n + 1);
			
			guchar y;
			guint i = 0;
			while(g_variant_iter_next(iter, "y", &y) && i < n)
				self->name[i++] = (gchar)y;
		}
		g_variant_iter_free(iter);
	}
	
	self->strength = 0;
	guchar strength;
	if(g_variant_dict_lookup(&dict, "Strength", "y", &strength))
		self->strength = strength/255.0;
	
	g_clear_pointer(&self->remoteMac, g_free);
	g_variant_dict_lookup(&dict, "HwAddress", "s", &self->remoteMac);
	
	// Once the AP's properties have been determined, it is ready
	csk_network_access_point_set_ready(self);
}

static void csk_network_access_point_set_ready(CskNetworkAccessPoint *self)
{
	g_message("Ap ready: %s, %s, %s, %f", self->nmApPath, self->name, self->remoteMac, self->strength);
	if(!self->device)
		return;
	self->device->readyAps = g_list_prepend(self->device->readyAps, self);
	if(self->device->ready)
		g_signal_emit(self->device, deviceSignals[DV_SIGNAL_AP_ADDED], 0, self);
	else
		csk_network_device_maybe_set_ready(self->device);
}
