/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * network.h/.c
 */

#include "network.h"
#include <gio/gio.h>

struct _GrapheneNetworkControl
{
  GObject parent;
  
  GDBusProxy *wicdDaemonProxy;
  
  guint32 status; // 0: Not Connected, 1: Connecting Wired, 2: Connecting Wireless, 3: Wired, 4: Wireless, 5: Suspended
  gchar *ip; // When connected only, NULL otherwise
  gint signalStrength; // 0-100 when connected on wireless, 100 on wired, 0 otherwise
  gchar *essid; // When connected on wireless only, NULL otherwise  
  gchar *iconName;
};

enum
{
  SIGNAL_0,
  SIGNAL_UPDATE,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static void graphene_network_control_dispose(GObject *self_);
static void on_wicd_proxy_signal(GrapheneNetworkControl *self, const gchar *sender, const gchar *signal, GVariant *parameters, GDBusProxy *proxy);
static void update_status(GrapheneNetworkControl *self, guint32 status, const gchar **info, gsize infoSize);


G_DEFINE_TYPE(GrapheneNetworkControl, graphene_network_control, G_TYPE_OBJECT)


GrapheneNetworkControl * graphene_network_control_new(void)
{
  return GRAPHENE_NETWORK_CONTROL(g_object_new(GRAPHENE_TYPE_NETWORK_CONTROL, NULL));
}

GrapheneNetworkControl * graphene_network_control_get_default(void)
{
  static GrapheneNetworkControl *netctrl = NULL;
  if(!GRAPHENE_IS_NETWORK_CONTROL(netctrl))
  {
    netctrl = graphene_network_control_new();
    return netctrl;
  }
  return g_object_ref(netctrl);
}

static void graphene_network_control_class_init(GrapheneNetworkControlClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->dispose = graphene_network_control_dispose;
  
  /*
   * Emitted when the network status changes
   */ 
  signals[SIGNAL_UPDATE] = g_signal_new("update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void graphene_network_control_init(GrapheneNetworkControl *self)
{
  self->iconName = g_strdup_printf("network-offline-symbolic");
  GError *error = NULL;
  self->wicdDaemonProxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, 0, NULL,
    "org.wicd.daemon",
    "/org/wicd/daemon",
    "org.wicd.daemon",
    NULL, &error);
  
  if(!self->wicdDaemonProxy || error)
  {
    g_warning("Failed to connect to wicd: %s", error ? error->message : "");
    g_clear_error(&error);
    return;
  }
  
  g_signal_connect_swapped(self->wicdDaemonProxy, "g-signal", G_CALLBACK(on_wicd_proxy_signal), self);
  
  // GetConnectionStatus returns ((uas))
  GVariant *statusVariantWrap = g_dbus_proxy_call_sync(self->wicdDaemonProxy, "GetConnectionStatus", NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);

  if(!statusVariantWrap || !g_variant_check_format_string(statusVariantWrap, "((uas))", FALSE))
  {
    g_warning("Failed to get wicd connection status.");
    return;
  }

  // Drops the outer tuple, leaving (uas)
  GVariant *statusVariant = g_variant_get_child_value(statusVariantWrap, 0);
  g_variant_unref(statusVariantWrap);

  if(!statusVariant)
    return;

  // Gets the status int
  guint32 status;
  g_variant_get_child(statusVariant, 0, "u", &status);
  
  // Get the info string array
  GVariant *infoVariant = g_variant_get_child_value(statusVariant, 1);
  gsize infoSize;
  const gchar **info = g_variant_get_strv(infoVariant, &infoSize);
  g_variant_unref(infoVariant);
  
  update_status(self, status, info, infoSize);
  g_free(info);
}

static void graphene_network_control_dispose(GObject *self_)
{
  GrapheneNetworkControl *self = GRAPHENE_NETWORK_CONTROL(self_);
  g_clear_object(&self->wicdDaemonProxy);
  g_clear_pointer(&self->essid, g_free);
  g_clear_pointer(&self->ip, g_free);
  g_clear_pointer(&self->iconName, g_free);
  G_OBJECT_CLASS(graphene_network_control_parent_class)->dispose(self_);
}

guint32 graphene_network_control_get_status(GrapheneNetworkControl *self)
{
  return self->status;
}
const gchar * graphene_network_control_get_ip(GrapheneNetworkControl *self)
{
  return self->ip;
}
gint graphene_network_control_get_signal_strength(GrapheneNetworkControl *self)
{
  return self->signalStrength;
}
const gchar * graphene_network_control_get_essid(GrapheneNetworkControl *self)
{
  return self->essid;
}
const gchar * graphene_network_control_get_icon_name(GrapheneNetworkControl *self)
{
  return self->iconName;
}

static void on_wicd_proxy_signal(GrapheneNetworkControl *self, const gchar *sender, const gchar *signal, GVariant *parameters, GDBusProxy *proxy)
{
  if(g_strcmp0(signal, "StatusChanged") == 0) // parameters are (uav)
  {
    // Get status and av iter
    guint32 status;
    GVariantIter *iter;
    g_variant_get(parameters, "(uav)", &status, &iter);
    
    // Not sure why it's av, since the v is always a string based on the wicd source code (same as GetConnectionStatus)
    // So this gets a string array out of the av iter
    gsize infoSize = g_variant_iter_n_children(iter);
    const gchar **info = g_new0(const gchar *, infoSize);
    GVariant *v;
    gsize i=0;
    while(g_variant_iter_next(iter, "v", &v))
      info[i++] = g_variant_get_string(v, NULL); // transfer none
    g_variant_iter_free(iter);

    update_status(self, status, info, infoSize);
    g_free(info);
  }
}

static void update_status(GrapheneNetworkControl *self, guint32 status, const gchar **info, gsize infoSize)
{
  self->status = 0;
  self->signalStrength = 0;
  g_clear_pointer(&self->essid, g_free);
  g_clear_pointer(&self->ip, g_free);
  g_clear_pointer(&self->iconName, g_free);

  switch(status)
  {
    case 0: // Not connected
      self->iconName = g_strdup_printf("network-offline-symbolic");
      break;
      
    case 1: // Connecting
      self->status = (infoSize >= 1 && g_strcmp0(info[0], "wireless") == 0) ? 2 : 1;
      self->essid = (infoSize >= 2) ? g_strdup(info[1]) : NULL;
      self->iconName = g_strdup_printf("network-%s-acquiring-symbolic", self->status == 1 ? "wired" : "wireless");
      break;
      
    case 3: // Wired
      self->status = 3;
      self->ip = (infoSize >= 1) ? g_strdup(info[0]) : NULL;
      self->signalStrength = 100;
      self->iconName = g_strdup_printf("network-wired-symbolic");
      break;
      
    case 2: // Wireless
    {
      self->status = 4;
      self->ip = (infoSize >= 1) ? g_strdup(info[0]) : NULL;
      self->essid = (infoSize >= 2) ? g_strdup(info[1]) : NULL;
      self->signalStrength = (infoSize >= 3) ? (gint)g_ascii_strtoll(info[2], NULL, 10) : 0;
      const gchar *strengthStr = "none";
      if(self->signalStrength > 75) strengthStr = "excellent";
      else if(self->signalStrength > 50) strengthStr = "good";
      else if(self->signalStrength > 25) strengthStr = "ok";
      else if(self->signalStrength > 0) strengthStr = "weak";
      self->iconName = g_strdup_printf("network-wireless-signal-%s-symbolic", strengthStr);
      break;
    }
    
    case 4:
      self->status = 5;
      self->iconName = g_strdup_printf("network-no-route-symbolic");
      break;
  }
  
  g_signal_emit_by_name(self, "update");
}
