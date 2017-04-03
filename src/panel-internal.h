/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_PANEL_INTERNAL_H__
#define __GRAPHENE_PANEL_INTERNAL_H__

#include <libcmk/cmk-widget.h>
#include <libcmk/cmk-label.h>

G_BEGIN_DECLS

typedef void (*CSettingsLogoutCallback)(gpointer userdata);

#define GRAPHENE_TYPE_LAUNCHER_POPUP graphene_launcher_popup_get_type()
G_DECLARE_FINAL_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, GRAPHENE, LAUNCHER_POPUP, CmkWidget)
GrapheneLauncherPopup * graphene_launcher_popup_new(void);

#define GRAPHENE_TYPE_SETTINGS_POPUP graphene_settings_popup_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSettingsPopup, graphene_settings_popup, GRAPHENE, SETTINGS_POPUP, CmkWidget)
GrapheneSettingsPopup * graphene_settings_popup_new(CSettingsLogoutCallback logoutCb, gpointer userdata);

#define GRAPHENE_TYPE_CLOCK_LABEL graphene_clock_label_get_type()
G_DECLARE_FINAL_TYPE(GrapheneClockLabel, graphene_clock_label, GRAPHENE, CLOCK_LABEL, CmkLabel);
GrapheneClockLabel * graphene_clock_label_new(void);

G_END_DECLS

#endif
