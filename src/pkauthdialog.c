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
 */

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#define THIS_ERROR_QUARK g_quark_from_static_string("GRAPHENE_PKAUTHDIALOG_ERROR")

#include "pkauthdialog.h"
#include <cmk/cmk.h>
#include <polkitagent/polkitagent.h>

typedef enum
{
	PK_STATE_NONE, // The agent session has not been started
	PK_STATE_IDENTITY, // The user has selected an identity
	PK_STATE_WAITING, // The agent session has made a request to the user
	PK_STATE_AUTHENTICATING, // The user has responded to the request
	PK_STATE_CANCELLED // The agent session has been cancelled
} PkState;

struct _GraphenePKAuthDialog
{
	GrapheneDialog parent;
	gchar *actionId;
	gchar *message;
	gchar *iconName;
	gchar *cookie;
	GList *identities;
	PolkitAgentSession *agentSession;
	CmkTextfield *responseField;
	PkState state;
};

enum
{
	SIGNAL_COMPLETE = 1,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static void graphene_pk_auth_dialog_dispose(GObject *self_);
static void on_option_selected(GrapheneDialog *self_, const gchar *selection);
static void on_select_identity(GraphenePKAuthDialog *self, gpointer userdata);
static gboolean on_activate(GraphenePKAuthDialog *self, ClutterButtonEvent *event, gpointer userdata);
static void on_auth_agent_completed(PolkitAgentSession *agentSession, gboolean gainedAuthorization, gpointer userdata);
static void on_auth_agent_request(PolkitAgentSession *agentSession, gchar *request, gboolean echoOn, gpointer userdata);
static void on_auth_agent_show_error(PolkitAgentSession *agentSession, gchar *text, gpointer userdata);
static void on_auth_agent_show_info(PolkitAgentSession *agentSession, gchar *text, gpointer userdata);
static GList * get_pkidentities_from_variant(GVariant *identitiesV, GError **error);
static void grab_focus_on_map(ClutterActor *actor);

G_DEFINE_TYPE(GraphenePKAuthDialog, graphene_pk_auth_dialog, GRAPHENE_TYPE_DIALOG);



GraphenePKAuthDialog * graphene_pk_auth_dialog_new(const gchar *actionId, const gchar *message, const gchar *iconName, const gchar *cookie, GVariant *identitiesV, GError **error)
{
	// The Polkit Authority sends a list of identities that are capable of
	// authorizing this particular action. These can either be users or
	// user groups (although there is room for new identity types).
	GList *identities = get_pkidentities_from_variant(identitiesV, error);
	if(!identities)
		return NULL;

	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(g_object_new(GRAPHENE_TYPE_PK_AUTH_DIALOG, NULL));
	if(!GRAPHENE_IS_PK_AUTH_DIALOG(self))
	{
		g_set_error_literal(error, THIS_ERROR_QUARK, 2, "Failed to create GObject");
		return NULL;
	}

	self->actionId = g_strdup(actionId);
	self->message = g_strdup(message);
	self->cookie = g_strdup(cookie);
	self->identities = identities;

	graphene_dialog_set_message(GRAPHENE_DIALOG(self), self->message);

	if(!iconName || g_strcmp0(iconName, "") == 0)
		graphene_dialog_set_icon(GRAPHENE_DIALOG(self), "locked");
	else
		graphene_dialog_set_icon(GRAPHENE_DIALOG(self), self->iconName);

	CmkTextfield *passwordBox = cmk_textfield_new("Password", NULL);
	cmk_textfield_set_is_password(passwordBox, TRUE);
	cmk_widget_set_margin(CMK_WIDGET(passwordBox), 15, 30, 0, 0);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(passwordBox), TRUE);
	clutter_actor_set_y_align(CLUTTER_ACTOR(passwordBox), CLUTTER_ACTOR_ALIGN_CENTER);
	self->responseField = passwordBox;
	graphene_dialog_set_content(GRAPHENE_DIALOG(self), CLUTTER_ACTOR(passwordBox));

	g_signal_connect_swapped(passwordBox, "activate-enter", G_CALLBACK(on_activate), self);
	g_signal_connect(passwordBox, "notify::mapped", G_CALLBACK(grab_focus_on_map), NULL);

	on_select_identity(self, NULL); // TEMP
	return self;
}

static void graphene_pk_auth_dialog_class_init(GraphenePKAuthDialogClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_pk_auth_dialog_dispose;
	GRAPHENE_DIALOG_CLASS(class)->select = on_option_selected;
	
	/*
	 * Emitted when authentication has been completed or cancelled.
	 * You should close this GraphenePKAuthDialog instance on this signal.
	 * The first parameter is TRUE if the dialog was cancelled.
	 * The second parameter is TRUE if authentication was successful.
	 */
	signals[SIGNAL_COMPLETE] = g_signal_new("complete", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
}

static void graphene_pk_auth_dialog_init(GraphenePKAuthDialog *self)
{
	self->state = PK_STATE_NONE;
	const gchar * const buttons[] = {"Cancel", "Authenticate", NULL};
	graphene_dialog_set_buttons(GRAPHENE_DIALOG(self), buttons);
}

static void graphene_pk_auth_dialog_dispose(GObject *self_)
{
	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(self_);
	
	g_clear_object(&self->agentSession);
	g_clear_pointer(&self->actionId, g_free);
	g_clear_pointer(&self->message, g_free);
	g_clear_pointer(&self->iconName, g_free);
	g_clear_pointer(&self->cookie, g_free);
	g_list_free_full(self->identities, (GDestroyNotify)g_object_unref);
	self->identities = NULL;

	G_OBJECT_CLASS(graphene_pk_auth_dialog_parent_class)->dispose(G_OBJECT(self));
}

static void grab_focus_on_map(ClutterActor *actor)
{
	if(clutter_actor_is_mapped(actor))
		clutter_actor_grab_key_focus(actor);
}

static void pk_cancel(GraphenePKAuthDialog *self)
{
	if(!self->agentSession || self->state == PK_STATE_NONE || self->state == PK_STATE_CANCELLED)
		return;
	
	self->state = PK_STATE_CANCELLED;
	polkit_agent_session_cancel(self->agentSession);
}

static void pk_respond(GraphenePKAuthDialog *self, const gchar *response)
{
	if(!self->agentSession || self->state != PK_STATE_WAITING)
		return;

	self->state = PK_STATE_AUTHENTICATING;
	polkit_agent_session_response(self->agentSession, response);
}

void graphene_pk_auth_dialog_cancel(GraphenePKAuthDialog *self)
{
	g_return_if_fail(GRAPHENE_IS_PK_AUTH_DIALOG(self));
	pk_cancel(self);
}

static void on_option_selected(GrapheneDialog *self_, const gchar *selection)
{
	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(self_);

	if(g_strcmp0(selection, "Authenticate") == 0)
		pk_respond(self, cmk_textfield_get_text(self->responseField));
	else
		pk_cancel(self);
}

static gboolean on_activate(GraphenePKAuthDialog *self, ClutterButtonEvent *event, gpointer userdata)
{
	pk_respond(self, cmk_textfield_get_text(self->responseField));
}

static void on_select_identity(GraphenePKAuthDialog *self, gpointer userdata)
{
	pk_cancel(self);
	// TODO: Get selected identity
	PolkitIdentity *identity = POLKIT_IDENTITY(self->identities->data);
	if(!identity)
		return;
	
	self->agentSession = polkit_agent_session_new(identity, self->cookie);
	
	#define connect(signal, func) g_signal_connect(self->agentSession, signal, G_CALLBACK(func), self)
	connect("completed", on_auth_agent_completed);
	connect("request", on_auth_agent_request);
	connect("show-error", on_auth_agent_show_error);
	connect("show-info", on_auth_agent_show_info);
	self->state = PK_STATE_IDENTITY;
	polkit_agent_session_initiate(self->agentSession);
	#undef connect
}

static void on_auth_agent_completed(PolkitAgentSession *agentSession, gboolean gainedAuthorization, gpointer userdata)
{
	g_return_if_fail(GRAPHENE_IS_PK_AUTH_DIALOG(userdata));
	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(userdata);
	gboolean cancelled = (self->state == PK_STATE_CANCELLED);
	self->state = PK_STATE_NONE;
	g_clear_object(&self->agentSession);
	g_signal_emit(self, signals[SIGNAL_COMPLETE], 0, cancelled, gainedAuthorization);
	// TODO: Try multiple times?
}

static void on_auth_agent_request(PolkitAgentSession *agentSession, gchar *request, gboolean echoOn, gpointer userdata)
{
	g_message("Request: %s (echo: %i)", request, echoOn);
	GRAPHENE_PK_AUTH_DIALOG(userdata)->state = PK_STATE_WAITING;
}

static void on_auth_agent_show_error(PolkitAgentSession *agentSession, gchar *text, gpointer userdata)
{
	g_warning("Authentication error: %s", text);
}

static void on_auth_agent_show_info(PolkitAgentSession *agentSession, gchar *text, gpointer userdata)
{
	g_warning("Authentication info: %s", text);
}

static GList * get_pkidentities_from_variant(GVariant *identitiesV, GError **error)
{	
	if(!g_variant_check_format_string(identitiesV, "a(sa{sv})", FALSE))
	{
		g_set_error(error, THIS_ERROR_QUARK, 3, "Invalid format string on 'identitiesV', should be 'a(sa{sv})' but found '%s;.", g_variant_get_type_string(identitiesV));
		return NULL;
	}

	GList *identities = NULL;

	gchar *kind;
	GVariantIter *propIter;
	GVariantIter iter;
	g_variant_iter_init(&iter, identitiesV);
	while(g_variant_iter_loop(&iter, "(sa{sv})", &kind, &propIter))
	{
		gchar *key;
		GVariant *val;
		while(g_variant_iter_loop(propIter, "{sv}", &key, &val)) 
		{
			PolkitIdentity *identity;
			if(g_strcmp0(kind, "unix-user") == 0
			&& g_strcmp0(key, "uid") == 0
			&& g_variant_check_format_string(val, "u", FALSE))
			{
				identity = polkit_unix_user_new(g_variant_get_uint32(val));
				gchar *dispname = g_strdup(polkit_unix_user_get_name(POLKIT_UNIX_USER(identity)));
				g_object_set_data_full(G_OBJECT(identity), "dispname", dispname, g_free);
			}
			else if(g_strcmp0(kind, "unix-group") == 0
			&& g_strcmp0(key, "gid") == 0
			&& g_variant_check_format_string(val, "u", FALSE))
			{
				guint32 gid = g_variant_get_uint32(val);
				identity = polkit_unix_group_new(gid);
				gchar *dispname = g_strdup_printf("Unix Group %u", gid);
				g_object_set_data_full(G_OBJECT(identity), "dispname", dispname, g_free);
			}
			else
			{
				g_variant_unref(val);
				g_free(key);
				g_variant_iter_free(propIter);
				g_free(kind);
				g_list_free_full(identities, (GDestroyNotify)g_object_unref);
				g_set_error(error, THIS_ERROR_QUARK, 1, "Invalid/unsupported user identity key: %s, %s", kind, key);
				return NULL;
			}

			identities = g_list_prepend(identities, identity);
		}
	}
	
	return identities;
}
