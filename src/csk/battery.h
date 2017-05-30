/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * Requires UPower available over DBus.
 */

#ifndef __CSK_BATTERY_H__
#define __CSK_BATTERY_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define CSK_TYPE_BATTERY_INFO  csk_battery_info_get_type()
G_DECLARE_FINAL_TYPE(CskBatteryInfo, csk_battery_info, CSK, BATTERY_INFO, GObject)

/*
 * Gets the default battery info object.
 * Free with g_object_unref.
 */
CskBatteryInfo * csk_battery_info_get_default(void);

/*
 * Returns TRUE if a battery is attached to the system.
 */
gboolean csk_battery_info_is_available(CskBatteryInfo *self);

/*
 * Returns the percentage charge of the battery in [0,100].
 */
gdouble csk_battery_info_get_percent(CskBatteryInfo *self);

/*
 * Returns the state of the battery
 * Same as the UPower Device state enum, currently:
 * 0: Unknown, 1: Charging, 2: Discharging, 3: Empty,
 * 4: Fully charged, 5: Pending charge, 6: Pending discharge
 */
guint32 csk_battery_info_get_state(CskBatteryInfo *self);

/*
 * Get the battery's state as a string. Do not free.
 */
const gchar * csk_battery_info_get_state_string(CskBatteryInfo *self);

/*
 * Get the name of an icon to represent the battery state and charge.
 * Returns a newly-allocated string, free with g_free.
 */
gchar * csk_battery_info_get_icon_name(CskBatteryInfo *self);

/*
 * Get the estimated time remaining, in seconds, on charge or
 * discharge, depending on the battery state.
 */
gint64 csk_battery_info_get_time(CskBatteryInfo *self);

G_END_DECLS

#endif /* __CSK_BATTERY_H__ */
