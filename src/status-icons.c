/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "status-icons.h"
#include "csk/network.h"

struct _GrapheneNetworkIcon
{
	CmkIcon parent;
	CskNetworkManager *mn;
};

static void graphene_network_icon_dispose(GObject *self_);
static void network_icon_on_update(GrapheneNetworkIcon *self);

G_DEFINE_TYPE(GrapheneNetworkIcon, graphene_network_icon, CMK_TYPE_ICON)


GrapheneNetworkIcon * graphene_network_icon_new(gfloat size)
{
	GrapheneNetworkIcon *self = GRAPHENE_NETWORK_ICON(g_object_new(GRAPHENE_TYPE_NETWORK_ICON, "use-foreground-color", TRUE, NULL));
	if(size > 0)
		cmk_icon_set_size(CMK_ICON(self), size);
	return self;
}

static void graphene_network_icon_class_init(GrapheneNetworkIconClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_network_icon_dispose;
}

static void graphene_network_icon_init(GrapheneNetworkIcon *self)
{
	self->mn = csk_network_manager_get_default();
	g_signal_connect_swapped(self->mn, "notify::icon", G_CALLBACK(network_icon_on_update), self);
	network_icon_on_update(self);
}

static void graphene_network_icon_dispose(GObject *self_)
{
	g_clear_object(&GRAPHENE_NETWORK_ICON(self_)->mn);
	G_OBJECT_CLASS(graphene_network_icon_parent_class)->dispose(self_);
}

static void network_icon_on_update(GrapheneNetworkIcon *self)
{
	g_message("set icon: %s", csk_network_manager_get_icon(self->mn));
	cmk_icon_set_icon(CMK_ICON(self), csk_network_manager_get_icon(self->mn));
}


#include "csk/audio.h"

struct _GrapheneVolumeIcon
{
	CmkIcon parent;
	CskAudioDeviceManager *audioManager;
	CskAudioDevice *defaultOutput;
};

static void graphene_volume_icon_dispose(GObject *self_);
static void volume_icon_on_default_output_changed(GrapheneVolumeIcon *self, const GParamSpec *spec, CskAudioDeviceManager *audioManager);
static void volume_icon_on_update(GrapheneVolumeIcon *self, const GParamSpec *spec, CskAudioDevice *device);

G_DEFINE_TYPE(GrapheneVolumeIcon, graphene_volume_icon, CMK_TYPE_ICON)


GrapheneVolumeIcon * graphene_volume_icon_new(gfloat size)
{
	GrapheneVolumeIcon *self = GRAPHENE_VOLUME_ICON(g_object_new(GRAPHENE_TYPE_VOLUME_ICON, "use-foreground-color", TRUE, NULL));
	if(size > 0)
		cmk_icon_set_size(CMK_ICON(self), size);
	return self;
}

static void graphene_volume_icon_class_init(GrapheneVolumeIconClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_volume_icon_dispose;
}

static void graphene_volume_icon_init(GrapheneVolumeIcon *self)
{
	self->audioManager = csk_audio_device_manager_get_default();
	g_signal_connect_swapped(self->audioManager, "notify::default-output", G_CALLBACK(volume_icon_on_default_output_changed), self);
	volume_icon_on_default_output_changed(self, NULL, self->audioManager);
}

static void graphene_volume_icon_dispose(GObject *self_)
{
	g_clear_object(&GRAPHENE_VOLUME_ICON(self_)->audioManager);
	G_OBJECT_CLASS(graphene_volume_icon_parent_class)->dispose(self_);
}

static void volume_icon_on_default_output_changed(GrapheneVolumeIcon *self, const GParamSpec *spec, CskAudioDeviceManager *audioManager)
{
	if(CSK_IS_AUDIO_DEVICE(self->defaultOutput))
		g_signal_handlers_disconnect_by_data(self->defaultOutput, self);

	self->defaultOutput = csk_audio_device_manager_get_default_output(audioManager);
	if(!self->defaultOutput)
		return;

	g_signal_connect_swapped(self->defaultOutput, "notify::volume", G_CALLBACK(volume_icon_on_update), self);
	g_signal_connect_swapped(self->defaultOutput, "notify::muted", G_CALLBACK(volume_icon_on_update), self);
	volume_icon_on_update(self, NULL, self->defaultOutput);
}

static void volume_icon_on_update(GrapheneVolumeIcon *self, const GParamSpec *spec, CskAudioDevice *device)
{
	float volume = 0;
	if(self->defaultOutput && !csk_audio_device_get_muted(self->defaultOutput))
		volume = csk_audio_device_get_volume(self->defaultOutput);

	const gchar *iconName;
	
	if(volume == 0)
		iconName = "audio-volume-muted-symbolic";
	else if(volume >= (gfloat)2/3)
		iconName = "audio-volume-high-symbolic";
	else if(volume >= (gfloat)1/3)
		iconName = "audio-volume-medium-symbolic";
	else
		iconName = "audio-volume-low-symbolic";

	cmk_icon_set_icon(CMK_ICON(self), iconName);
}


#include "csk/battery.h"

struct _GrapheneBatteryIcon
{
	CmkIcon parent;
	CskBatteryInfo *batInfo;
};

static void graphene_battery_icon_dispose(GObject *self_);
static void battery_icon_on_update(GrapheneBatteryIcon *self, CskBatteryInfo *info);

G_DEFINE_TYPE(GrapheneBatteryIcon, graphene_battery_icon, CMK_TYPE_ICON)


GrapheneBatteryIcon * graphene_battery_icon_new(gfloat size)
{
	GrapheneBatteryIcon *self = GRAPHENE_BATTERY_ICON(g_object_new(GRAPHENE_TYPE_BATTERY_ICON, "use-foreground-color", TRUE, NULL));
	if(size > 0)
		cmk_icon_set_size(CMK_ICON(self), size);
	return self;
}

static void graphene_battery_icon_class_init(GrapheneBatteryIconClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_battery_icon_dispose;
}

static void graphene_battery_icon_init(GrapheneBatteryIcon *self)
{
	ClutterColor bg = {0,0,0,0}; // Bg doesn't actually matter
	ClutterColor fg = {255,0,0,255};
	cmk_widget_style_set_color(CMK_WIDGET(self), "warning", &bg); 
	cmk_widget_style_set_color(CMK_WIDGET(self), "warning-foreground", &fg); 

	self->batInfo = csk_battery_info_get_default();
	g_signal_connect_swapped(self->batInfo, "update", G_CALLBACK(battery_icon_on_update), self);
	battery_icon_on_update(self, self->batInfo);
}

static void graphene_battery_icon_dispose(GObject *self_)
{
	G_OBJECT_CLASS(graphene_battery_icon_parent_class)->dispose(self_);
}

static void battery_icon_on_update(GrapheneBatteryIcon *self, CskBatteryInfo *info)
{
	gchar *iconName = csk_battery_info_get_icon_name(info);
	cmk_icon_set_icon(CMK_ICON(self), iconName);
	g_free(iconName);

	if(csk_battery_info_get_percent(info) <= 15)
		cmk_widget_set_background_color_name(CMK_WIDGET(self), "warning");
	else
		cmk_widget_set_background_color_name(CMK_WIDGET(self), NULL);
}

