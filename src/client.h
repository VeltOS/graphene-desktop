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
 * client.h/.c
 */

#ifndef __GRAPHENE_SESSION_CLIENT_H__
#define __GRAPHENE_SESSION_CLIENT_H__

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_SESSION_CLIENT  graphene_session_client_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSessionClient, graphene_session_client, GRAPHENE, SESSION_CLIENT, GObject)

typedef enum {
	CSM_CLIENT_RESTART_NEVER = 0,
	CSM_CLIENT_RESTART_FAIL_ONLY,
	CSM_CLIENT_RESTART_ALWAYS
} CSMClientAutoRestart;

GrapheneSessionClient * graphene_session_client_new(GDBusConnection *connection, const gchar *clientId);
void          graphene_session_client_lost_dbus(GrapheneSessionClient *self); // Call if the GDBusConnection given to _new has been lost/deallocated

void          graphene_session_client_spawn(GrapheneSessionClient *self);
void          graphene_session_client_term(GrapheneSessionClient *self);
void          graphene_session_client_kill(GrapheneSessionClient *self);
void          graphene_session_client_restart(GrapheneSessionClient *self);

void          graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId, gboolean implicit);
void          graphene_session_client_unregister(GrapheneSessionClient *self);

gboolean      graphene_session_client_query_end_session(GrapheneSessionClient *self, gboolean forced);
void          graphene_session_client_end_session(GrapheneSessionClient *self);

/*
 * Gets properties of the client. Some of these may not be available, in which
 * case the function returns NULL. Client not registered if ObjectPath is NULL.
 */
const gchar * graphene_session_client_get_id(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_object_path(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_app_id(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_dbus_name(GrapheneSessionClient *self);

/*
 * Finds the best name that has been associated with this client. Use this for
 * a human-readable name for this client. In order, this attempts to use:
 * .desktop name, app id, dbus name, args, id
 */
const gchar * graphene_session_client_get_best_name(GrapheneSessionClient *self);

/*
 * Client states
 * Alive: The client process is currently running.
 *
 * Ready: The client process has registered and/or successfully exit.
 *        A Ready client may not be Alive. If a client restarts, it
 *        will temporarily become not Ready until it registers or
 *        successfully exits again.
 *
 * Failed: Indicates that the client has unsuccessfully exit, and is not
 *        being restarted to attempt again. A client may change from Ready
 *        to Failed, but cannot be both. A Failed client cannot be Alive.
 *        If the client is not Complete, it may be started again when its
 *        auto-start condition is triggered (unsetting Failed).
 *
 * Complete: The client has exited, and will not, on its own, ever be alive
 *        again. This requires that its auto-start condition will never again
 *        be triggered. Once a client is Complete, it is no longer Alive and
 *        should probably be removed from any client lists. A client can only
 *        leave the Complete state if it is manually started through
 *        graphene_session_client_spawn or if its condition is changed.
 *        Clients retain their Ready or Failed state when they become Complete.
 *
 * A client may not meet any of these conditions for a small amount of time
 * while it (re)starts. A new client object is Complete by default.
 */
gboolean      graphene_session_client_get_is_alive(GrapheneSessionClient *self);
gboolean      graphene_session_client_get_is_ready(GrapheneSessionClient *self);
gboolean      graphene_session_client_get_is_failed(GrapheneSessionClient *self);
gboolean      graphene_session_client_get_is_complete(GrapheneSessionClient *self);

guint graphene_session_client_add_inhibition(GrapheneSessionClient *self, const gchar *reason, guint flags);
void graphene_session_client_remove_inhibition(GrapheneSessionClient *self, guint cookie);
gboolean graphene_session_client_is_inhibited(GrapheneSessionClient *self);

G_END_DECLS

#endif /* __GRAPHENE_SESSION_CLIENT_H__ */
