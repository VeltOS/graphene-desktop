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
	CSK_NDEVICE_TYPE_WIRED,
	CSK_NDEVICE_TYPE_WIFI,
	CSK_NETWORK_TYPE_BLUETOOTH,
} CskNDeviceType;

typedef enum
{
	CSK_NETWORK_DISCONENCTED,
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
 * changes. If a device is removed, it will emit the "removed" signal
 * and will become inert forever.
 */
const GList * csk_network_manager_get_devices(CskNetworkManager *ns);

/*
 * The name of an icon to represent the overall connection status.
 * Listen to the "notify::icon" signal for changes to this.
 */
const gchar * csk_network_manager_get_icon(CskNetworkManager *ns);

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
CskNConnectionStatus * csk_network_device_get_connection_status(CskNetworkDevice *device);

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
 * While normally "access point" refers to a physical Wi-Fi AP, in this
 * context it means any network this device can connect to. For a Wi-Fi
 * device, it is a list of Wi-Fi networks. For a Wired device, the list
 * will have one AP if a wire is connected; otherwise zero APs.
 *
 * Devices do not share Access Point objects, even if they refer to
 * the same physical network (ex. in the case of two Wi-Fi devices).
 *
 * Listen to the "ap-added" and "ap-removed" signals to tell when
 * access points change. If an access point is removed, it will emit
 * the "removed" signal and will become inert forever.
 */
const GList * csk_network_device_get_access_points(CskNetworkDevice *device);

/*
 * Gets the device that this access point has been found through, or
 * NULL if the device has been removed.
 */
CskNetworkDevice * csk_network_access_point_get_device(CskNetworkAccessPoint *ap);

/*
 * Gets a name for this access point. For Wi-Fi networks, it is the ssid.
 */
const gchar * csk_network_access_point_get_name(CskNetworkAccessPoint *ap);

/*
 * Gets the signal strength [0, 1] of the access point. If this concept
 * doesn't apply to the type of access point (eg Wired), it will be 1.
 */
gfloat csk_network_access_point_get_strength(CskNetworkAccessPoint *ap);

/*
 * Gets the security type in use by this AP (Wi-Fi only).
 * TODO: Currently CskNetwork does not provide methods for connecting
 * to secured networks.
 */
CskNSecurityType csk_network_access_point_get_security(CskNetworkAccessPoint *ap);

/*
 * The access point's connection status.
 */
CskNConnectionStatus * csk_network_device_get_connection_status(CskNetworkDevice *device);

/*
 * Tries to connect to this AP. This may disconnect other access
 * points on the same device. Listen to the "connection-failed" signal
 * on the parent CskNetworkManager object to check for failure.
 */
void csk_network_access_point_connect(CskNetworkAccessPoint *ap);

G_END_DECLS

#endif /* __CSK_NETWORK_H__ */
