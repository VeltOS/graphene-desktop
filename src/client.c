/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *       http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <glib/gprintf.h>
#include <sys/wait.h>
#include <session-dbus-iface.h>
#include "client.h"
#include "util.h"

#define CLIENT_OBJECT_PATH "/org/gnome/SessionManager/Client"
#define MAX_RESTARTS 5

typedef struct
{
	guint cookie;
	gchar *reason;
	guint flags;
} Inhibition;

struct _GrapheneSessionClient
{
	GObject parent;

	// Unique client ID. Given to new processes so that they can register themselves properly (startupId).
	// Appended to the end of CLIENT_OBJECT_PATH to make a DBus object path for this
	// client once it's been registered.
	gchar *id;
	
	// Program info (set if available)
	gchar *name; // Human-readable name of program
	gchar *args;
	gchar *condition; // Condition for launching the program (https://lists.freedesktop.org/archives/xdg/2007-January/007436.html)
	                  // Also supports gnome-session keys (https://github.com/GNOME/gnome-session/blob/865a6da78d23bee85f3c7bd72157974a3a918c86/gnome-session/gsm-autostart-app.c)
	gchar *icon;
	gboolean silent; // If true, redirects stdout and stderr to dev/null
	gint delay;
	CSMClientAutoRestart autoRestart;
	
	// Registration info (set when registered)
	gchar *objectPath; // Convenience: CLIENT_OBJECT_PATH + id
	gchar *appId; // How the application identifies itself
	gchar *dbusName; // The dbus name that the program registered from ("sender")
	DBusSessionManagerClient *dbusClientSkeleton;
	DBusSessionManagerClientPrivate *dbusPClientSkeleton;
	guint busWatchId;
	GDBusConnection *connection;
	gboolean implicitRegistration; // FALSE if the client explicity registered
	
	// Process info (set when spawned or if available)
	GPid processId;
	guint spawnDelaySourceId;
	guint childWatchId;
	guint restartCount; // Number of times the process has crashed and been restarted
	
	GObject *conditionMonitor; // Set if monitoring the condition (free and NULL this to stop monitoring)
	gboolean forceNextRestart;
	
	// Flags
	gboolean alive, ready, failed, complete;

	GArray *inhibitions;
};

enum
{
	PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_ARGS,
	PROP_ICON,
	PROP_SILENT,
	PROP_DELAY,
	PROP_CONDITION,
	PROP_AUTO_RESTART,
	PROP_REGISTERED,
	PROP_ALIVE,
	PROP_READY,
	PROP_FAILED,
	PROP_COMPLETE,
	PROP_BUS_CONNECTION,
	PROP_LAST
};

enum
{
	SIGNAL_0,
	SIGNAL_END_SESSION_RESPONSE,
	SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static void graphene_session_client_dispose(GObject *self_);
static void graphene_session_client_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_session_client_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);

static gchar * generate_client_id();
static void set_alive(GrapheneSessionClient *self, gboolean alive);
static void set_ready(GrapheneSessionClient *self, gboolean ready);
static void set_failed(GrapheneSessionClient *self, gboolean failed);
static void try_set_complete(GrapheneSessionClient *self, gboolean complete);

static gboolean graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self);

static void graphene_session_client_unregister_internal(GrapheneSessionClient *self);

static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self);
static void on_client_vanished(GDBusConnection *connection, const gchar *name, GrapheneSessionClient *self);
static void on_client_exit(GrapheneSessionClient *self, guint status);

static gboolean test_condition(GrapheneSessionClient *self);
static void update_condition(GrapheneSessionClient *self);

static void connect_dbus_methods(GrapheneSessionClient *self);


G_DEFINE_TYPE(GrapheneSessionClient, graphene_session_client, G_TYPE_OBJECT);


/*
 * GrapheneSessionClient Class
 */

static void graphene_session_client_class_init(GrapheneSessionClientClass *class)
{
	GObjectClass *objectClass = G_OBJECT_CLASS(class);
	objectClass->dispose = graphene_session_client_dispose;
	objectClass->set_property = graphene_session_client_set_property;
	objectClass->get_property = graphene_session_client_get_property;
	
	properties[PROP_ID] =
		g_param_spec_string("id", "id", "aka startup id", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
	properties[PROP_NAME] =
		g_param_spec_string("name", "name", "readable name", NULL, G_PARAM_READWRITE);
	properties[PROP_ARGS] =
		g_param_spec_string("args", "args", "args passed for spawning the process", NULL, G_PARAM_READWRITE);
	properties[PROP_ICON] =
		g_param_spec_string("icon", "icon", "icon", NULL, G_PARAM_READWRITE);
	properties[PROP_SILENT] =
		g_param_spec_boolean("silent", "silent", "if all output is redirected to /dev/null", FALSE, G_PARAM_READWRITE);
	properties[PROP_DELAY] =
		g_param_spec_int("delay", "delay", "delay before spawning program in ms", 0, 100000, 0, G_PARAM_READWRITE); // TODO: Too lazy get max int
	properties[PROP_CONDITION] =
		g_param_spec_string("condition", "condition", "only launch of this condition is met (.desktop format)", NULL, G_PARAM_READWRITE);
	properties[PROP_AUTO_RESTART] =
		g_param_spec_int("auto-restart", "auto restart", "0: never restart, 1: only on crash, 2: always restart", 0, 2, 0, G_PARAM_READWRITE);
	properties[PROP_REGISTERED] =
		g_param_spec_boolean("registered", "registered", "if the client has been registered", FALSE, G_PARAM_READABLE);
	properties[PROP_ALIVE] = 
		g_param_spec_boolean("alive", "alive", "see client.h", FALSE, G_PARAM_READABLE);
	properties[PROP_READY] =
		g_param_spec_boolean("ready", "ready", "see client.h", FALSE, G_PARAM_READABLE);
	properties[PROP_FAILED] =
		g_param_spec_boolean("failed", "failed", "see client.h", FALSE, G_PARAM_READABLE);
	properties[PROP_COMPLETE] =
		g_param_spec_boolean("complete", "complete", "see client.h", TRUE, G_PARAM_READABLE);
	properties[PROP_BUS_CONNECTION] =
		g_param_spec_object("bus", "bus", "bus connection", G_TYPE_DBUS_CONNECTION, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
	
	g_object_class_install_properties(objectClass, PROP_LAST, properties);

	/*
	 * Emitted from a DBus call to org.gnome.SessionManager.ClientPrivate.EndSessionResponse.
	 * First parameter is isOk, if it is okay to proceed with end session.
	 * Second parameter is reason, which specifies a reason for not proceeding if isOk is false.
	 * Can release this client object on this event if this event is in response to the EndSession signal (not QueryEndSession)
	 */
	signals[SIGNAL_END_SESSION_RESPONSE] = g_signal_new("end-session-response", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_STRING);
}

/*
 * Creates a new, empty client, optionally with an inital clientId.
 * If clientId is NULL, a default one will be generated.
 * connection should be the application's default GDBusConnection. Must not be NULL.
 */
GrapheneSessionClient* graphene_session_client_new(GDBusConnection *connection, const gchar *clientId)
{
	return GRAPHENE_SESSION_CLIENT(g_object_new(GRAPHENE_TYPE_SESSION_CLIENT, "bus", connection, "id", clientId, NULL));
}

static void graphene_session_client_init(GrapheneSessionClient *self)
{
	self->inhibitions = g_array_new(FALSE, FALSE, sizeof(Inhibition));
}

static void graphene_session_client_dispose(GObject *self_)
{
	GrapheneSessionClient *self = GRAPHENE_SESSION_CLIENT(self_);

	graphene_session_client_kill(self);
	graphene_session_client_unregister_internal(self);
	
	if(self->childWatchId)
		g_source_remove(self->childWatchId);
	self->childWatchId = 0;
	if(self->spawnDelaySourceId)
		g_source_remove(self->spawnDelaySourceId);
	self->spawnDelaySourceId = 0;
	g_clear_pointer(&self->name, g_free);
	g_clear_pointer(&self->args, g_free);
	g_clear_pointer(&self->condition, g_free);
	g_clear_pointer(&self->icon, g_free);
	g_clear_pointer(&self->id, g_free);

	if(self->inhibitions)
	{
		for(guint i=0;i<self->inhibitions->len;++i)
		{
			Inhibition *inh = &g_array_index(self->inhibitions, Inhibition, i);
			if(inh)
				g_free(inh->reason);
		}
		g_array_free(self->inhibitions, TRUE);
		self->inhibitions = NULL;
	}

	G_OBJECT_CLASS(graphene_session_client_parent_class)->dispose(G_OBJECT(self));
}

static void graphene_session_client_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self_));
	GrapheneSessionClient *self = GRAPHENE_SESSION_CLIENT(self_);

	switch(propertyId)
	{
	case PROP_ID:
		g_clear_pointer(&self->id, g_free);
		const gchar *valStr = g_value_get_string(value);
		self->id = valStr ? g_strdup(valStr) : generate_client_id();
		break;
	case PROP_NAME:
		g_clear_pointer(&self->name, g_free);
		self->name = g_strdup(g_value_get_string(value));
		break;
	case PROP_ARGS:
		g_clear_pointer(&self->args, g_free);
		self->args = g_strdup(g_value_get_string(value));
		break;
	case PROP_ICON:
		g_clear_pointer(&self->icon, g_free);
		self->icon = g_strdup(g_value_get_string(value));
		break;
	case PROP_SILENT:
		self->silent = g_value_get_boolean(value);
		break;
	case PROP_DELAY:
		self->delay = g_value_get_int(value);
		break;
	case PROP_CONDITION:
		g_clear_pointer(&self->condition, g_free);
		self->condition = g_strdup(g_value_get_string(value));
		update_condition(self);
		break;
	case PROP_AUTO_RESTART:
		self->autoRestart = g_value_get_int(value);
		break;
	case PROP_BUS_CONNECTION:
		self->connection = G_DBUS_CONNECTION(g_value_get_object(value));
		break;
	case PROP_COMPLETE:
		self->complete = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}

static void graphene_session_client_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self_));
	GrapheneSessionClient *self = GRAPHENE_SESSION_CLIENT(self_);

	switch(propertyId)
	{
	case PROP_ID:
		g_value_set_string(value, self->id);
		break;
	case PROP_NAME:
		g_value_set_string(value, self->name);
		break;
	case PROP_ARGS:
		g_value_set_string(value, self->args);
		break;
	case PROP_ICON:
		g_value_set_string(value, self->icon);
		break;
	case PROP_SILENT:
		g_value_set_boolean(value, self->silent);
		break;
	case PROP_DELAY:
		g_value_set_int(value, self->delay);
		break;
	case PROP_CONDITION:
		g_value_set_string(value, self->condition);
		break;
	case PROP_AUTO_RESTART:
		g_value_set_int(value, self->autoRestart);
		break;
	case PROP_REGISTERED:
		g_value_set_boolean(value, self->objectPath != NULL);
		break;
	case PROP_ALIVE:
		g_value_set_boolean(value, self->alive);
		break;
	case PROP_READY:
		g_value_set_boolean(value, self->ready);
		break;
	case PROP_FAILED:
		g_value_set_boolean(value, self->failed);
		break;
	case PROP_COMPLETE:
		g_value_set_boolean(value, self->complete);
		break;
	case PROP_BUS_CONNECTION:
		g_value_set_object(value, self->connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
		break;
	}
}



/*
 * Util
 */

static gchar * generate_client_id()
{
	// TODO: Generate ID according to XSMP standard
	
	static const guint32 length = 17;
	gchar *id = g_new(gchar, 17+1);
	id[0] = '0';
	id[length] = '\0';
	
	for(guint32 i=1;i<length;++i)
		g_sprintf(id + i, "%x", g_random_int() % 16);
	
	return id;
}

static void set_alive(GrapheneSessionClient *self, gboolean alive)
{
	if(alive)
	{
		set_failed(self, FALSE);
		try_set_complete(self, FALSE);
	}

	if(self->alive != alive)
	{
		self->alive = alive;
		g_debug("setting alive: %i", alive);
		g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ALIVE]);
	}
}

static void set_ready(GrapheneSessionClient *self, gboolean ready)
{
	if(ready)
		set_failed(self, FALSE);	
	if(self->ready != ready)
	{
		self->ready = ready;
		g_debug("setting ready: %i", ready);
		g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_READY]);
	}
}

static void set_failed(GrapheneSessionClient *self, gboolean failed)
{
	if(failed)
		set_ready(self, FALSE);
	if(self->failed != failed)
	{
		self->failed = failed;
		g_debug("setting failed: %i", failed);
		g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_FAILED]);
	}
}

static void try_set_complete(GrapheneSessionClient *self, gboolean complete)
{
	if(self->conditionMonitor != NULL)
		complete = FALSE;
	if(complete)
		set_alive(self, FALSE);
	if(self->complete != complete)
	{
		self->complete = complete;
		g_debug("setting complete: %i", complete);
		g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_COMPLETE]);
	}
}



/*
 * Spawning / Session Commands
 */

void graphene_session_client_spawn(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	if(self->delay > 0)
		self->spawnDelaySourceId = g_timeout_add(self->delay, (GSourceFunc)graphene_session_client_spawn_delay_cb, self);
	else
		graphene_session_client_spawn_delay_cb(self);
}

static gboolean graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self)
{
	if(self->spawnDelaySourceId)
		g_source_remove(self->spawnDelaySourceId);
	self->spawnDelaySourceId = 0;
	
	if(self->alive || self->processId)
	{
		return G_SOURCE_REMOVE;
	}
	else if(!self->args)
	{
		g_warning("Cannot spawn client '%s' because args is not set", graphene_session_client_get_best_name(self));
		return G_SOURCE_REMOVE;
	}
	else if(!test_condition(self))
	{
		g_debug("Cannot spawn client '%s' immediately because condition is not met (might spawn later)", graphene_session_client_get_best_name(self));
		set_ready(self, TRUE);
		return G_SOURCE_REMOVE;
	}
	
	set_alive(self, G_SOURCE_REMOVE);
	set_ready(self, G_SOURCE_REMOVE);

	GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
	if(self->silent)
		flags |= G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL;
	
	GPid pid = 0;
	GError *e = NULL;

	gchar **argsSplit;
	if(!g_shell_parse_argv(self->args, NULL, &argsSplit, &e))
	{
		g_critical("Failed to parse args '%s' (%s) to start process", self->args, e->message);
		g_error_free(e);
		return G_SOURCE_REMOVE;
	}
	gchar *startupIdVar = g_strdup_printf("DESKTOP_AUTOSTART_ID=%s", self->id);
	gchar **env = strv_append((const char * const *)g_get_environ(), startupIdVar);
	
	g_spawn_async(NULL, argsSplit, env, flags, NULL, NULL, &pid, &e);
	
	g_strfreev(env);
	g_free(startupIdVar);
	g_strfreev(argsSplit);

	if(e)
	{
		g_critical("Failed to start process with args '%s' (%s)", self->args, e->message);
		g_error_free(e);
		return G_SOURCE_REMOVE;
	}
	
	self->processId = pid;
	set_alive(self, TRUE);

	if(self->processId)
		self->childWatchId = g_child_watch_add(self->processId, (GChildWatchFunc)on_process_exit, self);
	
	update_condition(self); // Reset condition monitor, in case it was stopped

	g_debug(" + Spawned client with args '%s' with id '%s' and pId %i", self->args, self->id, self->processId);
	return G_SOURCE_REMOVE;
}

void graphene_session_client_term(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	if(!self->alive)
		return;
	g_debug("requesting term client '%s'", graphene_session_client_get_best_name(self));
	if(self->connection && self->dbusName && self->objectPath)
	{
		g_message("sending stop");
		g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
			"org.gnome.SessionManager.ClientPrivate", "Stop", NULL, NULL);
	}
	else if(self->processId)
	{
		g_debug(" - Client '%s' is not registered. Sending SIGTERM to %i to stop client.", graphene_session_client_get_best_name(self), self->processId);
		kill(self->processId, SIGTERM);
	}
	else
	{
		g_debug("Attempted to stop client '%s', but neither process id nor dbus object were available", graphene_session_client_get_best_name(self));
	}
}

void graphene_session_client_kill(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	if(!self->alive)
		return;
	g_debug("killing client '%s'", graphene_session_client_get_best_name(self));
	if(self->processId)
	{
		kill(self->processId, SIGKILL);
	}
	else if(self->connection && self->dbusName && self->objectPath)
	{
		g_debug("cannot directly kill client, no process id available");
		g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
			"org.gnome.SessionManager.ClientPrivate", "Stop", NULL, NULL);
	}
	else
	{
		g_warning("Attempted to kill client '%s', but neither process id nor dbus object were available", graphene_session_client_get_best_name(self));
	}
}

void graphene_session_client_restart(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	if(!self->alive)
		return;
	// TODO
}

/*
 * Registration
 */

void graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId, gboolean implicit)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	graphene_session_client_unregister_internal(self);

	g_debug("Registering client '%s' with sender '%s', appId '%s', and objectPath '%s'", self->id, sender, appId, self->objectPath);

	self->implicitRegistration = implicit;
	set_alive(self, TRUE);
	set_ready(self, FALSE);

	self->objectPath = g_strdup_printf("%s%s", CLIENT_OBJECT_PATH, self->id);
	self->dbusName = g_strdup(sender);
	self->appId = g_strdup(appId);
	
	self->dbusClientSkeleton = dbus_session_manager_client_skeleton_new();
	self->dbusPClientSkeleton = dbus_session_manager_client_private_skeleton_new();
	connect_dbus_methods(self);
	
	if(!self->connection)
	{
		g_warning("Failed to register client: no DBus connection.");
		graphene_session_client_unregister_internal(self);
		if(!self->childWatchId)
			try_set_complete(self, TRUE);
		return;
	}

	GError *error = NULL;	
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->dbusClientSkeleton), self->connection, self->objectPath, &error))
	{
		g_warning("Failed to register client '%s': %s", graphene_session_client_get_best_name(self), error->message);
		graphene_session_client_unregister_internal(self);
		g_error_free(error);
		// If registration fails and the client wasn't spawned in, it's
		// as good as complete as far as the SM is concerned.
		if(!self->childWatchId)
			try_set_complete(self, TRUE);
		return;
	}
	
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->dbusPClientSkeleton), self->connection, self->objectPath, &error))
	{
		g_warning("Failed to register client '%s': %s", graphene_session_client_get_best_name(self), error->message);
		graphene_session_client_unregister_internal(self);
		g_error_free(error);
		if(!self->childWatchId)
			try_set_complete(self, TRUE);
		return;
	}

	if(!self->processId && self->dbusName)
	{
		GVariant *vpid = g_dbus_connection_call_sync(self->connection,
			"org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
			"GetConnectionUnixProcessID", g_variant_new("(s)", self->dbusName), G_VARIANT_TYPE("(u)"),
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	
		if(error)
		{
			g_warning("Failed to obtain process id of '%s': %s", graphene_session_client_get_best_name(self), error->message);
			g_clear_error(&error);
		}
		else
		{
			g_variant_get(vpid, "(u)", &self->processId);
			g_variant_unref(vpid);
		}
	}

	if(self->processId && !self->args)
	{
		gchar *processArgs = NULL;
		g_spawn_command_line_sync(
			g_strdup_printf("ps --pid %i -o args=", self->processId),
			&processArgs,
			NULL, NULL, NULL);
		if(processArgs)
		{
			self->args = str_trim(processArgs);
			g_free(processArgs);
			g_object_notify(G_OBJECT(self), "args");
			g_debug("Got registered process args: '%s'", self->args);
		}
	}

	self->busWatchId = g_bus_watch_name(G_BUS_TYPE_SESSION, self->dbusName, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, (GBusNameVanishedCallback)on_client_vanished, self, NULL);
	if(!self->busWatchId)
		g_warning("Failed to watch bus name of process '%s' (%s)", graphene_session_client_get_best_name(self), self->dbusName);
	
	g_debug(" + Registered client '%s' at path '%s'", graphene_session_client_get_best_name(self), self->objectPath);
	set_ready(self, TRUE);
}

static void graphene_session_client_unregister_internal(GrapheneSessionClient *self)
{
	if(self->busWatchId)
		g_bus_unwatch_name(self->busWatchId);
	self->busWatchId = 0;

	// g_dbus_interface_skeleton_unexport will fail an assert if there aren't any
	// connections, so be sure to check first.
	if(self->connection && self->dbusClientSkeleton && g_dbus_interface_skeleton_get_connection(G_DBUS_INTERFACE_SKELETON(self->dbusClientSkeleton)) != NULL)
		g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->dbusClientSkeleton));
	g_clear_object(&self->dbusClientSkeleton);

	if(self->connection && self->dbusPClientSkeleton && g_dbus_interface_skeleton_get_connection(G_DBUS_INTERFACE_SKELETON(self->dbusPClientSkeleton)) != NULL)
		g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->dbusPClientSkeleton));
	g_clear_object(&self->dbusPClientSkeleton);

	g_clear_pointer(&self->objectPath, g_free);
	g_clear_pointer(&self->appId, g_free);
	g_clear_pointer(&self->dbusName, g_free);
}


/*
 * Client death
 */

void graphene_session_client_unregister(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	g_debug(" - Client '%s' unregistered", graphene_session_client_get_best_name(self));
	graphene_session_client_unregister_internal(self);
	
	// If we have a child watch, wait for that to trigger before
	// claiming death of the client
	if(!self->childWatchId)
		on_client_exit(self, 0); // EXIT_SUCCESS
}

/*
 * Called when a registered client's DBus connection vanishes.
 * This is effectively the same as the process exiting, and therefore the client should be removed.
 */ 
static void on_client_vanished(GDBusConnection *connection, const gchar *name, GrapheneSessionClient *self)
{
	if(!self)
		return;
	g_debug(" - Client '%s', %s vanished", graphene_session_client_get_best_name(self), self->appId);

	graphene_session_client_unregister_internal(self);
	
	if(!self->childWatchId)
	{
		// If the client successfully unregistered itself, then on_client_vanished wouldn't be called.
		// So this is almost certainly an EXIT_FAILURE (1).
		on_client_exit(self, 1);
	}
}

static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self)
{
	if(!self)
		return;
	g_debug(" - Process %i, %s exited (status: %i)", pid, graphene_session_client_get_best_name(self), status);
	on_client_exit(self, status);
}

/*
 * Removes all client registration and process info. This should be called to make the client be considered dead.
 */
static void destroy_client_info(GrapheneSessionClient *self)
{
	graphene_session_client_unregister_internal(self);
	// TODO: Update values?
	if(self->childWatchId)
		g_source_remove(self->childWatchId);
	self->childWatchId = 0;
	self->processId = 0;
	set_alive(self, FALSE);
}
 
/*
 * Called when a client has exited. This may be due to the process exiting,
 * the DBus connection vanishing, or the process unregistering.
 * This should restart the client if necessary.
 */
static void on_client_exit(GrapheneSessionClient *self, guint status)
{
	gboolean wasRegistered = self->objectPath != NULL;
	
	// Make sure on_client_exit can't be called twice (once from dbus, once from child watch)
	// Also unregisters the client
	destroy_client_info(self);

	// Restart it
	g_debug("should restart? auto: %i, args: %s, status: %i, force: %i", self->autoRestart, self->args, status, self->forceNextRestart);
	if(self->forceNextRestart || (self->autoRestart > 0 && status != 0) || self->autoRestart == 2)
	{
		self->forceNextRestart = FALSE;
		if(self->restartCount < MAX_RESTARTS)
		{
			if(status != 0 && !self->forceNextRestart)
				self->restartCount++;
			g_debug("restarting client with args %s", self->args);

			gint delay = self->delay;
			if(status != 0) // If client failed, restart (almost) instantly.
				self->delay = 500; // ms
			graphene_session_client_spawn(self);
			self->delay = delay;
			return;
		}
		else
		{
			g_warning("The application with args '%s' has crashed too many times, and will not be automatically restarted.", self->args);
		}
	}
	else
	{
		g_debug("not restarting");
	}

	
	if(status == 0)
		set_ready(self, TRUE);
	else
		set_failed(self, TRUE);
	try_set_complete(self, TRUE);
}



/*
 * Condition management
 */

static gboolean test_condition(GrapheneSessionClient *self)
{
	if(!self->condition)
		return TRUE;
	
	gchar **tokens = g_strsplit(self->condition, " ", -1);
	guint numTokens = g_strv_length(tokens);
	gboolean result = FALSE;
	
	if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gsettings") == 0)
	{
		GVariant *variant = get_gsettings_value(tokens[1], tokens[2]);
		if(g_variant_type_equal(g_variant_get_type(variant), G_VARIANT_TYPE_BOOLEAN))
			result = g_variant_get_boolean(variant);
		g_variant_unref(variant);
	}
	else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "if-exists") == 0)
	{
		// TODO
	}
	else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "unless-exists") == 0)
	{
		// TODO
	}
	else if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gnome3") == 0)
	{
		if(g_ascii_strcasecmp(tokens[1], "if-session"))
			result = g_ascii_strcasecmp(tokens[2], "graphene") == 0;
		else if(g_ascii_strcasecmp(tokens[1], "unless-session"))
			result = g_ascii_strcasecmp(tokens[2], "graphene") != 0;
	}

	g_strfreev(tokens);
	
	if(!result)
		g_debug("condition not met for client '%s'", graphene_session_client_get_best_name(self));
	return result;
}

static void run_condition(GrapheneSessionClient *self)
{
	if(test_condition(self))
		graphene_session_client_spawn(self);
	else
		graphene_session_client_term(self);
}

static void update_condition(GrapheneSessionClient *self)
{
	g_clear_object(&self->conditionMonitor);

	if(!self->condition)
	{
		// If there is no condition, and the client isn't alive or going to spawn soon, then it's complete
		if(!self->alive && !self->spawnDelaySourceId)
			try_set_complete(self, TRUE);
		return;
	}

	gchar **tokens = g_strsplit(self->condition, " ", -1);
	guint numTokens = g_strv_length(tokens);
	
	if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gsettings") == 0)
	{
		self->conditionMonitor = monitor_gsettings_key(tokens[1], tokens[2], G_CALLBACK(run_condition), self);
	}
	else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "if-exists") == 0)
	{
		// TODO
	}
	else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "unless-exists") == 0)
	{
		// TODO
	}
	
	g_strfreev(tokens);
	
	run_condition(self);
}

//gboolean graphene_session_client_query_end_session(GrapheneSessionClient *self, gboolean forced)
//{
//	if(self->objectPath)
//	{
//		g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
//			"org.gnome.SessionManager.ClientPrivate", "QueryEndSession", g_variant_new("(u)", forced == TRUE), NULL);
//		return TRUE;
//	}
//	
//	return FALSE;
//}

//void graphene_session_client_end_session(GrapheneSessionClient *self, gboolean forced)
//{
//	g_message("end session on %s", graphene_session_client_get_best_name(self));
//	self->forceNextRestart = FALSE;
//	
//	g_message("%s", self->objectPath);
//	if(self->objectPath)
//	{
//		g_message("emit end session");
//		g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath, "org.gnome.SessionManager.ClientPrivate", "EndSession", g_variant_new("(u)", TRUE), NULL);
//	}
//	else if(self->processId)
//	{
//		g_message("send kill");
//		kill(self->processId, SIGKILL);
//	}
//	g_clear_object(&self->conditionMonitor);
//	//destroy_client_info(self); 
//	//try_set_complete(self, TRUE);
//}

void graphene_session_client_end_session(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	g_message("requesting term client '%s'", graphene_session_client_get_best_name(self));
	g_clear_object(&self->conditionMonitor);
	if(!self->alive)
	{
		g_message("already ded");
		try_set_complete(self, TRUE);
		return;
	}
	if(self->connection && self->dbusName && self->objectPath && !self->implicitRegistration)
	{
		g_message("sending end session");
		g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
			"org.gnome.SessionManager.ClientPrivate", "EndSession", NULL, NULL);
	}
	else if(self->processId)
	{
		g_debug(" - Client '%s' is not registered. Sending SIGTERM to %i to stop client.", graphene_session_client_get_best_name(self), self->processId);
		kill(self->processId, SIGKILL);
	}
}




/*
 * Getters
 */

const gchar * graphene_session_client_get_id(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
	return self->id;
}
const gchar * graphene_session_client_get_object_path(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
	return self->objectPath;
}
const gchar * graphene_session_client_get_app_id(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
	return self->appId;
}
const gchar * graphene_session_client_get_dbus_name(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
	return self->dbusName;
}
const gchar * graphene_session_client_get_best_name(GrapheneSessionClient *self)
{
	if(self->name)          return self->name;
	else if(self->appId)    return self->appId;
	else if(self->dbusName) return self->dbusName;
	else if(self->args)     return self->args;
	else                    return self->id;
}
gboolean graphene_session_client_get_is_alive(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	return self->alive;
}
gboolean graphene_session_client_get_is_ready(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	return self->ready;
}
gboolean graphene_session_client_get_is_failed(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	return self->failed;
}
gboolean graphene_session_client_get_is_complete(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	return self->complete;
}



/*
 * Other DBus Commands
 */

static gboolean on_dbus_get_app_id(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	// TODO: Test if this crashes if self->appId is NULL
	// GVariants sometimes get angry on NULLs
	dbus_session_manager_client_complete_get_app_id(object, invocation, self->appId);
	return TRUE;
}

static gboolean on_dbus_get_startup_id(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	dbus_session_manager_client_complete_get_startup_id(object, invocation, self->id);
	return TRUE;
}

static gboolean on_dbus_get_restart_style_hint(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	// From GNOME SM code
	// 0=never, 1=if-running, 2=anyway, 3=immediately
	// Not sure what 1 or 2 mean, but we'll go with 0 and 3 always (TODO: for now)
	dbus_session_manager_client_complete_get_restart_style_hint(object, invocation,
		(self->autoRestart == CSM_CLIENT_RESTART_NEVER) ? 0 : 3);
	return TRUE;
}

static gboolean on_dbus_get_unix_process_id(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	dbus_session_manager_client_complete_get_unix_process_id(object, invocation, self->processId);
	return TRUE;
}

static gboolean on_dbus_get_status(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	// From GNOME SM code
	// 0=unregistered, 1=registered, 2=finished, 3=failed
	guint status = 0;
	if(self->failed) // A client can be both failed and complete, but put failed first because it's more descriptive
		status = 3;
	else if(self->complete)
		status = 2;
	else
		status = (self->objectPath == NULL) ? 0 : 1;
	dbus_session_manager_client_complete_get_status(object, invocation, status);
	return TRUE;
}

static gboolean on_dbus_stop(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	graphene_session_client_term(self);
	dbus_session_manager_client_complete_stop(object, invocation);
	return TRUE;
}

static gboolean on_dbus_restart(DBusSessionManagerClient *object, GDBusMethodInvocation *invocation, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	graphene_session_client_restart(self);
	dbus_session_manager_client_complete_stop(object, invocation);
	return TRUE;
}

static gboolean on_dbus_end_session_response(DBusSessionManagerClientPrivate *object, GDBusMethodInvocation *invocation, gboolean isOk, const gchar *reason, GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	g_message("response: %i (%s)", isOk, graphene_session_client_get_best_name(self));
	try_set_complete(self, TRUE);
	//g_signal_emit(self, signals[SIGNAL_END_SESSION_RESPONSE], 0, isOk, reason);
	dbus_session_manager_client_private_complete_end_session_response(object, invocation);
	return TRUE;
}

static void connect_dbus_methods(GrapheneSessionClient *self)
{
	#define connect(s, f) g_signal_connect(self->dbusClientSkeleton, s, G_CALLBACK(f), self)
	#define connectp(s, f) g_signal_connect(self->dbusPClientSkeleton, s, G_CALLBACK(f), self)
	connect("handle-get-app-id", on_dbus_get_app_id);
	connect("handle-get-startup-id", on_dbus_get_startup_id);
	connect("handle-get-restart-style-hint", on_dbus_get_restart_style_hint);
	connect("handle-get-unix-process-id", on_dbus_get_unix_process_id);
	connect("handle-get-status", on_dbus_get_status);
	connect("handle-stop", on_dbus_stop);
	connect("handle-restart", on_dbus_restart);
	connectp("handle-end-session-response", on_dbus_end_session_response);
	#undef connectp
	#undef connect
}

void graphene_session_client_lost_dbus(GrapheneSessionClient *self)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	self->connection = NULL;
}

guint graphene_session_client_add_inhibition(GrapheneSessionClient *self, const gchar *reason, guint flags)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), 0);
	static guint cookie = 0;
	Inhibition inh = { ++cookie, g_strdup(reason), flags };
	g_array_append_val(self->inhibitions, inh);
	return cookie;
}

void graphene_session_client_remove_inhibition(GrapheneSessionClient *self, guint cookie)
{
	g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
	for(guint i=0;i<self->inhibitions->len;++i)
	{
		Inhibition *inh = &g_array_index(self->inhibitions, Inhibition, i);
		if(inh && inh->cookie == cookie)
		{
			g_free(inh->reason);
			g_array_remove_index_fast(self->inhibitions, i);
			return;
		}	
	}
}

gboolean graphene_session_client_is_inhibited(GrapheneSessionClient *self)
{
	g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), FALSE);
	return self->inhibitions->len > 0;
}
