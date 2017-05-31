/*
 * This file is part of csk-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * Requires a network management daemon. Currently, CskNetwork supports
 * only NetworkManager. WICD support coming.
 */

#ifndef __CSK_NETWORK_H__
#define __CSK_NETWORK_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define CSK_TYPE_NETWORK_MANAGER  csk_network_manager_get_type()
G_DECLARE_FINAL_TYPE(CskNetworkManager, csk_network_manager, CSK, NETWORK_MANAGER, GObject)

#define CSK_TYPE_NETWORK_DEVICE  csk_network_device_get_type()
G_DECLARE_FINAL_TYPE(CskNetworkDevice, csk_network_device, CSK, NETWORK_DEVICE, GObject)

#define CSK_TYPE_NETWORK_ACCESS_POINT  csk_network_access_point_get_type()
G_DECLARE_FINAL_TYPE(CskNetworkAccessPoint, csk_network_access_point, CSK, NETWORK_ACCESS_POINT, GObject)

typedef enum
{
	CSK_NDEVICE_TYPE_UNKNOWN,
	CSK_NDEVICE_TYPE_WIRED,
	CSK_NDEVICE_TYPE_WIFI,
	CSK_NDEVICE_TYPE_BLUETOOTH,
} CskNDeviceType;

typedef enum
{
	CSK_NETWORK_DISCONNECTED,
	CSK_NETWORK_CONNECTING,
	CSK_NETWORK_CONNECTED,
} CskNConnectionStatus;

typedef enum
{
	CSK_NSECURITY_NONE,
	CSK_NSECURITY_OTHER,
	CSK_NSECURITY_WEP_KEY,
	CSK_NSECURITY_WEP_PASSPHRASE,
	CSK_NSECURITY_LEAP,
	CSK_NSECURITY_DYNAMIC_WEP,
	CSK_NSECURITY_WPA_WPA2_PSK,
	CSK_NSECURITY_WPA_WPA2_ENTERPRISE,
} CskNSecurityType;

/*
 * Gets the default NetworkManager object. Free with g_object_unref.
 * If the NetworkManager object is freed, all its devices and their
 * access points become inert.
 */
CskNetworkManager * csk_network_manager_get_default(void);

/*
 * Get a list of every available CskNetworkDevice. Listen to the
 * "device-added" and "device-removed" signals to check for device
 * changes. When a device is removed, it will become inert forever,
 * only useful for pointer comparisons.
 */
const GList * csk_network_manager_get_devices(CskNetworkManager *nm);

/*
 * Gets the active access point from the primary device access point,
 * or NULL if disconnected from the network.
 */
//CskNetworkAccessPoint * csk_network_manager_get_primary_access_point(CskNetworkManager *nm);

/*
 * The name of an icon to represent the overall connection status.
 * Same as the icon property on the primary access point.
 * Listen to the "notify::icon" signal for changes to this.
 */
//const gchar * csk_network_manager_get_icon(CskNetworkManager *nm);



/*
 * Gets the type of device.
 */
CskNDeviceType csk_network_device_get_device_type(CskNetworkDevice *device);

/*
 * Obtain a human-readable name of the device. (ex "Wi-Fi")
 * This name may change as other devices become available. For example,
 * one Wi-Fi device will be named "Wi-Fi", but if two Wi-Fi devices
 * are available, one will be "Wi-Fi (wlan0)" and the other "Wi-Fi (wlan1)".
 * Listen to the "notify::name" signal to see if this changes.
 */
const gchar * csk_network_device_get_name(CskNetworkDevice *device);

/*
 * Gets the MAC address of the device. This is different from the MAC
 * address of the network access point if the device is connected.
 */
const gchar * csk_network_device_get_mac(CskNetworkDevice *device);

/*
 * The device's connection status.
 */
CskNConnectionStatus csk_network_device_get_connection_status(CskNetworkDevice *device);

/*
 * An array of all IP addresses currently assigned to this device.
 * Just because a device has no assigned IP addresses does not mean
 * it is not connected to an access point.
 */
GArray * csk_network_device_get_ips(CskNetworkDevice *device);

/*
 * Force a scan of access points.
 */
void csk_network_device_scan(CskNetworkDevice *device);

/*
 * Gets all the access points available to this device. 
 *
 * Access points are used for all types of devices, not just Wi-Fi.
 * For Wired devices, only one access point object will exist, and
 * only if the ethernet wire is actually connected.
 * 
 * Devices do not share Access Point objects, even if they refer to
 * the same physical network (ex. in the case of two Wi-Fi devices).
 *
 * Listen to the "ap-added" and "ap-removed" signals to tell when
 * access points change. When an access point is removed, it will become
 * inert forever, only useful for pointer comparisons.
 */
const GList * csk_network_device_get_access_points(CskNetworkDevice *device);

/*
 * Active access point on this device, or NULL if disconnected.
 */
CskNetworkAccessPoint * csk_network_device_get_active_access_point(CskNetworkDevice *device);



/*
 * Gets the device that this access point has been found through, or
 * NULL if the device or this AP has been removed.
 */
CskNetworkDevice * csk_network_access_point_get_device(CskNetworkAccessPoint *ap);

/*
 * The access point's connection status.
 */
CskNConnectionStatus csk_network_access_point_get_connection_status(CskNetworkAccessPoint *ap);

/*
 * Gets a name for this access point. For Wi-Fi networks, it is the ssid.
 */
const gchar * csk_network_access_point_get_name(CskNetworkAccessPoint *ap);

/*
 * Gets the MAC address of the remote access point if connected, or NULL if
 * this access point is not connected or if the remote MAC is unavailable.
 */
const gchar * csk_network_access_point_get_mac(CskNetworkAccessPoint *ap);

/*
 * Gets the signal strength [0, 1] of the access point. If this concept
 * doesn't apply to the type of access point (eg Wired), it will be 1.
 */
guint csk_network_access_point_get_strength(CskNetworkAccessPoint *ap);

/*
 * Returns TRUE if this access point is the best out of other access
 * points of the same device with the same name (SSID) and security type.
 * FALSE otherwise. Connect to the "notify::best" signal.
 * This property can be used to determine which APs to show in a GUI
 * list, to avoid showing lots of networks that are all really the "same."
 */
gboolean csk_network_access_point_is_best(CskNetworkAccessPoint *ap);

/*
 * Returns TRUE if this AP is the active AP of its device.
 * Equivelent to
 * csk_network_device_get_active_access_point(csk_network_access_point_get_device(ap)) == ap
 */
gboolean csk_network_access_point_is_active(CskNetworkAccessPoint *ap);

/*
 * Returns TRUE if ap represents the same network as other. They must be of
 * the same device, have the same security type, and have the same name.
 * Always returns TRUE if ap is passed for other.
 */
gboolean csk_network_access_point_matches(CskNetworkAccessPoint *ap, CskNetworkAccessPoint *other);

/*
 * Gets the security type in use by this AP (Wi-Fi only).
 * TODO: Currently CskNetwork does not provide methods for connecting
 * to secured networks.
 */
CskNSecurityType csk_network_access_point_get_security(CskNetworkAccessPoint *ap);

/*
 * Gets an icon to represent the status of this AP.
 */
const gchar * csk_network_access_point_get_icon(CskNetworkAccessPoint *ap);

/*
 * Tries to connect to this AP. This may disconnect other access
 * points on the same device. Listen to the "connection-failed" signal
 * on the parent CskNetworkManager object to check for failure.
 */
void csk_network_access_point_connect(CskNetworkAccessPoint *ap);

G_END_DECLS

#endif /* __CSK_NETWORK_H__ */
