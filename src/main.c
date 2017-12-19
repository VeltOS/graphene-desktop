/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This program functions as both the session manager and window manager.
 * Partially because I see "session management" and "window management" as
 * very similar jobs, partially because it makes session-related graphics
 * easier to display (ex. logout dialog).
 *
 * Code dealing with Mutter must be GPL'd, but session management code
 * is under the Apache License and in its own file.
 */

#include <meta/main.h>
#include <meta/meta-plugin.h>
#include <glib-unix.h>
#include "session.h"
#include "wm.h"
#include <stdio.h>

#ifndef GRAPHENE_VERSION_STR
#define GRAPHENE_VERSION_STR ""
#endif

#ifndef GRAPHENE_DEBUG
#define GRAPHENE_DEBUG FALSE
#endif

G_DEFINE_TYPE(GrapheneWM, graphene_wm, META_TYPE_PLUGIN);
static gboolean on_exit_signal(gpointer userdata);
static void on_session_startup_complete(gpointer userdata);
static void on_show_dialog(ClutterActor *dialog, gpointer userdata);
static void on_session_quit(gboolean failed, gpointer userdata);

int main(int argc, char **argv)
{
#if GRAPHENE_DEBUG
	g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
	const gchar *home = g_getenv("HOME");
	gchar *out = g_strdup_printf("%s/graphene.log", home);
	int f = open(out, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
	g_free(out);
	dup2(f, 1); // stdout -> f
	dup2(f, 2); // stderr -> f
	setvbuf(stdout, NULL, _IOLBF, 0); // Set stdout/err to line-buffered. This
	setvbuf(stderr, NULL, _IOLBF, 0); // affects libc calls (printf,fwrite,etc).
#endif
	
	fprintf(stderr, "\n"); // g_message uses stderr by default
	g_message("Graphene Version %s%s", GRAPHENE_VERSION_STR, GRAPHENE_DEBUG ? "d" : "");
	
#if GRAPHENE_DEBUG
	// Print date and time
	GDateTime *dt = g_date_time_new_now_local();
	gchar *fdt = g_date_time_format(dt, "%G-%m-%e T%H:%M:%S Z%z");
	g_message("%s\n", fdt);
	g_free(fdt);
	g_date_time_unref(dt);
	
	// Make sure initial log messages get written
	fflush(stderr); 
	fsync(fileno(stderr));
#endif
	
	meta_plugin_manager_set_plugin_type(GRAPHENE_TYPE_WM);
	meta_set_wm_name("GRAPHENE Desktop");
	meta_set_gnome_wm_keybindings("Mutter,GNOME Shell");
	
	GError *error = NULL;
	GOptionContext *opt = meta_get_option_context();
	if(!g_option_context_parse(opt, &argc, &argv, &error))
	{
		g_critical("Bad arguments to graphene-wm: %s", error->message);
		return 1;
	}
	g_option_context_free(opt);
	
	g_setenv("NO_GAIL", "1", TRUE);
	g_setenv("NO_AT_BRIDGE", "1", TRUE);
	meta_init();
	g_unsetenv("NO_AT_BRIDGE");
	g_unsetenv("NO_GAIL");
	
	return meta_run();
}

static void graphene_wm_class_init(GrapheneWMClass *class)
{
	MetaPluginClass *pluginClass = META_PLUGIN_CLASS(class);
	pluginClass->plugin_info = graphene_wm_plugin_info;
	pluginClass->start = graphene_wm_start;
	pluginClass->minimize = graphene_wm_minimize;
	pluginClass->unminimize = graphene_wm_unminimize;
	pluginClass->map = graphene_wm_map;
	pluginClass->destroy = graphene_wm_destroy;
	// The plugin class is never properly destructed, and it exists for the
	// entire duration of the program. So don't attach dispose/finalize.
}

static void graphene_wm_init(GrapheneWM *wm)
{
	graphene_session_init(on_session_startup_complete, on_show_dialog, on_session_quit, wm);

	g_unix_signal_add(SIGTERM, (GSourceFunc)on_exit_signal, NULL);
	g_unix_signal_add(SIGINT, (GSourceFunc)on_exit_signal, NULL);
	g_unix_signal_add(SIGHUP, (GSourceFunc)on_exit_signal, NULL);
}

static gboolean on_exit_signal(UNUSED gpointer userdata)
{
	g_warning("SIGTERM/INT/HUP. Aborting.");
	graphene_session_exit(TRUE);
	return G_SOURCE_CONTINUE;
}

static void on_session_startup_complete(gpointer userdata)
{
	g_message("SM startup complete.");
	// Hide the startup "cover" dialog
	graphene_wm_show_dialog(GRAPHENE_WM(userdata), NULL);
}

static void on_show_dialog(ClutterActor *dialog, gpointer userdata)
{
	graphene_wm_show_dialog(GRAPHENE_WM(userdata), dialog);
}

static void on_session_quit(gboolean failed, UNUSED gpointer userdata)
{
	g_message("SM has completed %s. Exiting mutter.", failed ? "with an error" : "successfully");
	meta_quit(failed ? META_EXIT_ERROR : META_EXIT_SUCCESS);
}

// Called directly from wm.c (extern)
void wm_request_logout(UNUSED gpointer userdata)
{
	graphene_session_request_logout();
}
