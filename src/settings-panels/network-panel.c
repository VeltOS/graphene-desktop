/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "settings-panels.h"
#include "../network.h"

struct _GrapheneNetworkPanel
{
	CmkWidget parent;
};

static void graphene_network_panel_dispose(GObject *self_);

G_DEFINE_TYPE(GrapheneNetworkPanel, graphene_network_panel, CMK_TYPE_WIDGET)


GrapheneNetworkPanel * graphene_network_panel_new()
{
	GrapheneNetworkPanel *self = GRAPHENE_NETWORK_PANEL(g_object_new(GRAPHENE_TYPE_NETWORK_PANEL, NULL));
	return self;
}

static void graphene_network_panel_class_init(GrapheneNetworkPanelClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_network_panel_dispose;
}

static void graphene_network_panel_init(GrapheneNetworkPanel *self)
{
}

static void graphene_network_panel_dispose(GObject *self_)
{
	g_clear_object(&GRAPHENE_NETWORK_PANEL(self_)->networkControl);
	G_OBJECT_CLASS(graphene_network_panel_parent_class)->dispose(self_);
}