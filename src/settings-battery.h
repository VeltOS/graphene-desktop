/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_BATTERY_H__
#define __GRAPHENE_BATTERY_H__

#include <glib.h>
#include <glib-object.h>
#include <libcmk/cmk-icon.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_BATTERY_INFO  graphene_battery_info_get_type()
G_DECLARE_FINAL_TYPE(GrapheneBatteryInfo, graphene_battery_info, GRAPHENE, BATTERY_INFO, GObject)
GrapheneBatteryInfo * graphene_battery_info_get_default(void); // Free with g_object_unref
gboolean graphene_battery_info_is_available(GrapheneBatteryInfo *self);
gdouble graphene_battery_info_get_percent(GrapheneBatteryInfo *self);
guint32 graphene_battery_info_get_state(GrapheneBatteryInfo *self);
const gchar * graphene_battery_info_get_state_string(GrapheneBatteryInfo *self);
gchar * graphene_battery_info_get_icon_name(GrapheneBatteryInfo *self); // Returns a newly-allocated string
gint64 graphene_battery_info_get_time(GrapheneBatteryInfo *self); // Time until charged or time until empty, depending on state

G_END_DECLS

#endif /* __GRAPHENE_BATTERY_H__ */
