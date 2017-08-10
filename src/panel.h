/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_PANEL_H__
#define __GRAPHENE_PANEL_H__

#include <cmk/cmk-widget.h>
#include "window.h"

G_BEGIN_DECLS

#define GRAPHENE_TYPE_PANEL  graphene_panel_get_type()
G_DECLARE_FINAL_TYPE(GraphenePanel, graphene_panel, GRAPHENE, PANEL, CmkWidget)

typedef void (*CPanelLogoutCallback)(gpointer userdata);

typedef enum
{
	GRAPHENE_PANEL_SIDE_TOP,
	GRAPHENE_PANEL_SIDE_BOTTOM,
} GraphenePanelSide;

GraphenePanel * graphene_panel_new(CPanelLogoutCallback logoutCb, gpointer userdata);

void graphene_panel_add_window(GraphenePanel *panel, GrapheneWindow *window);
void graphene_panel_remove_window(GraphenePanel *panel, GrapheneWindow *window);
void graphene_panel_update_window(GraphenePanel *panel, GrapheneWindow *window);

void graphene_panel_show_main_menu(GraphenePanel *panel);

// The main panel bar. Return value will not change after panel construction.
ClutterActor * graphene_panel_get_input_actor(GraphenePanel *panel);

GraphenePanelSide graphene_panel_get_side(GraphenePanel *panel);

G_END_DECLS

#endif /* __GRAPHENE_PANEL_H__ */
