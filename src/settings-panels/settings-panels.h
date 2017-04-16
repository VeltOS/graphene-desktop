/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_SETTINGS_PANELS_H__
#define __GRAPHENE_SETTINGS_PANELS_H__

#include <libcmk/cmk-widget.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_NETWORK_PANEL  graphene_network_panel_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNetworkPanel, graphene_network_panel, GRAPHENE, NETWORK_PANEL, CmkWidget)
GrapheneNetworkPanel * graphene_network_panel_new(void);



G_END_DECLS

#endif