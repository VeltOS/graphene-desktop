/*
 * This file is part of csk-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CSK_NETWORK_H__
#define __CSK_NETWORK_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CSK_TYPE_NETWORK_SETTINGS  csk_network_settings_get_type()
G_DECLARE_FINAL_TYPE(CskNetworkSettings, csk_network_settings, CSK, NETWORK_SETTINGS, GObject)

typedef enum
{
	CSK_NETWORK_TYPE_WIRED,
	CSK_NETWORK_TYPE_WIRELESS,
} CskNetworkType;

enum
{
	CSK_NETWORK_FLAG_SET_NAME = 1,
	CSK_NETWORK_FLAG_SET_MAC = 2,
	CSK_NETWORK_FLAG_SET_IP = 4,
	CSK_NETWORK_FLAG_SET_STRENGTH = 8,
};

struct _CskNetworkInfo
{
	gint id;
	CskNetworkType type;
	gchar *name;
	gchar *mac;
	gchar *ip;
	gfloat signalStrength;
	gchar *iconName;
	
	CskNetworkSettings *settings;
	guint flags;
};

typedef struct _CskNetworkInfo CskNetworkInfo;

CskNetworkSettings * csk_network_settings_get_default(void); // Free with g_object_unref
const CskNetworkInfo * csk_network_settings_get_current_network(CskNetworkSettings *ns);
const GList * csk_network_settings_get_networks(CskNetworkSettings *ns);

G_END_DECLS

#endif /* __CSK_NETWORK_H__ */
