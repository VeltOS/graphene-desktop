/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * session.c
 * Session Manager for Graphene Desktop. Follows most of the specifications put
 * forth by GNOME at wiki.gnome.org/Projects/SessionManagement/NewGnomeSession.
 *
 * CSM = Graphene Session Manager (Because GSM = GNOME SM)
 * C is for Carbon because graphene is made of carbon. Get it? Yay.
 */

#include "session.h"
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "client.h"
#include "util.h"
#include "status-notifier-watcher.h"
#include <session-dbus-iface.h>
#include <stdio.h>
#include "pkauthdialog.h"
#include "dialog.h"

#define GRAPHENE_SESSION_NAME "Graphene"
#define SESSION_DBUS_NAME "org.gnome.SessionManager"
#define SESSION_DBUS_PATH "/org/gnome/SessionManager"
#define POLKIT_AUTH_AGENT_DBUS_PATH "/io/velt/PolicyKit1/AuthenticationAgent"
#define SHOW_ALL_OUTPUT TRUE // Set to TRUE for release; FALSE only shows output from .desktop files with 'Graphene-ShowOutput=true'

// Generated name is a bit too long...
typedef DBusOrgFreedesktopPolicyKit1AuthenticationAgent DBusPolkitAuthAgent; 

// Session phases happen in linear order and only happen at most once each.
typedef enum {
	// Connection to system and session dbus, aborts session on fail or
	// continues to STARTUP on completion of both connections.
	SESSION_PHASE_INIT = 0,

	// Launches required daemons and programs, waits for them to complete
	// or register before continuing to IDLE.
	SESSION_PHASE_STARTUP,

	// Launches user applications at the beginning, but does not watch
	// for their success. After that, it's idle waiting for clients to
	// send dbus commands.
	SESSION_PHASE_IDLE,

	// This phase cannot be aborted; any inhibiting clients should
	// have been resolved before this phase begins. If any clients
	// resist logout for too long, the session will self-destruct
	// (and depending on exitType, logout, reboot, or shutdown).
	SESSION_PHASE_EXIT,
} SessionPhase;

typedef enum {
	EXIT_LOGOUT,
	EXIT_REBOOT,
	EXIT_SHUTDOWN,
} ExitType;

typedef struct {
	CSMStartupCompleteCallback startupCb;
	CSMDialogCallback dialogCb;
	CSMQuitCallback quitCb;
	gpointer cbUserdata;

	// DBus
	GCancellable *cancel;
	GDBusConnection *eBus; // sEssion DBus Connection
	GDBusConnection *yBus; // sYstem DBus Connection
	guint dbusNameId;
	gboolean hasName;
	gboolean polkitIsRegistered;
	DBusSessionManager *dbusSMSkeleton;
	DBusPolkitAuthAgent *dbusPkAgentSkeleton;
	gchar *ldSessionObject; // DBus session object path provided by systemd-logind
	GrapheneStatusNotifierWatcher *statusNotifierWatcher;
	
	GList *pkAuthDialogList; // In case multiple requests come in at once, put them in a wait list. The first in the list is always the current one.

	SessionPhase phase;
	ExitType exitType;
	
	GList *clients;
} GrapheneSession;


static void on_ybus_connection_acquired(GObject *source, GAsyncResult *res, gpointer userdata);
static void on_logind_session_acquired(GDBusConnection *yBus, GAsyncResult *res, gpointer userdata);
static void on_polkit_auth_agent_registered(GDBusConnection *yBus, GAsyncResult *res, gpointer userdata);
static void on_ebus_connection_acquired(GObject *source, GAsyncResult *res, gpointer userdata);
static void on_eybus_connection_lost(GDBusConnection *eyBus, gboolean remotePeerVanished, GError *error, gpointer userdata);
static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, void *userdata);
static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, void *userdata);

static void do_startup();
static gboolean check_startup_complete();
static void do_idle_phase();

static void stop_self_destruct();
static void self_destruct_countdown();

static void on_client_notify_ready(GrapheneSessionClient *client);
static void on_client_notify_complete(GrapheneSessionClient *client);

static GHashTable * list_autostarts();
static void launch_autostart(GDesktopAppInfo *desktopInfo);

static void connect_dbus_methods();

static gboolean on_pk_agent_begin_authentication(DBusPolkitAuthAgent *object, GDBusMethodInvocation *invocation, const gchar *actionId, const gchar *message, const gchar *iconName, GVariant *details, const gchar *cookie, GVariant *identities);
static gboolean on_pk_agent_cancel_authentication(DBusPolkitAuthAgent *object, GDBusMethodInvocation *invocation, const gchar *cookie);


static GrapheneSession *session = NULL;

/*
 * Init
 */

void graphene_session_init(CSMStartupCompleteCallback startupCb, CSMDialogCallback dialogCb, CSMQuitCallback quitCb, gpointer cbUserdata)
{
	if(session || !startupCb || !dialogCb || !quitCb)
		return;
	
	session = g_new0(GrapheneSession, 1);
	session->phase = SESSION_PHASE_INIT;
	session->exitType = EXIT_LOGOUT;
	
	session->startupCb = startupCb;
	session->dialogCb = dialogCb;
	session->quitCb = quitCb;
	session->cbUserdata = cbUserdata;
	
	// Setup session bus things (export SM interface, etc) and system bus
	// things (systemd-logind interaction, etc) at the same time. If either one
	// fails, abort the session. Whichever one finishes last will run the
	// STARTUP phase to "begin" the session.
	// Also, the system bus setup part will split into two async paths, so
	// really it's the last of all three paths to finish...
	session->cancel = g_cancellable_new();
	g_bus_get(G_BUS_TYPE_SYSTEM, session->cancel, on_ybus_connection_acquired, NULL);
	g_bus_get(G_BUS_TYPE_SESSION, session->cancel, on_ebus_connection_acquired, NULL);
}

static void on_ybus_connection_acquired(GObject *source, GAsyncResult *res, gpointer userdata)
{
	GError *error = NULL;
	GDBusConnection *yBus = g_bus_get_finish(res, &error);
	if(!yBus || error)
	{
		g_critical("Failed to acquire System DBus connection.");
		g_clear_object(&yBus);
		g_clear_error(&error);
		graphene_session_exit(TRUE);
		return;	
	}
	
	g_message("Acquired System DBus connection.");
	g_signal_connect(yBus, "closed", G_CALLBACK(on_eybus_connection_lost), NULL);
	g_dbus_connection_set_exit_on_close(yBus, FALSE);
	session->yBus = yBus;
	
	g_dbus_connection_call(yBus,
		"org.freedesktop.login1",
		"/org/freedesktop/login1",
		"org.freedesktop.login1.Manager",
		"GetSessionByPID",
		g_variant_new("(u)", getpid()),
		G_VARIANT_TYPE("(o)"),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		session->cancel,
		(GAsyncReadyCallback)on_logind_session_acquired,
		NULL);
}

static void on_logind_session_acquired(GDBusConnection *yBus, GAsyncResult *res, gpointer userdata)
{
	GError *error = NULL;
	GVariant *ret = g_dbus_connection_call_finish(yBus, res, &error);
	if(!ret || error)
	{
		g_critical("Failed to find logind session");
		if(error)
			g_critical("%s", error->message);
		g_clear_error(&error);
		g_clear_object(&ret);
		graphene_session_exit(TRUE);
		return;
	}
	
	g_variant_get(ret, "(o)", &session->ldSessionObject);
	g_message("Got session ID: %s", session->ldSessionObject);
	g_variant_unref(ret);
	
	session->dbusPkAgentSkeleton = dbus_org_freedesktop_policy_kit1_authentication_agent_skeleton_new();
	g_signal_connect(session->dbusPkAgentSkeleton, "handle-begin-authentication", G_CALLBACK(on_pk_agent_begin_authentication), NULL);
	g_signal_connect(session->dbusPkAgentSkeleton, "handle-cancel-authentication", G_CALLBACK(on_pk_agent_cancel_authentication), NULL);
	
	// TODO: Failing to register as an authentication agent probably shouldn't
	// be fatal. The session could still run without it (although various
	// things may not work).
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(session->dbusPkAgentSkeleton), yBus, POLKIT_AUTH_AGENT_DBUS_PATH, NULL))
	{
		g_critical("Failed to export PolKit authentication agent dbus object.");
		graphene_session_exit(TRUE);
		return;
	}
	
	// PolKit just wants the session id, not the full object path
	// The object path is in the form /org/freedesktop/login1/session/<session id>
	gchar **tokens = g_strsplit(session->ldSessionObject, "/", -1);
	guint numTokens = g_strv_length(tokens);
	GVariant *sessionIdV = g_variant_new_string(tokens[numTokens-1]);
	g_strfreev(tokens);
	
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&builder, "{sv}", "session-id", sessionIdV);
	GVariant *dict = g_variant_builder_end(&builder);
	
	g_dbus_connection_call(yBus,
		"org.freedesktop.PolicyKit1",
		"/org/freedesktop/PolicyKit1/Authority",
		"org.freedesktop.PolicyKit1.Authority",
		"RegisterAuthenticationAgent",
		g_variant_new("((s@a{sv})ss)",
			"unix-session",
			dict,
			g_getenv("LANG"),
			POLKIT_AUTH_AGENT_DBUS_PATH),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		session->cancel,
		(GAsyncReadyCallback)on_polkit_auth_agent_registered,
		NULL);
}

static void on_polkit_auth_agent_registered(GDBusConnection *yBus, GAsyncResult *res, gpointer userdata)
{
	GError *error = NULL;
	GVariant *ret = g_dbus_connection_call_finish(yBus, res, &error);
	if(error)
	{
		g_critical("Failed to register as PolKit Authentication Agent: %s", error->message);
		g_clear_error(&error);
		graphene_session_exit(TRUE);
		return;
	}
	
	g_variant_unref(ret);
 
	g_message("Registered as authentication agent!");
	if(session->hasName)
	{
		g_message("Running session from auth registered");
		do_startup();
	}
}

static void on_ebus_connection_acquired(GObject *source, GAsyncResult *res, gpointer userdata)
{
	GError *error = NULL;
	GDBusConnection *eBus = g_bus_get_finish(res, &error);
	if(!eBus || error)
	{
		g_critical("Failed to acquire Session DBus connection.");
		g_clear_object(&eBus);
		g_clear_error(&error);
		graphene_session_exit(TRUE);
		return;	
	}
	
	g_message("Acquired Session DBus connection.");
	
	g_signal_connect(eBus, "closed", G_CALLBACK(on_eybus_connection_lost), NULL);
	g_dbus_connection_set_exit_on_close(eBus, FALSE);
	session->eBus = eBus;
	
	session->dbusSMSkeleton = dbus_session_manager_skeleton_new();
	connect_dbus_methods();
	dbus_session_manager_set_session_name(session->dbusSMSkeleton, GRAPHENE_SESSION_NAME);
	dbus_session_manager_set_session_is_active(session->dbusSMSkeleton, FALSE);
	// TODO: How does inhibited_actions work
	//dbus_session_manager_set_inhibited_actions(session->dbusSMSkeleton, ...);
	
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(session->dbusSMSkeleton), eBus, SESSION_DBUS_PATH, NULL))
	{
		g_critical("Failed to export SM dbus object.");
		graphene_session_exit(TRUE);
		return;
	}
	
	session->dbusNameId = g_bus_own_name_on_connection(eBus, 
		SESSION_DBUS_NAME,
		G_BUS_NAME_OWNER_FLAGS_REPLACE,
		on_dbus_name_acquired,
		on_dbus_name_lost,
		NULL,
		NULL);
}

static void on_eybus_connection_lost(GDBusConnection *eyBus, gboolean remotePeerVanished, GError *error, gpointer userdata)
{
	g_critical("Lost connecion to the Session or System DBus.");
	g_clear_object(&session->yBus);
	g_clear_object(&session->eBus);
	graphene_session_exit(TRUE);
}

static void on_dbus_name_acquired(GDBusConnection *eBus, const gchar *name, void *userdata)
{
	session->hasName = TRUE;
	g_message("Acquired name '%s' on the Session DBus", SESSION_DBUS_NAME);
	
	if(session->polkitIsRegistered)
	{
		g_message("Running session from dbus name acquired");
		do_startup();
	}
}

static void on_dbus_name_lost(GDBusConnection *eBus, const gchar *name, void *userdata)
{
	// Not necessarily fatal if the name is lost but connection isn't, so
	// keep the session alive. No new clients will be able to register, but
	// existing clients should be fine, and logout should work.
	session->hasName = FALSE;
	g_warning("Lost name on the Session DBus");
}



/*
 * Startup
 */

static void launch_desktop();

static void do_startup()
{
	g_return_if_fail(session->phase == SESSION_PHASE_INIT);
	g_message("==== STARTUP ====");
	
	session->phase = SESSION_PHASE_STARTUP;
	session->statusNotifierWatcher = graphene_status_notifier_watcher_new();
	launch_desktop();
	check_startup_complete();
	
	self_destruct_countdown();
}

static gboolean check_startup_complete()
{
	if(session->phase != SESSION_PHASE_STARTUP)
		return FALSE;
	for(GList *it = session->clients; it != NULL; it = it->next)
	{
		if(!graphene_session_client_get_is_ready(it->data))
		{
			g_message("... Client '%s' is not ready\n", graphene_session_client_get_best_name(it->data));
			return FALSE;
		}
	}
	
	do_idle_phase();
	return TRUE;
}

static void launch_desktop()
{
	GHashTable *autostarts = list_autostarts();
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, autostarts);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{    
		GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    	gchar *phase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");
	
		// Just launch all of the startup phases at once. Maybe give it order later,
		// but it doesn't make much difference.
		if(g_strcmp0(phase, "Initialization") == 0
		|| g_strcmp0(phase, "Panel") == 0
		|| g_strcmp0(phase, "Desktop") == 0)
		{
			launch_autostart(desktopInfo);
		}
	}
}



/*
 * Idle
 */

static void launch_apps();

static void do_idle_phase()
{
	g_return_if_fail(session->phase == SESSION_PHASE_STARTUP);
	g_message("===== IDLE =====");

	session->phase = SESSION_PHASE_IDLE;
	stop_self_destruct();
	dbus_session_manager_set_session_is_active(session->dbusSMSkeleton, TRUE);
	dbus_session_manager_emit_session_running(session->dbusSMSkeleton);
	if(session->startupCb)
		session->startupCb(session->cbUserdata);
	launch_apps();
}

static void launch_apps()
{
	GHashTable *autostarts = list_autostarts();
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, autostarts);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{    
		GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    	gchar *phase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");

		// Only launch applications not launched in launch_desktop
		if(g_strcmp0(phase, "Initialization") != 0
		&& g_strcmp0(phase, "WindowManager") != 0
		&& g_strcmp0(phase, "Panel") != 0
		&& g_strcmp0(phase, "Desktop") != 0)
		{
			launch_autostart(desktopInfo);
		}
	}
}



/*
 * Exit
 */

static void on_logout_dialog_close(GrapheneDialog *dialog, const gchar *button);
static void on_inhibitors_dialog_close(GrapheneDialog *dialog, const gchar *button);
static void do_exit(ExitType exitType, gboolean force);

void graphene_session_request_logout()
{	
	GrapheneDialog *dialog = graphene_dialog_new_simple(NULL, NULL, "Cancel", "Suspend", "Logout", "Restart", "Shutdown", NULL);
	g_signal_connect(dialog, "select", G_CALLBACK(on_logout_dialog_close), NULL);
	session->dialogCb(CLUTTER_ACTOR(dialog), session->cbUserdata);
}

static void on_logout_dialog_close(GrapheneDialog *dialog, const gchar *button)
{
	session->dialogCb(NULL, session->cbUserdata);
	if(g_strcmp0(button, "Suspend") == 0)
		system("systemctl suspend");
	else if(g_strcmp0(button, "Shutdown") == 0)
		do_exit(EXIT_SHUTDOWN, FALSE);
	else if(g_strcmp0(button, "Restart") == 0)
		do_exit(EXIT_REBOOT, FALSE);
	else if(g_strcmp0(button, "Logout") == 0)
		do_exit(EXIT_LOGOUT, FALSE); 
}

void notify_inhibitors()
{	
	const gchar *type = "logout";
	if(session->exitType == EXIT_SHUTDOWN)
		type = "shutdown";
	else if(session->exitType == EXIT_REBOOT)
		type = "restart";
	gchar *msg = g_strdup_printf("An application is blocking %s. Force %s?", type, type);
	GrapheneDialog *dialog = graphene_dialog_new_simple(type, NULL, "Cancel", "Force", NULL);
	g_signal_connect(dialog, "select", G_CALLBACK(on_inhibitors_dialog_close), NULL);
	session->dialogCb(CLUTTER_ACTOR(dialog), session->cbUserdata);
}

static void on_inhibitors_dialog_close(GrapheneDialog *dialog, const gchar *button)
{
	session->dialogCb(NULL, session->cbUserdata);
	if(g_strcmp0(button, "Force") == 0)
		do_exit(session->exitType, TRUE);
	else
		session->exitType = EXIT_LOGOUT; // Reset it, just in case
}

static void do_exit(ExitType exitType, gboolean force)
{
	g_return_if_fail(session->phase < SESSION_PHASE_EXIT);
	g_message("==== EXIT (%i) ====", exitType);
	session->exitType = exitType;
	
	if(!force)
	{
		// TODO: Check for systemd shutdown/restart inhibitors
		gboolean inhibited = FALSE;
		for(GList *it = session->clients; it != NULL; it=it->next)
		{
			GrapheneSessionClient *client = it->data;
			if(graphene_session_client_is_inhibited(client))
			{
				inhibited = TRUE;
				g_message("Client '%s' is blocking exit", graphene_session_client_get_best_name(client));
			}
		}
		
		if(inhibited)
		{
			notify_inhibitors();
			return;
		}
	}
	
	session->dialogCb(clutter_actor_new(), session->cbUserdata);
	
	session->phase = SESSION_PHASE_EXIT;
	//dbus_session_manager_set_session_is_active(session->dbusSMSkeleton, FALSE);
	//dbus_session_manager_emit_session_over(session->dbusSMSkeleton);
	
	// Inform all clients of the endsession
	// Once all clients close, the session will end automatically
	g_message("Num clients: %i", g_list_length(session->clients));
	for(GList *it = session->clients; it != NULL;)
	{
		GrapheneSessionClient *client = it->data;
		it=it->next;
		graphene_session_client_end_session(client);
	}

	// Start a countdown. If all the clients don't close before
	// it completes, the session will end anyway.
	self_destruct_countdown();
}

gboolean graphene_session_exit(gboolean failed)
{
	if(!session)
		return G_SOURCE_REMOVE;

	g_message("Session exiting...");

	// Cancelling operations may cause exit_internal to be called again, but
	// 'session' will be NULL by that point, so it'll have no effect.
	if(session->cancel)
		g_cancellable_cancel(session->cancel);
	g_clear_object(&session->cancel);

	// Kill and free any remaining client objects
	// (In a successful logout, there should be no clients left anyway)
	g_list_free_full(session->clients, g_object_unref);
	session->clients = NULL;

	// Destroy status notifier watcher
	g_clear_object(&session->statusNotifierWatcher);
	
	// May be blocking according to g_bus_unown_name source code
	if(session->dbusNameId)
		g_bus_unown_name(session->dbusNameId);
	session->dbusNameId = 0;

	if(session->dbusSMSkeleton)
		g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(session->dbusSMSkeleton));
	g_clear_object(&session->dbusSMSkeleton);

	g_clear_pointer(&session->ldSessionObject, g_free);

	// Flush and close the connection. This may be blocking.
	if(session->yBus)
		g_dbus_connection_flush_sync(session->yBus, NULL, NULL);
	if(session->eBus)
		g_dbus_connection_flush_sync(session->eBus, NULL, NULL);
	g_clear_object(&session->yBus);
	g_clear_object(&session->eBus);

	if(session->exitType == EXIT_REBOOT)
		system("reboot");
	else if(session->exitType == EXIT_SHUTDOWN)
		system("poweroff");

	CSMQuitCallback quitCb = session->quitCb;
	gpointer cbUserdata = session->cbUserdata;
	g_clear_pointer(&session, g_free);
	
	if(quitCb)
		quitCb(failed, cbUserdata);
	return G_SOURCE_REMOVE;
}

static void graphene_session_exit_on_idle(gboolean failed)
{
	// G_PRIORITY_HIGH-10 is higher than G_PRIORITY_HIGH
	// This "on idle" exit is so that the session can be exited from
	// callbacks, such as DBus method handlers, without breaking things.
	g_idle_add_full(G_PRIORITY_HIGH - 10, (GSourceFunc)graphene_session_exit, GINT_TO_POINTER(failed), NULL);
}


//static gboolean do_action_source_remove(void (*func)())
//{
//	g_return_val_if_fail(func, G_SOURCE_REMOVE);
//	func();
//	return G_SOURCE_REMOVE;
//}
//
//static void wait_idle(void (*func)())
//{
//	g_idle_add((GSourceFunc)do_action_source_remove, func);
//}
//
//static int wait_seconds(int seconds, void (*func)())
//{
//	return g_timeout_add_seconds(seconds, (GSourceFunc)do_action_source_remove, func);
//}

static int SelfDestructTimeoutId = 0;

static void stop_self_destruct()
{
	if(SelfDestructTimeoutId)
		g_source_remove(SelfDestructTimeoutId);
	SelfDestructTimeoutId = 0;
}

static gboolean on_self_destruct()
{
	g_message("==== SELF DESTRUCT ====");
	graphene_session_exit(TRUE);
	SelfDestructTimeoutId = 0;
	return G_SOURCE_REMOVE;
}

static void self_destruct_countdown()
{
	stop_self_destruct();
	SelfDestructTimeoutId = g_timeout_add_seconds(10, (GSourceFunc)on_self_destruct, NULL);
}



/*
 * Client Events
 * Some of these are sent back from the GrapheneSessionClient object, while
 * some are sent from DBus to the Client object. 
 */

static GrapheneSessionClient * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName)
{
	for(GList *clients=session->clients;clients!=NULL;clients=clients->next)
	{
		GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
		
		const gchar *clientId = graphene_session_client_get_id(client);
		const gchar *clientObjectPath = graphene_session_client_get_object_path(client);
		const gchar *clientAppId = graphene_session_client_get_app_id(client);
		const gchar *clientDbusName = graphene_session_client_get_dbus_name(client);
		
		if((clientId && id && g_strcmp0(clientId, id) == 0)
		|| (clientObjectPath && objectPath && g_strcmp0(clientObjectPath, objectPath) == 0)
		|| (clientAppId && appId && g_strcmp0(clientAppId, appId) == 0)
		|| (clientDbusName && dbusName && g_strcmp0(clientDbusName, dbusName) == 0))
			return client;
	}
	
	return NULL;
}

static gboolean on_client_register(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *appId, const gchar *startupId, gpointer userdata)
{
	const gchar *sender = g_dbus_method_invocation_get_sender(invocation);
	GrapheneSessionClient *client = find_client_from_given_info(startupId, NULL, appId, sender);

	if(!client)
	{
		client = graphene_session_client_new(session->eBus, NULL);
		g_object_connect(client,
			"signal::notify::complete", on_client_notify_complete, NULL,
			//"signal::end-session-response", on_client_end_session_response, NULL,
			NULL);
		session->clients = g_list_prepend(session->clients, client);
	}

	graphene_session_client_register(client, sender, appId, FALSE);
	const gchar *objectPath = graphene_session_client_get_object_path(client);
	
	if(objectPath != NULL)
	{
		dbus_session_manager_complete_register_client(object, invocation, objectPath);
		dbus_session_manager_emit_client_added(session->dbusSMSkeleton, objectPath);
		g_message("Client %s registered.", graphene_session_client_get_best_name(client));
		return TRUE;
	}

	on_client_notify_complete(client);
	g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to register client."); 
	return TRUE;
}

static void on_client_notify_ready(GrapheneSessionClient *client)
{
	if(!graphene_session_client_get_is_ready(client))
		return;
	g_message("Client %s is ready.", graphene_session_client_get_best_name(client));
	check_startup_complete();
}

static gboolean on_client_unregister(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *clientObjectPath, gpointer userdata)
{
	GrapheneSessionClient *client = find_client_from_given_info(NULL, clientObjectPath, NULL, NULL);
	if(client)
	{
		graphene_session_client_unregister(client);
		dbus_session_manager_emit_client_removed(session->dbusSMSkeleton, clientObjectPath);
		g_message("Client %s unregistered.", graphene_session_client_get_best_name(client));
	}
	dbus_session_manager_complete_unregister_client(object, invocation);
	return TRUE;
}

static void on_client_notify_complete(GrapheneSessionClient *client)
{
	if(!graphene_session_client_get_is_complete(client))
		return;
	g_message("Client %s is complete. Remain: %i", graphene_session_client_get_best_name(client), g_list_length(session->clients)-1);
	session->clients = g_list_remove(session->clients, client);
	g_object_unref(client);
	
	
	if(session->phase == SESSION_PHASE_STARTUP)
		check_startup_complete();
	else if(session->phase == SESSION_PHASE_EXIT && session->clients == NULL)
	{
		g_message("exit");
		graphene_session_exit_on_idle(FALSE);
	}
	//if(!check_startup_complete())
	//{
	//	// If all clients die, exit
	//	// This will happen at the end of a successful Logout
	//	// Exit on idle because on_client_notify_complete can be called indirectly from
	//	// on_client_register, a DBus callback.
	//	if(session->clients == NULL)
	//		graphene_session_exit_internal_on_idle(FALSE);
	//}
}



/*
 * Autostarting Clients
 */

/*
 * Gets a GHashTable of name->GDesktopAppInfo* containing all autostart .desktop files
 * in all system/user config directories. Also includes Graphene-specific .desktop files.
 *
 * Does not include any .desktop files with the Hidden attribute set to true, or
 * any that have the OnlyShowIn attribute set to something other than "Graphene" or "GNOME".
 *
 * Free the returned GHashTable by calling g_hash_table_unref() on the table itself.
 * All keys and values are automatically destroyed.
 */
static GHashTable * list_autostarts() 
{
  GHashTable *desktopInfoTable = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
  
  gchar **configDirs = strv_append(g_get_system_config_dirs(), g_get_user_config_dir()); // Important that the user config dir comes last (for overwriting)

  guint numConfigDirs = g_strv_length(configDirs);
  for(guint i=0;i<numConfigDirs;++i)
  {
    gchar *searchPath = g_strconcat(configDirs[i], "/autostart", NULL);
    GFile *dir = g_file_new_for_path(searchPath);
    
    GFileEnumerator *iter = g_file_enumerate_children(dir, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if(!iter)
    {
      g_warning("Failed to search the directory '%s' for .desktop files.", searchPath);
      continue;
    }
    
    GFileInfo *info = NULL;
    while(g_file_enumerator_iterate(iter, &info, NULL, NULL, NULL) && info != NULL)
    {
      const gchar *_name = g_file_info_get_name(info);
      if(!g_str_has_suffix(_name, ".desktop"))
        continue;
        
      gchar *name = g_strdup(_name);
      
      gchar *desktopInfoPath = g_strconcat(searchPath, "/", name, NULL);
      GDesktopAppInfo *desktopInfo = g_desktop_app_info_new_from_filename(desktopInfoPath);
      
      if(desktopInfo)
      {
        // "Hidden should have been called Deleted. ... It's strictly equivalent to the .desktop file not existing at all."
        // https://specifications.freedesktop.org/desktop-entry-spec/latest/ar01s05.html
        gboolean deleted = g_desktop_app_info_get_is_hidden(desktopInfo);
        gboolean shouldShow = g_desktop_app_info_get_show_in(desktopInfo, "GNOME")
                              || g_desktop_app_info_get_show_in(desktopInfo, "Graphene");
                              
        if(deleted || !shouldShow) // Hidden .desktops should be completely ignored
        {
          g_message("Skipping '%s' because it is hidden or not available for Graphene.", name);
          g_object_unref(desktopInfo);
          g_hash_table_remove(desktopInfoTable, name); // Overwrite previous entries of the same name
        }
        else
        {
          g_hash_table_insert(desktopInfoTable, name, desktopInfo); // Overwrite previous entries of the same name
        }
      }
      
      g_free(desktopInfoPath);
      
      info = NULL; // Automatically freed
    }
    
    g_object_unref(iter);
    g_free(searchPath);
  }
  g_strfreev(configDirs);
  return desktopInfoTable;
}

static void launch_autostart(GDesktopAppInfo *desktopInfo)
{
	GrapheneSessionClient *client = graphene_session_client_new(session->eBus, NULL);
	session->clients = g_list_prepend(session->clients, client);

	CSMClientAutoRestart autoRestart;
	autoRestart = g_desktop_app_info_get_boolean(desktopInfo, "X-GNOME-AutoRestart") ? CSM_CLIENT_RESTART_FAIL_ONLY : CSM_CLIENT_RESTART_NEVER;
	if(g_desktop_app_info_has_key(desktopInfo, "Graphene-AutoRestart"))
	{
		autoRestart = CSM_CLIENT_RESTART_NEVER;
		gchar *autoRestartStr = g_desktop_app_info_get_string(desktopInfo, "Graphene-AutoRestart");
		if(g_strcmp0(autoRestartStr, "fail-only") == 0)
			autoRestart = CSM_CLIENT_RESTART_FAIL_ONLY;
		else if(g_strcmp0(autoRestartStr, "always") == 0)
			autoRestart = CSM_CLIENT_RESTART_ALWAYS;
	}

	gchar *delayString = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Delay");
	gint64 delay = 0;
	if(delayString)
		delay = g_ascii_strtoll(delayString, NULL, 0) * 1000; // seconds to milliseconds
	g_free(delayString);

	g_object_set(client,
		"name", g_app_info_get_display_name(G_APP_INFO(desktopInfo)),
		"args", g_app_info_get_commandline(G_APP_INFO(desktopInfo)),
		"auto-restart", autoRestart,
		"silent", SHOW_ALL_OUTPUT ? FALSE : !g_desktop_app_info_get_boolean(desktopInfo, "Graphene-ShowOutput"),
		"delay", delay,
		"condition", g_desktop_app_info_get_string(desktopInfo, "AutostartCondition"),
		NULL);

	g_object_connect(client,
		"signal::notify::ready", on_client_notify_ready, NULL,
		"signal::notify::complete", on_client_notify_complete, NULL,
		//"signal::end-session-response", on_client_end_session_response, NULL,
		NULL);

	graphene_session_client_spawn(client); // Ignored if autostart condition is false
}



/*
 * Session Inhibition
 */

static gboolean on_client_inhibit(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *appId, guint toplevelXId, const gchar *reason, guint flags, gpointer userdata)
{
	const gchar *sender = g_dbus_method_invocation_get_sender(invocation);
	GrapheneSessionClient *client = find_client_from_given_info(NULL, NULL, appId, NULL);

	// Create and register the client
	if(!client)
	{
		client = graphene_session_client_new(session->eBus, NULL);
		g_object_connect(client,
			"signal::notify::complete", on_client_notify_complete, NULL,
			NULL);
		session->clients = g_list_prepend(session->clients, client);
		graphene_session_client_register(client, sender, appId, TRUE);
		const gchar *objectPath = graphene_session_client_get_object_path(client);
		if(objectPath)
		{
			dbus_session_manager_emit_client_added(session->dbusSMSkeleton, objectPath);
			g_message("Client %s registered.", graphene_session_client_get_best_name(client));
			on_client_notify_complete(client);
		}
	}

	guint cookie = graphene_session_client_add_inhibition(client, reason, flags);
	dbus_session_manager_complete_inhibit(object, invocation, cookie);
	return TRUE;
}

static gboolean on_client_uninhibit(DBusSessionManager *object, GDBusMethodInvocation *invocation, guint cookie, gpointer userdata)
{
	// Try to remove from each client
	for(GList *it = session->clients; it != NULL; it = it->next)
		graphene_session_client_remove_inhibition(it->data, cookie);
	
	dbus_session_manager_complete_uninhibit(object, invocation);
	return TRUE;
}



/*
 * Other DBus Commands
 */

static gboolean on_dbus_set_env(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *variable, const gchar *value, gpointer userdata)
{
	// Setting environment variables isn't really useful after the startup phase
	if(session && session->phase <= SESSION_PHASE_STARTUP)
	{
		// Variable name cannot contain = according to g_setenv docs.
		if(g_strstr_len(variable, -1, "=") != NULL)
		{
			g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Variable name cannot contain =."); 
			return TRUE;
		}
		
		g_setenv(variable, value, FALSE);
	}

	dbus_session_manager_complete_setenv(object, invocation);
	return TRUE;
}

static gboolean on_dbus_get_locale(DBusSessionManager *object, GDBusMethodInvocation *invocation, gint category, gpointer userdata)
{
	// TODO
	//dbus_session_manager_complete_get_locale(object, invocation, "");
	return FALSE;
}

static gboolean on_dbus_initialization_error(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *message, gboolean fatal, gpointer userdata)
{
	if(fatal && session->phase <= SESSION_PHASE_STARTUP)
	{
		g_critical("Fatal External Initialization Error: %s", message);
		graphene_session_exit_on_idle(TRUE);
	}
	else
	{
		g_warning("External Initialization Error: %s", message);
	}

	dbus_session_manager_complete_initialization_error(object, invocation);
	return TRUE;
}

static gboolean on_dbus_client_relaunch(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *name, gpointer userdata)
{
	GrapheneSessionClient *client = find_client_from_given_info(name, name, name, name);
	if(client)
		graphene_session_client_restart(client);
	dbus_session_manager_complete_relaunch(object, invocation);
	return FALSE;
}

static gboolean on_dbus_is_inhibited(DBusSessionManager *object, GDBusMethodInvocation *invocation, gint flags, gpointer userdata)
{
	// TODO
	dbus_session_manager_complete_is_inhibited(object, invocation, 0);
	return TRUE;
}

static gboolean on_dbus_get_current_client(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	const gchar *sender = g_dbus_method_invocation_get_sender(invocation);
	GrapheneSessionClient *client = find_client_from_given_info(NULL, NULL, NULL, sender);
	if(client)
	{
		const gchar *objectPath = graphene_session_client_get_object_path(client);
		dbus_session_manager_complete_get_current_client(object, invocation, objectPath);
	}
	else
	{
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Calling process is not a client."); 
	}
	return TRUE;
}

static gboolean on_dbus_get_clients(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	guint count = 0;
	for(GList *clients=session->clients;clients!=NULL;clients=clients->next)
	{
		GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
		const gchar *path = graphene_session_client_get_object_path(client);
		if(path)
			count++;
	}
	
	gchar **arr = g_new(gchar*, count+1);
	count = 0;
	for(GList *clients=session->clients;clients!=NULL;clients=clients->next)
	{
		GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
		const gchar *path = graphene_session_client_get_object_path(client);
		if(path)
			arr[count++] = g_strdup(path);
	}
	arr[count] = NULL;
	
	dbus_session_manager_complete_get_clients(object, invocation, (const gchar * const *)arr);
	g_strfreev(arr);
	return TRUE;
}

static gboolean on_dbus_get_inhibitors(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	// TODO
	//dbus_session_manager_complete_get_inhibitors(object, invocation);
	return FALSE;
}

static gboolean on_dbus_get_is_autostart_condition_handled(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *condition, gpointer userdata)
{
	// TODO: What is the format for 'condition'?
	dbus_session_manager_complete_is_autostart_condition_handled(object, invocation, FALSE);
	return TRUE;
}

static gboolean on_dbus_shutdown(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	dbus_session_manager_complete_shutdown(object, invocation);
	do_exit(EXIT_SHUTDOWN, FALSE);
	return TRUE;
}

static gboolean on_dbus_reboot(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	dbus_session_manager_complete_reboot(object, invocation);
	do_exit(EXIT_REBOOT, FALSE);
	return TRUE;
}

static gboolean on_dbus_get_can_shutdown(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	// TODO: Return based on inhibition status
	dbus_session_manager_complete_can_shutdown(object, invocation, TRUE);
	return TRUE;
}

static gboolean on_dbus_logout(DBusSessionManager *object, GDBusMethodInvocation *invocation, gint mode, gpointer userdata)
{
	dbus_session_manager_complete_logout(object, invocation);
	graphene_session_request_logout();
	return TRUE;
}

static gboolean on_dbus_get_is_session_running(DBusSessionManager *object, GDBusMethodInvocation *invocation, gpointer userdata)
{
	gboolean running = session && session->phase == SESSION_PHASE_IDLE;
	dbus_session_manager_complete_is_session_running(object, invocation, running);
	return TRUE;
}

// At the end to avoid a huge block of function declarations
static void connect_dbus_methods()
{
	#define connect(s, f) g_signal_connect(session->dbusSMSkeleton, "handle-" s, G_CALLBACK(f), NULL)
	connect("setenv", on_dbus_set_env);
	connect("get-locale", on_dbus_get_locale);
	connect("initialization-error", on_dbus_initialization_error);
	connect("register-client", on_client_register);
	connect("unregister-client", on_client_unregister);
	connect("relaunch", on_dbus_client_relaunch);
	connect("inhibit", on_client_inhibit);
	connect("uninhibit", on_client_uninhibit);
	connect("get-current-client", on_dbus_get_current_client);
	connect("get-clients", on_dbus_get_clients);
	connect("get-inhibitors", on_dbus_get_inhibitors);
	connect("is-autostart-condition-handled", on_dbus_get_is_autostart_condition_handled);
	connect("shutdown", on_dbus_shutdown);
	connect("reboot", on_dbus_reboot);
	connect("can-shutdown", on_dbus_get_can_shutdown);
	connect("logout", on_dbus_logout);
	connect("is-session-running", on_dbus_get_is_session_running);
	#undef connect
}



/*
 * PolKit Authentication Agent
 *
 * The Authentication Agent is responsible for displaying a dialog box asking
 * for a password or some other form of authentication when an unprivileged
 * application needs to preform a privilged operation (for example, GNOME
 * Control Center modifying users in the Users panel).
 * 
 * How it works:
 * An unprivileged application sends a request to the Polkit Authority Daemon
 * via DBus, and then the Authority asks us to display a dialog. We display
 * a dialog, and using the password/response from the user, launch an
 * a helper app with the necessary privileges. This helper app then attempts
 * to send a completion request to the Authority. If it worked, the original
 * application gets permission to do its operation, and we close our dialog.
 * The whole "helper app" part of this process is done for us by the
 * polkitagent library.
 */

static void on_pk_auth_dialog_complete(GraphenePKAuthDialog *dialog, gboolean cancelled, gboolean gainedAuthentication, gpointer userdata)
{
	session->pkAuthDialogList = g_list_remove(session->pkAuthDialogList, dialog);
	
	// This closes and frees the dialog
	session->dialogCb(NULL, session->cbUserdata);
	
	g_return_if_fail(G_IS_DBUS_METHOD_INVOCATION(userdata));
	GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION(userdata);

	if(cancelled)
	{
		g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.PolicyKit1.Error.Cancelled", "Cancelled");
	}
	else
	{
		dbus_org_freedesktop_policy_kit1_authentication_agent_complete_begin_authentication(session->dbusPkAgentSkeleton, invocation);
	}

	// Show the next dialog in the queue, if any
	if(session->pkAuthDialogList != NULL)
		session->dialogCb(CLUTTER_ACTOR(session->pkAuthDialogList->data), session->cbUserdata);
}

static gboolean on_pk_agent_begin_authentication(DBusPolkitAuthAgent *object, GDBusMethodInvocation *invocation, const gchar *actionId, const gchar *message, const gchar *iconName, GVariant *details, const gchar *cookie, GVariant *identitiesV)
{
	if(!session->dialogCb)
	{
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "");
		return TRUE;
	}

	GError *error = NULL;
	GraphenePKAuthDialog *dialog = graphene_pk_auth_dialog_new(actionId, message, iconName, cookie, identitiesV, &error);
	if(!dialog)
	{
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, error->message);
		return TRUE;
	}

	g_signal_connect(dialog, "complete", G_CALLBACK(on_pk_auth_dialog_complete), invocation);

	gboolean firstDialog = session->pkAuthDialogList == NULL;
	session->pkAuthDialogList = g_list_append(session->pkAuthDialogList, dialog);

	if(firstDialog)
		session->dialogCb(CLUTTER_ACTOR(dialog), session->cbUserdata);
	return TRUE;
}

static gboolean on_pk_agent_cancel_authentication(DBusPolkitAuthAgent *object, GDBusMethodInvocation *invocation, const gchar *cookie)
{
	if(session && session->pkAuthDialogList && session->pkAuthDialogList->data)
		graphene_pk_auth_dialog_cancel(session->pkAuthDialogList->data);

	dbus_org_freedesktop_policy_kit1_authentication_agent_complete_cancel_authentication(object, invocation);
	return TRUE;
}
