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
 */

#include "wm.h"
#include "background.h"
#include "dialog.h"
#include "window.h"
#include <libcmk/cmk.h>
#include <libcmk/cmk-icon-loader.h>
#include "csk/backlight.h"
#include <meta/meta-shadow-factory.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <meta/util.h>
#include <glib-unix.h>
#include <stdio.h>

#define WM_VERSION_STRING "1.0.0"
#define WM_PERCENT_BAR_STEPS 15
#define WM_TRANSITION_TIME 200 // Common transition time, ms
#define ACTOR CLUTTER_ACTOR // I am lazy

static const ClutterColor GrapheneColors[] = {
	{73, 86, 92, 255}, // background (panel)
	{255, 255, 255, 204}, // foreground (font)
	{255, 255, 255, 40}, // hover
	{255, 255, 255, 25}, // selected
};

static const float GrapheneBevelRadius = 3.0;
static const float GraphenePadding = 10.0;

/*
 * From what I can tell, the current version of Clutter has a memory leak
 * where the ClutterTransition object isn't freed after a transition,
 * and since it holds a reference to the actor, the actor gets an extra
 * reference.
 * I may be mistaken on this, but it seems to be so. I think the line
 * 19059 in clutter-actor.c, commit 40bbab8, is the issue.
 * This quick-fix just unrefs the ClutterTransition object after the
 * transition completes.
 * This also shouldn't cause crashes if the memleak is fixed, since
 * the signal connects after all the internal signals, and g_object_unref
 * would just throw an error message.
 * Submitted as bug 776471 on GNOME BugZilla.
 *
 * Update: Replacing a transition before it finishes causes Clutter to
 * remove the transition correctly, meaning that it causes a secondary
 * g_object_unref. I think. Either way, quickly minimizing and unminimizing
 * windows causes a bunch of g_object_unref on not-objects warnings, but
 * isn't really a problem.
 */
#define TRANSITION_MEMLEAK_FIX(actor, tname) g_signal_connect_after(clutter_actor_get_transition((actor), (tname)), "stopped", G_CALLBACK(g_object_unref), NULL)


extern void wm_request_logout(gpointer userdata);
static void on_monitors_changed(MetaScreen *screen, GrapheneWM *self);
static void update_struts(GrapheneWM *self);
static void on_window_created(GrapheneWM *self, MetaWindow *window, MetaDisplay *display);
static void reset_clutter_dpi();
static void on_global_scale_changed(CmkIconLoader *iconLoader);
static void xfixes_add_input_actor(GrapheneWM *self, ClutterActor *actor);
static void xfixes_remove_input_actor(GrapheneWM *self, ClutterActor *actor);
static void graphene_wm_begin_modal(GrapheneWM *self);
static void graphene_wm_end_modal(GrapheneWM *self);
static void on_panel_request_modal(gboolean modal, GrapheneWM *self);
static void center_actor_on_primary(GrapheneWM *self, ClutterActor *actor);

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void destroy_done(ClutterActor *actor, MetaPlugin *plugin);
static void map_done(ClutterActor *actor, MetaPlugin *plugin);

static void init_keybindings(GrapheneWM *self);

const MetaPluginInfo * graphene_wm_plugin_info(MetaPlugin *plugin)
{
	static const MetaPluginInfo info = {
		.name = "Graphene WM Manager",
		.version = WM_VERSION_STRING,
		.author = "Velt (Aidan Shafran)",
		.license = "GPLv3",
		.description = "Graphene WM+Window Manager for VeltOS"
	};
	
	return &info;
}

static gint requestedDPI = 1024 * 96;
static gfloat requestedDPIScale = 1.0;

//#include <math.h>
//static gboolean sizewarp()
//{
//	static gfloat t = 0;
//	t += 0.05;
//	requestedDPIScale = 4 * fabs(sin(t));
//	
//	CmkWidget *style = cmk_widget_get_style_default();
//	cmk_widget_style_set_scale_factor(style, requestedDPIScale);
//	//g_object_set(clutter_settings_get_default(), "font-dpi", (gint)(requestedDPI * requestedDPIScale), NULL);
//	
//	return G_SOURCE_CONTINUE;
//}

void graphene_wm_start(MetaPlugin *self_)
{
	// Some stuff for DPI scaling
	CmkWidget *style = cmk_widget_get_style_default();
	CmkIconLoader *iconLoader = cmk_icon_loader_get_default();

	requestedDPIScale = cmk_icon_loader_get_scale(iconLoader);
	cmk_widget_style_set_scale_factor(style, requestedDPIScale);
	g_object_set(clutter_settings_get_default(), "font-dpi", (gint)(requestedDPI * requestedDPIScale), NULL);
	g_signal_connect_after(clutter_get_default_backend(), "resolution-changed", G_CALLBACK(reset_clutter_dpi), NULL);
	g_signal_connect(iconLoader, "notify::scale", G_CALLBACK(on_global_scale_changed), NULL);
	
	cmk_set_grab_handler((CmkGrabHandler)on_panel_request_modal, self_);

	//g_timeout_add(50, sizewarp, NULL);	

	GrapheneWM *self = GRAPHENE_WM(self_);

	MetaScreen *screen = meta_plugin_get_screen(self_);
	self->stage = meta_get_stage_for_screen(screen);

	MetaDisplay *display = meta_screen_get_display(screen);
	g_signal_connect_swapped(display, "window-created", G_CALLBACK(on_window_created), self_);

	self->audioManager = csk_audio_device_manager_get_default();

	// Don't bother clearing the stage when we're drawing our own background
	clutter_stage_set_no_clear_hint(CLUTTER_STAGE(self->stage), TRUE);

	init_keybindings(self);
	
	// Default styling
	// TODO: Load styling from a file
	// cmk_stlye_get_default gets a new ref here, which we never release to
	// ensure all widgets get the same default style.
	cmk_widget_style_set_color(style, "background", &GrapheneColors[0]);
	cmk_widget_style_set_color(style, "foreground", &GrapheneColors[1]);
	cmk_widget_style_set_color(style, "hover", &GrapheneColors[2]);
	cmk_widget_style_set_color(style, "selected", &GrapheneColors[3]);
	cmk_widget_style_set_bevel_radius(style, GrapheneBevelRadius);
	cmk_widget_style_set_padding(style, GraphenePadding);

	// Background is always below all other actors
	ClutterActor *backgroundGroup = meta_background_group_new();
	self->backgroundGroup = META_BACKGROUND_GROUP(backgroundGroup);
	clutter_actor_set_reactive(backgroundGroup, FALSE);
	clutter_actor_insert_child_below(self->stage, backgroundGroup, NULL);
	clutter_actor_show(backgroundGroup);

	// Notifications go lowest of all widgets (but above windows)
	self->notificationBox = graphene_notification_box_new((NotificationAddedCb)xfixes_add_input_actor, self);
	clutter_actor_insert_child_above(self->stage, ACTOR(self->notificationBox), NULL);

	// Panel is 2nd lowest
	self->panel = graphene_panel_new((CPanelModalCallback)on_panel_request_modal, wm_request_logout, self);
	ClutterActor *panelBar = graphene_panel_get_input_actor(self->panel);
	xfixes_add_input_actor(self, panelBar);
	clutter_actor_insert_child_above(self->stage, ACTOR(self->panel), NULL);
	g_signal_connect_swapped(panelBar, "allocation-changed", G_CALLBACK(update_struts), self);
	g_signal_connect_swapped(screen, "workspace-switched", G_CALLBACK(update_struts), self);

	// Cover group goes over everything to "dim" the screen for dialogs
	self->coverGroup = clutter_actor_new();
	clutter_actor_set_reactive(self->coverGroup, FALSE);
	clutter_actor_insert_child_above(self->stage, self->coverGroup, NULL);

	// Only the percent bar (for volume/brightness indication) goes above
	self->percentBar = graphene_percent_floater_new();
	graphene_percent_floater_set_divisions(self->percentBar, WM_PERCENT_BAR_STEPS);
	graphene_percent_floater_set_scale(self->percentBar, 2); // TEMP
	clutter_actor_insert_child_above(self->stage, ACTOR(self->percentBar), NULL);

	// Update actors when the monitors change/resize
	g_signal_connect(screen, "monitors_changed", G_CALLBACK(on_monitors_changed), self);
	on_monitors_changed(screen, self);
	update_struts(self);
	
	// Show everything
	clutter_actor_show(self->stage);

	// Start the WM modal, and the session manager can end the modal when
	// startup completes with graphene_wm_show_dialog(wm, NULL);
	// This must happen after showing the stage
	//clutter_actor_hide(self->coverGroup);
	clutter_actor_show(self->coverGroup);
	graphene_wm_begin_modal(self);
}

static void on_monitors_changed(MetaScreen *screen, GrapheneWM *self)
{
	ClutterActor *bgGroup = ACTOR(self->backgroundGroup);
	clutter_actor_destroy_all_children(bgGroup);
	clutter_actor_destroy_all_children(self->coverGroup);
	
	ClutterColor *coverColor = clutter_color_new(0,0,0,140);

	gint numMonitors = meta_screen_get_n_monitors(screen);
	for(int i=0;i<numMonitors;++i)
	{
		clutter_actor_add_child(bgGroup, ACTOR(graphene_wm_background_new(screen, i)));
	
		MetaRectangle rect = meta_rect(0,0,0,0);
		meta_screen_get_monitor_geometry(screen, i, &rect);

		ClutterActor *cover = clutter_actor_new();
		clutter_actor_set_background_color(cover, coverColor);
		clutter_actor_set_position(cover, rect.x, rect.y);
		clutter_actor_set_size(cover, rect.width, rect.height);
		clutter_actor_add_child(self->coverGroup, cover);
	}
	
	clutter_color_free(coverColor);

	int primaryMonitor = meta_screen_get_primary_monitor(screen);
	MetaRectangle primary;
	meta_screen_get_monitor_geometry(screen, primaryMonitor, &primary);
	
	clutter_actor_set_y(ACTOR(self->percentBar), primary.y+30);
	clutter_actor_set_x(ACTOR(self->percentBar), primary.x+primary.width/2-primary.width/8);
	clutter_actor_set_width(ACTOR(self->percentBar), primary.width/4);
	clutter_actor_set_height(ACTOR(self->percentBar), 20);

	if(self->dialog)
		center_actor_on_primary(self, self->dialog);

	clutter_actor_set_position(ACTOR(self->panel), primary.x, primary.y);
	clutter_actor_set_size(ACTOR(self->panel), primary.width, primary.height);

	clutter_actor_set_position(ACTOR(self->notificationBox), primary.x, primary.y);
	clutter_actor_set_size(ACTOR(self->notificationBox), primary.width, primary.height);
}

/*
 * From all my investigation into the API and source code, there appears to
 * be no way to interrupt Clutter's auto-detection of font dpi from the
 * current system. Ideally, font dpi could be a Cmk style property. However,
 * that would be very annoying and hacky to setup without creating a custom
 * version of ClutterText (which I don't want to do).
 * This method is called at the end of the Backend's resolution-chaged
 * emission, and checks to see if the resolution that has been set is the
 * one we want. If not, change it back.
 */
static void reset_clutter_dpi()
{
	ClutterSettings *settings = clutter_settings_get_default();
	gint dpi = 0;
	g_object_get(settings, "font-dpi", &dpi, NULL);
	gint dpiReq = requestedDPI * requestedDPIScale;
	if(dpi != dpiReq)
	{
		// The setter for font-dpi scales the value by GDK_DPI_SCALE, which
		// is very annoying. So just make sure that's unset. Whatever.
		g_unsetenv("GDK_DPI_SCALE");
		g_object_set(settings, "font-dpi", dpiReq, NULL);
	}
}

static void on_global_scale_changed(CmkIconLoader *iconLoader)
{
	CmkWidget *style = cmk_widget_get_style_default();
	requestedDPIScale = cmk_icon_loader_get_scale(iconLoader);
	cmk_widget_style_set_scale_factor(style, requestedDPIScale);
	g_object_unref(style);
	g_object_set(clutter_settings_get_default(), "font-dpi", (gint)(requestedDPI * requestedDPIScale), NULL);
}



/*
 * Graphene Window (MetaWindow wrapper)
 */

static void graphene_window_show(GrapheneWindow *cwindow)
{
	g_return_if_fail(cwindow);
	g_return_if_fail(META_IS_WINDOW(cwindow->window));
	MetaWindow *window = META_WINDOW(cwindow->window);
	MetaDisplay *display = meta_window_get_display(window);
	meta_window_activate(window, meta_display_get_current_time(display));
}

static void graphene_window_minimize(GrapheneWindow *cwindow)
{
	meta_window_minimize(META_WINDOW(cwindow->window));
}

static void graphene_window_set_icon_box(GrapheneWindow *cwindow, double x, double y, double width, double height)
{
	MetaRectangle rect = {x, y, width, height};
	meta_window_set_icon_geometry(META_WINDOW(cwindow->window), &rect);
}

static void graphene_window_update(GrapheneWindow *cwindow)
{
	MetaWindow *window = META_WINDOW(cwindow->window);
	cwindow->title = meta_window_get_title(window);
	g_free(cwindow->icon);
	
	const gchar *icon = meta_window_get_wm_class(window);
	if(!icon)
		icon = meta_window_get_wm_class_instance(window);
	if(!icon)
		cwindow->icon = NULL;
	else
		cwindow->icon = g_utf8_strdown(icon, -1); // TODO: Should probably validate

	cwindow->flags = GRAPHENE_WINDOW_FLAG_NORMAL;
	gboolean minimized, attention, focused, skip;
	g_object_get(window,
		"minimized", &minimized,
		"demands-attention", &attention,
		"appears-focused", &focused,
		"skip-taskbar", &skip,
		NULL);
	if(minimized)
		cwindow->flags |= GRAPHENE_WINDOW_FLAG_MINIMIZED;
	if(attention)
		cwindow->flags |= GRAPHENE_WINDOW_FLAG_ATTENTION;
	if(focused)
		cwindow->flags |= GRAPHENE_WINDOW_FLAG_FOCUSED;
	if(skip)
		cwindow->flags |= GRAPHENE_WINDOW_FLAG_SKIP_TASKBAR;
}

static void graphene_window_changed(GrapheneWindow *cwindow)
{
	graphene_window_update(cwindow);
	graphene_panel_update_window(GRAPHENE_WM(cwindow->wm)->panel, cwindow);
}

static void graphene_window_connect(GrapheneWindow *cwindow, GrapheneWindowNotify callback)
{
	MetaWindow *window = META_WINDOW(cwindow->window);
	g_signal_connect_swapped(window, "notify::title", G_CALLBACK(callback), cwindow);
	g_signal_connect_swapped(window, "notify::minimized", G_CALLBACK(callback), cwindow);
	g_signal_connect_swapped(window, "notify::appears-focused", G_CALLBACK(callback), cwindow);
	g_signal_connect_swapped(window, "notify::demands-attention", G_CALLBACK(callback), cwindow);
	g_signal_connect_swapped(window, "notify::wm-class", G_CALLBACK(callback), cwindow);
}

static void on_window_destroyed(GrapheneWindow *cwindow, MetaWindow *window)
{
	graphene_panel_remove_window(GRAPHENE_WM(cwindow->wm)->panel, cwindow);
	g_free(cwindow->icon);
	g_free(cwindow);
}

static void on_window_created(GrapheneWM *self, MetaWindow *window, MetaDisplay *display)
{
	GrapheneWindow *cwindow = g_new0(GrapheneWindow, 1);
	cwindow->wm = self;
	cwindow->window = window;
	cwindow->show = graphene_window_show;
	cwindow->minimize = graphene_window_minimize;
	cwindow->setIconBox = graphene_window_set_icon_box;

	// This seems to be the best way to get a notification when a window is
	// destroyed. In special cases, MetaWindow objects are freed and recreated,
	// and I'm not sure if the window-created signal will be called in that
	// case. TODO: Figure out.
	g_object_weak_ref(G_OBJECT(window), (GWeakNotify)on_window_destroyed, cwindow);
	
	graphene_window_connect(cwindow, graphene_window_changed);
	graphene_window_update(cwindow);

	// Inform delegates
	graphene_panel_add_window(self->panel, cwindow);
}

static void update_struts(GrapheneWM *self)
{
	g_return_if_fail(GRAPHENE_IS_WM(self));
	ClutterActor *bar = graphene_panel_get_input_actor(self->panel);
	
	// TODO: Using the wrong side with the given struct rectangle can be
	// very bad, sometimes even causing segfaults. Maybe do some checks
	// to make sure the Panel is giving us good info on it's position.
	MetaSide side;
	switch(graphene_panel_get_side(self->panel))
	{
	case GRAPHENE_PANEL_SIDE_TOP:
		side = META_SIDE_TOP; break;
	case GRAPHENE_PANEL_SIDE_BOTTOM:
		side = META_SIDE_BOTTOM; break;
	default:
		return;
	}
	
	MetaScreen *screen = meta_plugin_get_screen(META_PLUGIN(self));
	MetaWorkspace *ws = meta_screen_get_active_workspace(screen);

	gfloat x, y, width, height;
	clutter_actor_get_position(bar, &x, &y);
	clutter_actor_get_size(bar, &width, &height);

	MetaStrut strut = {x, y, width, height, side};
	GSList *struts = NULL;
	if(width > 0 && height > 0)	
		struts = g_slist_append(NULL, &strut);
	meta_workspace_set_builtin_struts(ws, struts);
	g_slist_free(struts);
}



/*
 * Based on shell-global.c:shell_global_set_stage_input_region from gnome-shell
 *
 * I don't know all the details, but X has some issues with compositor input.
 * More specifically, without this, clicking on any 'reactive' ClutterActors
 * on the Stage, may either have no effect or cause permanent loss of mouse
 * input and requires the compositor/session to be restarted.
 *
 * Any reactive actors that need to show up above everything on-screen (ex.
 * the panel) must be added to the xInputActors array, and whenever they move
 * or resize this input region must be recalculated.
 *
 * Use the xfixes_add/remove_input_actor functions for this. They will
 * automatically handle watching for size/position changes. 
 */
static void xfixes_calculate_input_region(GrapheneWM *self)
{
	if(meta_is_wayland_compositor())
		return;

	MetaScreen *screen = meta_plugin_get_screen(META_PLUGIN(self));
	Display *xDisplay = meta_display_get_xdisplay(meta_screen_get_display(screen));

	guint numActors = g_list_length(self->xInputActors);

	if(self->modalCount > 0 || numActors == 0)
	{
		meta_empty_stage_input_region(screen);
		if(self->xInputRegion)
			XFixesDestroyRegion(xDisplay, self->xInputRegion);
		self->xInputRegion = 0;
		return;
	}

	XRectangle *rects = g_new(XRectangle, numActors);
	guint i = 0;

	for(GList *it = self->xInputActors; it != NULL; it = it->next)
	{
		if(!CLUTTER_IS_ACTOR(it->data))
			continue;
		ClutterActor *actor = ACTOR(it->data);
		if(!clutter_actor_is_mapped(actor) || !clutter_actor_get_reactive(actor))
			return;
		gfloat x, y, width, height;
		clutter_actor_get_transformed_position(actor, &x, &y);
		clutter_actor_get_transformed_size(actor, &width, &height);
		rects[i].x = (short)x;
		rects[i].y = (short)y+1; // It seems that the X region is offset by one pixel. Not sure why.
		rects[i].width = (unsigned short)width;
		rects[i].height = (unsigned short)height;
		i++;
	}

	if(self->xInputRegion)
		XFixesDestroyRegion(xDisplay, self->xInputRegion);

	self->xInputRegion = XFixesCreateRegion(xDisplay, rects, i);
	g_free(rects);
	meta_set_stage_input_region(screen, self->xInputRegion);
}

/*
 * Call this on any (reactive) actor which will show above windows.
 * This includes the Panel, modal popups, etc. You shouldn't need to manually
 * remove the actor using xfixes_remove_input_actor, as this automatically
 * watches for moving, resizing, mapping, and destroying of the actor.
 */
static void xfixes_add_input_actor(GrapheneWM *self, ClutterActor *actor)
{
	if(meta_is_wayland_compositor())
		return;
	g_return_if_fail(CLUTTER_IS_ACTOR(actor));
	self->xInputActors = g_list_prepend(self->xInputActors, actor);
	
	g_signal_connect_swapped(actor, "notify::allocation", G_CALLBACK(xfixes_calculate_input_region), self);
	g_signal_connect_swapped(actor, "notify::mapped", G_CALLBACK(xfixes_calculate_input_region), self);
	g_signal_connect_swapped(actor, "notify::reactive", G_CALLBACK(xfixes_calculate_input_region), self);
	g_signal_connect_swapped(actor, "destroy", G_CALLBACK(xfixes_remove_input_actor), self);

	xfixes_calculate_input_region(self);
}

static void xfixes_remove_input_actor(GrapheneWM *self, ClutterActor *actor)
{
	if(meta_is_wayland_compositor())
		return;
	g_return_if_fail(CLUTTER_IS_ACTOR(actor));
	gboolean changed = FALSE;
	for(GList *it = self->xInputActors; it != NULL;)
	{
		if(it->data == actor)
		{
			GList *temp = it;
			it = it->next;
			g_signal_handlers_disconnect_by_func(temp->data, xfixes_calculate_input_region, self);
			g_signal_handlers_disconnect_by_func(temp->data, xfixes_remove_input_actor, self);
			self->xInputActors = g_list_delete_link(self->xInputActors, temp);
			changed = TRUE;
		}
		else
			it = it->next;
	}

	if(changed)	
		xfixes_calculate_input_region(self);
}

static void graphene_wm_begin_modal(GrapheneWM *self)
{
	if(self->modalCount > 0)
	{
		self->modalCount ++;
		return;
	}

	// TODO: If the user is currently dragging (already in modal),
	// this doesn't work to grab their mouse.
	self->modalCount ++;
	meta_plugin_begin_modal(META_PLUGIN(self), 0, 0);
	xfixes_calculate_input_region(self);
}

static void graphene_wm_end_modal(GrapheneWM *self)
{
	self->modalCount --;
	if(self->modalCount > 0)
		return;

	self->modalCount = 0;
	meta_plugin_end_modal(META_PLUGIN(self), 0);
	xfixes_calculate_input_region(self);
}

static void on_panel_request_modal(gboolean modal, GrapheneWM *self)
{
	g_return_if_fail(GRAPHENE_IS_WM(self));
	if(modal)
		graphene_wm_begin_modal(self);
	else
		graphene_wm_end_modal(self);
}

/*
 * Modal dialog
 */

static void close_dialog_complete(GrapheneWM *self, ClutterActor *dialog)
{
	g_signal_handlers_disconnect_by_func(dialog, close_dialog_complete, self);
	clutter_actor_remove_child(self->stage, dialog);
	if(dialog == self->dialog)
	{
		self->dialog = NULL;
		clutter_actor_hide(self->coverGroup);
	}
}

static void graphene_wm_close_dialog(GrapheneWM *self, gboolean closeCover)
{
	if(self->dialog)
	{
		g_signal_connect_swapped(self->dialog, "transitions_completed", G_CALLBACK(close_dialog_complete), self);
		clutter_actor_save_easing_state(self->dialog);
		clutter_actor_set_easing_mode(self->dialog, CLUTTER_EASE_IN_BACK);
		clutter_actor_set_easing_duration(self->dialog, WM_TRANSITION_TIME);
		clutter_actor_set_scale(self->dialog, 0, 0);
		clutter_actor_restore_easing_state(self->dialog);
		clutter_actor_set_reactive(self->dialog, FALSE);
		TRANSITION_MEMLEAK_FIX(self->dialog, "scale-x");
		TRANSITION_MEMLEAK_FIX(self->dialog, "scale-y");
	}
	
	graphene_wm_end_modal(self);
	
	if(!closeCover || clutter_actor_get_opacity(self->coverGroup) == 0)
		return;

	clutter_actor_save_easing_state(self->coverGroup);
	clutter_actor_set_easing_mode(self->coverGroup, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(self->coverGroup, WM_TRANSITION_TIME);
	clutter_actor_set_opacity(self->coverGroup, 0);
	clutter_actor_restore_easing_state(self->coverGroup);
	TRANSITION_MEMLEAK_FIX(self->coverGroup, "opacity");
}

static void on_dialog_size_changed(ClutterActor *dialog, GParamSpec *param, GrapheneWM *self)
{
	center_actor_on_primary(self, dialog);
}

void graphene_wm_show_dialog(GrapheneWM *self, ClutterActor *dialog)
{
	if(!dialog || (dialog && self->dialog))
		graphene_wm_close_dialog(self, !dialog);
	
	if(!dialog)
		return;

	CmkShadow *shadow = cmk_shadow_new_full(CMK_SHADOW_MASK_ALL, 40);
	clutter_actor_add_child(CLUTTER_ACTOR(shadow), dialog);

	self->dialog = CLUTTER_ACTOR(shadow);
	clutter_actor_insert_child_above(self->stage, self->dialog, NULL);
	clutter_actor_show(self->dialog);
	clutter_actor_set_pivot_point(self->dialog, 0.5, 0.5);
	clutter_actor_set_scale(self->dialog, 0, 0);
	g_signal_connect(self->dialog, "notify::size", G_CALLBACK(on_dialog_size_changed), self);
	center_actor_on_primary(self, self->dialog);

	clutter_actor_save_easing_state(self->dialog);
	clutter_actor_set_easing_mode(self->dialog, CLUTTER_EASE_OUT_BACK);
	clutter_actor_set_easing_duration(self->dialog, WM_TRANSITION_TIME);
	clutter_actor_set_scale(self->dialog, 1, 1);
	clutter_actor_restore_easing_state(self->dialog);
	clutter_actor_set_reactive(self->dialog, TRUE);
	TRANSITION_MEMLEAK_FIX(self->dialog, "scale-x");
	TRANSITION_MEMLEAK_FIX(self->dialog, "scale-y");

	clutter_actor_show(self->coverGroup);
	clutter_actor_save_easing_state(self->coverGroup);
	clutter_actor_set_easing_mode(self->coverGroup, CLUTTER_EASE_OUT_SINE);
	clutter_actor_set_easing_duration(self->coverGroup, WM_TRANSITION_TIME);
	clutter_actor_set_opacity(self->coverGroup, 255);
	clutter_actor_restore_easing_state(self->coverGroup);
	TRANSITION_MEMLEAK_FIX(self->coverGroup, "opacity");
	graphene_wm_begin_modal(self);
}


static void center_actor_on_primary(GrapheneWM *self, ClutterActor *actor)
{
	MetaScreen *screen = meta_plugin_get_screen(META_PLUGIN(self));
	int primaryMonitor = meta_screen_get_primary_monitor(screen);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_screen_get_monitor_geometry(screen, primaryMonitor, &rect);
	
	gfloat width, height;
	clutter_actor_get_size(actor, &width, &height);
	
	clutter_actor_set_position(actor,
		rect.x + rect.width/2.0 - width/2.0,
		rect.y + rect.height/2.0 - height/2.0);
}



void graphene_wm_minimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = ACTOR(windowActor);
	if(g_object_get_data(G_OBJECT(actor), "unminimizing") == (gpointer)TRUE)
		unminimize_done(actor, plugin);
	g_object_set_data(G_OBJECT(actor), "minimizing", (gpointer)TRUE);
	
	// Get the minimized position
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_window_get_icon_geometry(window, &rect); // This is set by the Launcher applet
	
	// Ease the window into its minimized position
	clutter_actor_remove_all_transitions(actor);
	clutter_actor_set_pivot_point(actor, 0, 0);
	clutter_actor_save_easing_state(actor);
	clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(minimize_done), plugin);
	clutter_actor_set_x(actor, rect.x);
	clutter_actor_set_y(actor, rect.y);
	clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
	TRANSITION_MEMLEAK_FIX(actor, "x");
	TRANSITION_MEMLEAK_FIX(actor, "y");
	TRANSITION_MEMLEAK_FIX(actor, "scale-x");
	TRANSITION_MEMLEAK_FIX(actor, "scale-y");
	clutter_actor_restore_easing_state(actor);
}

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	// End transition
	g_signal_handlers_disconnect_by_func(actor, minimize_done, plugin);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_hide(actor); // Actually hide the window
	
	// Must call to complete the minimization
	meta_plugin_minimize_completed(plugin, META_WINDOW_ACTOR(actor));
	g_object_set_data(G_OBJECT(actor), "minimizing", (gpointer)FALSE);
}

void graphene_wm_unminimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = ACTOR(windowActor);
	if(g_object_get_data(G_OBJECT(actor), "minimizing") == (gpointer)TRUE)
		minimize_done(actor, plugin);
	g_object_set_data(G_OBJECT(actor), "unminimizing", (gpointer)TRUE);

	// Get the unminimized position
	gint x = clutter_actor_get_x(actor);
	gint y = clutter_actor_get_y(actor);
	
	// Move the window to it's minimized position and scale
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_window_get_icon_geometry(window, &rect);
	clutter_actor_set_x(actor, rect.x);
	clutter_actor_set_y(actor, rect.y);
	clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
	clutter_actor_show(actor);
	
	// Ease it into its unminimized position
	clutter_actor_remove_all_transitions(actor);
	clutter_actor_set_pivot_point(actor, 0, 0);
	clutter_actor_save_easing_state(actor);
	clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_SINE);
	clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(unminimize_done), plugin);
	clutter_actor_set_x(actor, x);
	clutter_actor_set_y(actor, y);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_restore_easing_state(actor);
	TRANSITION_MEMLEAK_FIX(actor, "x");
	TRANSITION_MEMLEAK_FIX(actor, "y");
	TRANSITION_MEMLEAK_FIX(actor, "scale-x");
	TRANSITION_MEMLEAK_FIX(actor, "scale-y");
}

static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func(actor, unminimize_done, plugin);
	meta_plugin_unminimize_completed(plugin, META_WINDOW_ACTOR(actor));
	g_object_set_data(G_OBJECT(actor), "unminimizing", (gpointer)FALSE);
}

void graphene_wm_destroy(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = ACTOR(windowActor);

	clutter_actor_remove_all_transitions(actor);
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

	switch(meta_window_get_window_type(window))
	{
	case META_WINDOW_NORMAL:
	case META_WINDOW_NOTIFICATION:
	case META_WINDOW_DIALOG:
	case META_WINDOW_MODAL_DIALOG:
		clutter_actor_set_pivot_point(actor, 0.5, 0.5);
		clutter_actor_save_easing_state(actor);
		clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_SINE);
		clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
		clutter_actor_set_scale(actor, 0, 0);
		clutter_actor_restore_easing_state(actor);
		TRANSITION_MEMLEAK_FIX(actor, "scale-x");
		TRANSITION_MEMLEAK_FIX(actor, "scale-y");
		break;
		
	case META_WINDOW_MENU:
	case META_WINDOW_DOCK:
	default:
		meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
	}
}

static void destroy_done(ClutterActor *actor, MetaPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func(actor, destroy_done, plugin);
	meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
}

void graphene_wm_map(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = ACTOR(windowActor);

	clutter_actor_remove_all_transitions(actor);
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

	switch(meta_window_get_window_type(window))
	{
	case META_WINDOW_NORMAL:
	case META_WINDOW_NOTIFICATION:
	case META_WINDOW_DIALOG:
	case META_WINDOW_MODAL_DIALOG:
		clutter_actor_set_pivot_point(actor, 0.5, 0.5);
		clutter_actor_set_scale(actor, 0, 0);
		clutter_actor_show(actor);
		clutter_actor_save_easing_state(actor);
		clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_SINE);
		clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(map_done), plugin);
		clutter_actor_set_scale(actor, 1, 1);
		clutter_actor_restore_easing_state(actor);
		TRANSITION_MEMLEAK_FIX(actor, "scale-x");
		TRANSITION_MEMLEAK_FIX(actor, "scale-y");
		break;
		
	case META_WINDOW_MENU:
	case META_WINDOW_DOCK:
	default:
		meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
	}
	
	if(g_strcmp0(meta_window_get_role(window), "GrapheneDock") == 0 || g_strcmp0(meta_window_get_role(window), "GraphenePopup") == 0)
	{
		g_object_set(windowActor, "shadow-mode", META_SHADOW_MODE_FORCED_ON, "shadow-class", "dock", NULL);
	}
}

static void map_done(ClutterActor *actor, MetaPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func(actor, map_done, plugin);
	meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
}



/*
 * Keybindings
 */

static void on_key_volume_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	CskAudioDevice *device = csk_audio_device_manager_get_default_output(self->audioManager);

	if(!device)
	{
		graphene_percent_floater_set_percent(self->percentBar, 0);
		return;
	}

	csk_audio_device_set_muted(device, FALSE);
	
	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	if(clutter_event_has_shift_modifier((ClutterEvent *)event))
		stepSize /= 2;
	
	float vol = csk_audio_device_get_volume(device) + stepSize;
	vol = (vol > 1) ? 1 : vol;
	graphene_percent_floater_set_percent(self->percentBar, vol);
	csk_audio_device_set_volume(device, vol);
}

static void on_key_volume_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	CskAudioDevice *device = csk_audio_device_manager_get_default_output(self->audioManager);

	if(!device)
	{
		graphene_percent_floater_set_percent(self->percentBar, 0);
		return;
	}

	csk_audio_device_set_muted(device, FALSE);

	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	if(clutter_event_has_shift_modifier((ClutterEvent *)event))
		stepSize /= 2;
	
	float vol = csk_audio_device_get_volume(device) - stepSize;
	vol = (vol < 0) ? 0 : vol;
	graphene_percent_floater_set_percent(self->percentBar, vol);
	csk_audio_device_set_volume(device, vol);
}

static void on_key_volume_mute(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	CskAudioDevice *device = csk_audio_device_manager_get_default_output(self->audioManager);

	if(!device)
	{
		graphene_percent_floater_set_percent(self->percentBar, 0);
		return;
	}

	gboolean newMute = !csk_audio_device_get_muted(device);
	graphene_percent_floater_set_percent(self->percentBar, newMute ? 0 : csk_audio_device_get_volume(device));
	csk_audio_device_set_muted(device, newMute);
}

static void on_key_backlight_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	gfloat val = csk_backlight_set_brightness(1.0/WM_PERCENT_BAR_STEPS, TRUE);
	if(val < 0)
		val = 1;
	graphene_percent_floater_set_percent(self->percentBar, val);
}

static void on_key_backlight_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	gfloat val = csk_backlight_set_brightness(-1.0/WM_PERCENT_BAR_STEPS, TRUE);
	if(val < 0)
		val = 1;
	graphene_percent_floater_set_percent(self->percentBar, val);
}

static void on_key_kb_backlight_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
}

static void on_key_kb_backlight_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	graphene_session_exit(TRUE);
}

static void on_panel_main_menu(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	graphene_panel_show_main_menu(self->panel);
}

static void init_keybindings(GrapheneWM *self)
{
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "graphene-window-manager");
	//// pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, g_application_get_application_id(g_application_get_default()));
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "multimedia-volume-control-symbolic");
	//pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, WM_VERSION_STRING);

	GSettings *keybindings = g_settings_new("io.velt.desktop.keybindings");
	MetaDisplay *display = meta_screen_get_display(meta_plugin_get_screen(META_PLUGIN(self)));
	
	#define bind(key, func) meta_display_add_keybinding(display, key, keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)func, self, NULL);
	bind("volume-up", on_key_volume_up);
	bind("volume-down", on_key_volume_down);
	bind("volume-up-half", on_key_volume_up);
	bind("volume-down-half", on_key_volume_down);
	bind("volume-mute", on_key_volume_mute);
	bind("backlight-up", on_key_backlight_up);
	bind("backlight-down", on_key_backlight_down);
	bind("kb-backlight-up", on_key_kb_backlight_up);
	bind("kb-backlight-down", on_key_kb_backlight_down);
	#undef bind

	g_object_unref(keybindings);

	meta_keybindings_set_custom_handler("panel-main-menu", (MetaKeyHandlerFunc)on_panel_main_menu, self, NULL);
	meta_keybindings_set_custom_handler("panel-run-dialog", (MetaKeyHandlerFunc)on_panel_main_menu, self, NULL);
	// meta_keybindings_set_custom_handler("switch-windows", switch_windows);
	// meta_keybindings_set_custom_handler("switch-applications", switch_windows);
}
