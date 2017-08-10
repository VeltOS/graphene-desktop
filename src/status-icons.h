/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_STATUS_ICONS_H__
#define __GRAPHENE_STATUS_ICONS_H__

#include <cmk/cmk-icon.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_NETWORK_ICON  graphene_network_icon_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNetworkIcon, graphene_network_icon, GRAPHENE, NETWORK_ICON, CmkIcon)
GrapheneNetworkIcon * graphene_network_icon_new(gfloat size);

#define GRAPHENE_TYPE_VOLUME_ICON  graphene_volume_icon_get_type()
G_DECLARE_FINAL_TYPE(GrapheneVolumeIcon, graphene_volume_icon, GRAPHENE, VOLUME_ICON, CmkIcon)
GrapheneVolumeIcon * graphene_volume_icon_new(gfloat size);

#define GRAPHENE_TYPE_BATTERY_ICON  graphene_battery_icon_get_type()
G_DECLARE_FINAL_TYPE(GrapheneBatteryIcon, graphene_battery_icon, GRAPHENE, BATTERY_ICON, CmkIcon)
GrapheneBatteryIcon * graphene_battery_icon_new(gfloat size);

G_END_DECLS

#endif

