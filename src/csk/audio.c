/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "audio.h"
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#define MAX_DEVICE_NAME_LENGTH 75 // These include NULL terminator
#define MAX_DEVICE_DESCRIPTION_LENGTH 100

struct _CskAudioDevice
{
	GObject parent;
	
	CskAudioDeviceManager *manager;
	CskAudioDeviceType type;
	guint32 index;

	char *name;
	char *hname; // "human readable" name
	char *description;
	float volume;
	float balance;
	gboolean mute;

	pa_cvolume cvolume;
};

struct _CskAudioDeviceManager
{
	GObject parent;

	pa_proplist *contextProps;
	pa_glib_mainloop *mainloop;
	pa_mainloop_api *mainloopAPI;
  	pa_context *context;
	gboolean ready;
	
	GList *devices;
	char *defaultSinkName;
	char *defaultSourceName;
	CskAudioDevice *defaultOutput; // Pointers to items in devices list,
	CskAudioDevice *defaultInput;  // may be NULL.
};

enum
{
	PROP_TYPE = 1,
	PROP_NAME,
	PROP_DESCRIPTION,
	PROP_VOLUME,
	PROP_BALANCE,
	PROP_MUTED,
	PROP_IS_DEFAULT_DEVICE,
	PROPD_LAST
};

static GParamSpec *propertiesD[PROPD_LAST];

static void csk_audio_device_dispose(GObject *self_);
static void csk_audio_device_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);

G_DEFINE_TYPE(CskAudioDevice, csk_audio_device, G_TYPE_OBJECT)

static void csk_audio_device_class_init(CskAudioDeviceClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_audio_device_dispose;
	base->get_property = csk_audio_device_get_property;

	propertiesD[PROP_TYPE] = g_param_spec_int("type", "type", "type", 0, 4, 0, G_PARAM_READABLE);
	propertiesD[PROP_NAME] = g_param_spec_string("name", "name", "name", NULL, G_PARAM_READABLE);
	propertiesD[PROP_DESCRIPTION] = g_param_spec_string("description", "description", "description", NULL, G_PARAM_READABLE);
	propertiesD[PROP_VOLUME] = g_param_spec_float("volume", "volume", "volume", 0, 2, 0, G_PARAM_READABLE);
	propertiesD[PROP_BALANCE] = g_param_spec_float("balance", "balance", "balance", -1, 1, 0, G_PARAM_READABLE);
	propertiesD[PROP_MUTED] = g_param_spec_boolean("muted", "muted", "muted", FALSE, G_PARAM_READABLE);
	propertiesD[PROP_IS_DEFAULT_DEVICE] = g_param_spec_boolean("is-default-device", "is default device", "is default device", FALSE, G_PARAM_READABLE);
	g_object_class_install_properties(base, PROPD_LAST, propertiesD);
}

static void csk_audio_device_init(UNUSED CskAudioDevice *self)
{
}

static void csk_audio_device_dispose(GObject *self_)
{
	CskAudioDevice *self = CSK_AUDIO_DEVICE(self_);
	g_free(self->name);
	g_free(self->hname);
	g_free(self->description);
	self->name = NULL;
	self->hname = NULL;
	self->description = NULL;
	self->type = CSK_AUDIO_DEVICE_TYPE_INVALID;
	self->manager = NULL;
	self->index = 0;
	G_OBJECT_CLASS(csk_audio_device_parent_class)->dispose(self_);
}

static void csk_audio_device_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	CskAudioDevice *self = CSK_AUDIO_DEVICE(self_);
	
	switch(propertyId)
	{
	case PROP_TYPE:
		g_value_set_int(value, (int)csk_audio_device_get_type_(self));
		break;
	case PROP_NAME:
		g_value_set_string(value, csk_audio_device_get_name(self));
		break;
	case PROP_DESCRIPTION:
		g_value_set_string(value, csk_audio_device_get_description(self));
		break;
	case PROP_VOLUME:
		g_value_set_float(value, csk_audio_device_get_volume(self));
		break;
	case PROP_BALANCE:
		g_value_set_float(value, csk_audio_device_get_balance(self));
		break;
	case PROP_MUTED:
		g_value_set_boolean(value, csk_audio_device_get_muted(self));
		break;
	case PROP_IS_DEFAULT_DEVICE:
		g_value_set_boolean(value, csk_audio_device_is_default(self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}

static gboolean audio_device_valid(CskAudioDevice *device)
{
	return device->type != CSK_AUDIO_DEVICE_TYPE_INVALID;
}

CskAudioDeviceType csk_audio_device_get_type_(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), CSK_AUDIO_DEVICE_TYPE_INVALID);
	return device->type;
}

const char * csk_audio_device_get_name(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), NULL);
	g_return_val_if_fail(audio_device_valid(device), NULL);
	return device->hname;
}

const char * csk_audio_device_get_description(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), NULL);
	g_return_val_if_fail(audio_device_valid(device), NULL);
	return device->description;
}

float csk_audio_device_get_volume(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), 0);
	g_return_val_if_fail(audio_device_valid(device), 0);
	return device->volume;
}

void csk_audio_device_set_volume(CskAudioDevice *device, float volume)
{
	g_return_if_fail(CSK_IS_AUDIO_DEVICE(device));
	g_return_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(device->manager));
	g_return_if_fail(audio_device_valid(device));

	pa_volume_t newVol = (pa_volume_t)(volume * (PA_VOLUME_NORM - PA_VOLUME_MUTED) + PA_VOLUME_MUTED);
	pa_cvolume_scale(&device->cvolume, newVol);
	pa_operation *o = pa_context_set_sink_volume_by_index(device->manager->context, device->index, &device->cvolume, NULL, NULL);
	//gboolean success = (o != NULL);
	if(o) pa_operation_unref(o);
}

float csk_audio_device_get_balance(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), 0);
	g_return_val_if_fail(audio_device_valid(device), 0);
	return device->balance;
}

void csk_audio_device_set_balance(CskAudioDevice *device, UNUSED float balance)
{
	g_return_if_fail(CSK_IS_AUDIO_DEVICE(device));
	g_return_if_fail(audio_device_valid(device));
	// TODO
}

gboolean csk_audio_device_get_muted(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), FALSE);
	g_return_val_if_fail(audio_device_valid(device), FALSE);
	return device->mute;
}

void csk_audio_device_set_muted(CskAudioDevice *device, gboolean muted)
{
	g_return_if_fail(CSK_IS_AUDIO_DEVICE(device));
	g_return_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(device->manager));
	g_return_if_fail(audio_device_valid(device));

	pa_operation *o = pa_context_set_sink_mute_by_index(device->manager->context, device->index, muted, NULL, NULL);
	//gboolean success = (o != NULL);
	if(o) pa_operation_unref(o);
}

gboolean csk_audio_device_is_default(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), FALSE);
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(device->manager), FALSE);
	g_return_val_if_fail(audio_device_valid(device), FALSE);
	if(device->type == CSK_AUDIO_DEVICE_TYPE_OUTPUT)
		return device->manager->defaultOutput == device;
	else if(device->type == CSK_AUDIO_DEVICE_TYPE_INPUT)
		return device->manager->defaultInput == device;
	return FALSE;
}

gboolean csk_audio_device_set_default(CskAudioDevice *device)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE(device), FALSE);
	g_return_val_if_fail(audio_device_valid(device), FALSE);
	// TODO
	return FALSE;
}



enum
{
	PROP_READY = 1,
	PROP_DEFAULT_OUTPUT,
	PROP_DEFAULT_INPUT,
	PROP_LAST
};

enum
{
	SIGNAL_DEVICE_ADDED = 1,
	SIGNAL_DEVICE_REMOVED = 1,
	SIGNAL_LAST
};

static GParamSpec *propertiesM[PROP_LAST];
static guint signalsM[SIGNAL_LAST];

static void csk_audio_device_manager_dispose(GObject *self_);
static void csk_audio_device_manager_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void unref_all_devices(CskAudioDeviceManager *self);
static void on_manager_pa_state_change(pa_context *context, CskAudioDeviceManager *self);
static void on_manager_pa_event(pa_context *context, pa_subscription_event_type_t type, uint32_t index, CskAudioDeviceManager *self);
static void on_manager_server_get_info(pa_context *context, const pa_server_info *server, CskAudioDeviceManager *self);
static void on_manager_sink_get_info(pa_context *context, const pa_sink_info *sink, int eol, CskAudioDeviceManager *self);
static void on_manager_source_get_info(pa_context *context, const pa_source_info *source, int eol, CskAudioDeviceManager *self);
static CskAudioDevice * get_device(CskAudioDeviceManager *self, guint32 index, CskAudioDeviceType type, gboolean create, gboolean *created);

G_DEFINE_TYPE(CskAudioDeviceManager, csk_audio_device_manager, G_TYPE_OBJECT)

CskAudioDeviceManager * csk_audio_device_manager_get_default()
{
	static CskAudioDeviceManager *global = NULL;
	if(CSK_AUDIO_DEVICE_MANAGER(global))
		return g_object_ref(global);
	global = CSK_AUDIO_DEVICE_MANAGER(g_object_new(CSK_TYPE_AUDIO_DEVICE_MANAGER, NULL));
	return global;
}

static void csk_audio_device_manager_class_init(CskAudioDeviceManagerClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = csk_audio_device_manager_dispose;
	base->get_property = csk_audio_device_manager_get_property;

	propertiesM[PROP_READY] = g_param_spec_boolean("ready", "ready", "ready", FALSE, G_PARAM_READABLE);
	propertiesM[PROP_DEFAULT_OUTPUT] = g_param_spec_object("default-output", "default output", "default output device", CSK_TYPE_AUDIO_DEVICE, G_PARAM_READABLE);
	propertiesM[PROP_DEFAULT_INPUT] = g_param_spec_object("default-input", "default input", "default input device", CSK_TYPE_AUDIO_DEVICE, G_PARAM_READABLE);
	g_object_class_install_properties(base, PROP_LAST, propertiesM);

	signalsM[SIGNAL_DEVICE_ADDED] = g_signal_new("device-added", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, CSK_TYPE_AUDIO_DEVICE);
	signalsM[SIGNAL_DEVICE_REMOVED] = g_signal_new("device-removed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, CSK_TYPE_AUDIO_DEVICE);
}

static void csk_audio_device_manager_init(CskAudioDeviceManager *self)
{
	self->contextProps = pa_proplist_new();
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "graphene-window-manager");
	// pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, g_application_get_application_id(g_application_get_default()));
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "multimedia-volume-control-symbolic");
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, WM_VERSION_STRING);
	
	self->mainloop = pa_glib_mainloop_new(g_main_context_default());
	self->mainloopAPI = pa_glib_mainloop_get_api(self->mainloop);
	self->context = pa_context_new_with_proplist(self->mainloopAPI, NULL, self->contextProps);
	
	pa_context_set_state_callback(self->context, (pa_context_notify_cb_t)on_manager_pa_state_change, self);
	pa_context_set_subscribe_callback(self->context, (pa_context_subscribe_cb_t)on_manager_pa_event, self);

	// NOFAIL: Instead of failing if the PulseAudio daemon is unavailable, enter
	// the "connecting" state and wait for it to appear.
	pa_context_connect(self->context, NULL, PA_CONTEXT_NOFAIL, NULL);
}

static void csk_audio_device_manager_dispose(GObject *self_)
{
	CskAudioDeviceManager *self = CSK_AUDIO_DEVICE_MANAGER(self_);
	
	unref_all_devices(self);

	if(self->context)
	{
		pa_context_set_subscribe_callback(self->context, NULL, NULL);
		pa_context_set_state_callback(self->context, NULL, NULL);
		pa_context_disconnect(self->context);
		pa_context_unref(self->context);
		self->context = NULL;
	}

	if(self->mainloop)
	{
		pa_glib_mainloop_free(self->mainloop);
		self->mainloop = NULL;
	}

	if(self->contextProps)
	{
		pa_proplist_free(self->contextProps);
		self->contextProps = NULL;
	}

	self->mainloopAPI = NULL;

	g_free(self->defaultSinkName);
	g_free(self->defaultSourceName);
	self->defaultSinkName = NULL;
	self->defaultSourceName = NULL;

	G_OBJECT_CLASS(csk_audio_device_manager_parent_class)->dispose(self_);
}

static void csk_audio_device_manager_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	CskAudioDeviceManager *self = CSK_AUDIO_DEVICE_MANAGER(self_);
	
	switch(propertyId)
	{
	case PROP_READY:
		g_value_set_boolean(value, self->ready);
		break;
	case PROP_DEFAULT_OUTPUT:
		g_value_set_object(value, self->defaultOutput);
		break;
	case PROP_DEFAULT_INPUT:
		g_value_set_object(value, self->defaultInput);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}

gboolean csk_audio_device_manager_is_ready(CskAudioDeviceManager *self)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(self), FALSE);
	return self->ready;
}

CskAudioDevice * csk_audio_device_manager_get_default_output(CskAudioDeviceManager *self)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(self), NULL);
	return self->defaultOutput;
}

CskAudioDevice * csk_audio_device_manager_get_default_input(CskAudioDeviceManager *self)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(self), NULL);
	return self->defaultInput;
}

static void unref_all_devices(CskAudioDeviceManager *self)
{
	for(GList *it=self->devices; it!=NULL; it=self->devices)
	{
		CskAudioDevice *device = CSK_AUDIO_DEVICE(it->data);

		device->type = CSK_AUDIO_DEVICE_TYPE_INVALID;
		self->devices = g_list_delete_link(self->devices, it);

		g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_TYPE]);
		g_signal_emit(self, signalsM[SIGNAL_DEVICE_REMOVED], 0, device);
		g_object_unref(device);
		
		if(self->defaultOutput == device)
			self->defaultOutput = NULL;
		if(self->defaultInput == device)
			self->defaultInput = NULL;
	}
}

static gboolean reconnect(CskAudioDeviceManager *self)
{
	g_return_val_if_fail(CSK_IS_AUDIO_DEVICE_MANAGER(self), G_SOURCE_REMOVE);
	unref_all_devices(self);

	if(self->context)
	{
		pa_context_set_subscribe_callback(self->context, NULL, NULL);
		pa_context_set_state_callback(self->context, NULL, NULL);
		pa_context_disconnect(self->context);
		pa_context_unref(self->context);
		self->context = NULL;
	}

	self->context = pa_context_new_with_proplist(self->mainloopAPI, NULL, self->contextProps);
	
	pa_context_set_state_callback(self->context, (pa_context_notify_cb_t)on_manager_pa_state_change, self);
	pa_context_set_subscribe_callback(self->context, (pa_context_subscribe_cb_t)on_manager_pa_event, self);
	pa_context_connect(self->context, NULL, PA_CONTEXT_NOFAIL, NULL);

	return G_SOURCE_REMOVE;
}

static void on_manager_pa_state_change(pa_context *context, CskAudioDeviceManager *self)
{
	gboolean prevReady = self->ready;

	int state = pa_context_get_state(context);
	switch(state)
	{
	case PA_CONTEXT_READY:
	{
		pa_operation *o = pa_context_subscribe(
			self->context,
			PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER,
			NULL, NULL);
		if(o) pa_operation_unref(o);
		o = pa_context_get_server_info(self->context,
			(pa_server_info_cb_t)on_manager_server_get_info,
			self);
 		if(o) pa_operation_unref(o);
		o = pa_context_get_sink_info_list(self->context,
			(pa_sink_info_cb_t)on_manager_sink_get_info,
			self);
		if(o) pa_operation_unref(o);
		o = pa_context_get_source_info_list(self->context,
			(pa_source_info_cb_t)on_manager_source_get_info,
			self);
		if(o) pa_operation_unref(o);

		self->ready = TRUE;
		break;
	}
	case PA_CONTEXT_FAILED:
		// Failed state?
	default:
		self->ready = FALSE;
		unref_all_devices(self);
		// Reconnect
		if(state == PA_CONTEXT_FAILED)
			g_timeout_add_seconds(1, (GSourceFunc)reconnect, self);
		break;
	}

	if(self->ready != prevReady)
		g_object_notify_by_pspec(G_OBJECT(self), propertiesM[PROP_READY]);
}

static void on_manager_pa_event(UNUSED pa_context *context, pa_subscription_event_type_t type, uint32_t index, CskAudioDeviceManager *self)
{
	pa_subscription_event_type_t eFacility = (type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
	pa_subscription_event_type_t eType = (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
	pa_operation *o = NULL;
	
	if(eFacility == PA_SUBSCRIPTION_EVENT_SERVER)
	{
		o = pa_context_get_server_info(self->context, (pa_server_info_cb_t)on_manager_server_get_info, self);
	}
	else if(eType == PA_SUBSCRIPTION_EVENT_NEW || eType == PA_SUBSCRIPTION_EVENT_CHANGE)
	{
		if(eFacility == PA_SUBSCRIPTION_EVENT_SINK)
			o = pa_context_get_sink_info_by_index(self->context, index, (pa_sink_info_cb_t)on_manager_sink_get_info, self);
		else if(eFacility == PA_SUBSCRIPTION_EVENT_SOURCE)
			o = pa_context_get_source_info_by_index(self->context, index, (pa_source_info_cb_t)on_manager_source_get_info, self);
	}
	else if(eType == PA_SUBSCRIPTION_EVENT_REMOVE)
	{
		CskAudioDeviceType dt = CSK_AUDIO_DEVICE_TYPE_INVALID;
		if(eFacility == PA_SUBSCRIPTION_EVENT_SINK)
			dt = CSK_AUDIO_DEVICE_TYPE_OUTPUT;
		else if(eFacility == PA_SUBSCRIPTION_EVENT_SOURCE)
			dt = CSK_AUDIO_DEVICE_TYPE_INPUT;

		if(dt != CSK_AUDIO_DEVICE_TYPE_INVALID)
		{
			CskAudioDevice *device = get_device(self, index, dt, FALSE, NULL);
			if(device)
			{
				device->type = CSK_AUDIO_DEVICE_TYPE_INVALID;
				self->devices = g_list_remove(self->devices, device);
				g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_TYPE]);
				g_signal_emit(self, signalsM[SIGNAL_DEVICE_REMOVED], 0, device);
				g_object_unref(device);
				if(self->defaultOutput == device)
					self->defaultOutput = NULL;
				if(self->defaultInput == device)
					self->defaultInput = NULL;
			}
		}
	}

	if(o) pa_operation_unref(o);
}

static void on_manager_server_get_info(UNUSED pa_context *context, const pa_server_info *server, CskAudioDeviceManager *self)
{
	if(!server || !self)
		return;
	
	g_free(self->defaultSinkName);
	g_free(self->defaultSourceName);
	self->defaultSinkName = g_strdup(server->default_sink_name);
	self->defaultSourceName = g_strdup(server->default_source_name);
	
	for(GList *it=self->devices; it!=NULL; it=it->next)
	{
		CskAudioDevice *device = CSK_AUDIO_DEVICE(it->data);
		if(g_strcmp0(device->name, self->defaultSinkName))
			self->defaultOutput = device;
		else if(g_strcmp0(device->name, self->defaultSourceName))
			self->defaultInput = device;
	}

	// todo: There aren't that many server updates, but refreshing all the sinks and sources each time might be too laggy
	//       Avoid if possible
	// Refresh all the sinks and sources
	// These will repeatedly call on_*_get_info for each sink/source
	//pa_operation *o = pa_context_get_sink_info_list(self->context, (pa_sink_info_cb_t)on_sink_get_info, self);
	//if(o) pa_operation_unref(o);
	//o = pa_context_get_source_info_list(self->context, (pa_source_info_cb_t)on_source_get_info, self);
	//if(o) pa_operation_unref(o);
}

static void manager_set_device_info(
	CskAudioDeviceManager *self,
	CskAudioDevice *device,
	gboolean created,
	const char *name,
	const char *hname,
	const char *activePortDescription,
	pa_proplist *proplist,
	pa_cvolume volume,
	pa_channel_map channelMap,
	gboolean mute)
{
	float prevVolume = device->volume;
	float prevBalance = device->balance;	
	gboolean prevMute = device->mute;
	gboolean hnameChanged = FALSE;
	gboolean descriptionChanged = FALSE;
	
	if(g_strcmp0(device->name, name) != 0)
	{
		g_free(device->name);
		device->name = g_strdup(name);
	}

	if(g_strcmp0(device->hname, hname) != 0)
	{
		g_free(device->hname);
		device->hname = g_strdup(hname);
		hnameChanged = TRUE;
	}

	const char *description;
	if(activePortDescription)
		description = activePortDescription;
	else if(proplist && pa_proplist_contains(proplist, "device.profile.description"))
		description = pa_proplist_gets(proplist, "device.profile.description");
	else
		description = name;

	if(g_strcmp0(device->description, description) != 0)
	{
		g_free(device->description);
		device->description = g_strdup(description);
		descriptionChanged = TRUE;
	}

	device->volume = ((float)(pa_cvolume_max(&volume) - PA_VOLUME_MUTED))/(PA_VOLUME_NORM - PA_VOLUME_MUTED);
	device->balance = pa_cvolume_get_balance(&volume, &channelMap);
	device->mute = mute;
	device->cvolume = volume;

	gboolean wasDefault = (self->defaultOutput == device);
	if(g_strcmp0(device->name, self->defaultSinkName) == 0)
		self->defaultOutput = device;
	
	if(created)
	{
		g_signal_emit(self, signalsM[SIGNAL_DEVICE_ADDED], 0, device);
	}
	else
	{
		if(hnameChanged)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_NAME]);
		if(descriptionChanged)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_DESCRIPTION]);
		if(prevVolume != device->volume)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_VOLUME]);
		if(prevBalance != device->balance)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_BALANCE]);
		if(prevMute != device->mute)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_MUTED]);
		if((self->defaultOutput == device) != wasDefault)
			g_object_notify_by_pspec(G_OBJECT(device), propertiesD[PROP_IS_DEFAULT_DEVICE]);
	}

	if((self->defaultOutput == device) != wasDefault)
		g_object_notify_by_pspec(G_OBJECT(self), propertiesM[PROP_DEFAULT_OUTPUT]);
}

static void on_manager_sink_get_info(UNUSED pa_context *context, const pa_sink_info *sink, UNUSED int eol, CskAudioDeviceManager *self)
{
	if(!sink || !self)
		return; // When listing devices, a final NULL device will be sent (with eol = 1)
  
	gboolean created = FALSE;
	CskAudioDevice *device = get_device(self, sink->index, CSK_AUDIO_DEVICE_TYPE_OUTPUT, TRUE, &created);

	if(!device)
	{
		g_warning("Could not find or create audio sink device.");
		return;
	}
	
	manager_set_device_info(
		self,
		device,
		created,
		sink->name,
		sink->description,
		sink->active_port ? sink->active_port->description : NULL,
		sink->proplist,
		sink->volume,
		sink->channel_map,
		sink->mute);
}

static void on_manager_source_get_info(UNUSED pa_context *context, const pa_source_info *source, UNUSED int eol, CskAudioDeviceManager *self)
{
	if(!source || !self)
		return; // When listing devices, a final NULL device will be sent (with eol = 1)

	gboolean created = FALSE;
	CskAudioDevice *device = get_device(self, source->index, CSK_AUDIO_DEVICE_TYPE_INPUT, TRUE, &created);

	if(!device)
	{
		g_warning("Could not find or create audio source device.");
		return;
	}
	
	manager_set_device_info(
		self,
		device,
		created,
		source->name,
		source->description,
		source->active_port ? source->active_port->description : NULL,
		source->proplist,
		source->volume,
		source->channel_map,
		source->mute);
}

static CskAudioDevice * get_device(CskAudioDeviceManager *self, guint32 index, CskAudioDeviceType type, gboolean create, gboolean *created)
{
	if(created)
		*created = FALSE;
	
	// Find an existing device
	for(GList *it=self->devices; it!=NULL; it=it->next)
	{
		CskAudioDevice *device = CSK_AUDIO_DEVICE(it->data);
		if(device->type == type && device->index == index)
			return device;
	}
	
	if(!create)
		return NULL;
	if(created)
		*created = TRUE;
	
	CskAudioDevice *device = CSK_AUDIO_DEVICE(g_object_new(CSK_TYPE_AUDIO_DEVICE, NULL));
	device->manager = self;
	device->type = type;
	device->index = index;
	
	self->devices = g_list_prepend(self->devices, device);
	return device;
}
