//*
// * This file is part of graphene-desktop, the desktop environment of VeltOS.
// * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
// * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
// */
 
#define GMENU_I_KNOW_THIS_IS_UNSTABLE // TODO: Maybe find an alternative? 

#include "panel-internal.h"
#include <libcmk/cmk-label.h>
#include <libcmk/button.h>
#include <libcmk/cmk-icon.h>
#include <libcmk/shadow.h>
#include <libcmk/cmk-scroll-box.h>
#include <gdk/gdkx.h>
#include <gmenu-tree.h>
#include <gio/gdesktopappinfo.h>

#define LAUNCHER_WIDTH 300

struct _GrapheneLauncherPopup
{
	CmkWidget parent;
	
	CmkShadow *sdc;
	CmkWidget *window;
	CmkScrollBox *scroll;
	CmkButton *firstApp;
	gdouble scrollAmount;
	
	ClutterText *searchBox;
	CmkIcon *searchIcon;
	ClutterActor *searchSeparator;
	gchar *filter;
	
	GMenuTree *appTree;
};


G_DEFINE_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, CMK_TYPE_WIDGET)


static void graphene_launcher_popup_dispose(GObject *self_);
static void graphene_launcher_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_style_changed(CmkWidget *self_);
static void on_background_changed(CmkWidget *self_);
static void on_search_box_text_changed(GrapheneLauncherPopup *self, ClutterText *searchBox);
static void on_search_box_activate(GrapheneLauncherPopup *self, ClutterText *searchBox);
static ClutterActor * separator_new();
static void popup_applist_refresh(GrapheneLauncherPopup *self);
static void popup_applist_populate(GrapheneLauncherPopup *self);
static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory);
static void applist_on_item_clicked(GrapheneLauncherPopup *self, CmkButton *button);
static gboolean on_key_pressed(ClutterActor *self, ClutterKeyEvent *event);

//static void applist_launch_first(GrapheneLauncherPopup *self);

GrapheneLauncherPopup* graphene_launcher_popup_new(void)
{
	return GRAPHENE_LAUNCHER_POPUP(g_object_new(GRAPHENE_TYPE_LAUNCHER_POPUP, NULL));
}

static void graphene_launcher_popup_class_init(GrapheneLauncherPopupClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_launcher_popup_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_launcher_popup_allocate;
	CLUTTER_ACTOR_CLASS(class)->key_press_event = on_key_pressed;
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;
	CMK_WIDGET_CLASS(class)->background_changed = on_background_changed;
}

static void graphene_launcher_popup_init(GrapheneLauncherPopup *self)
{
	self->sdc = cmk_shadow_new_full(CMK_SHADOW_MASK_RIGHT | CMK_SHADOW_MASK_BOTTOM, 40);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->sdc));

	self->window = cmk_widget_new();
	cmk_widget_set_draw_background_color(self->window, TRUE);
	cmk_widget_set_background_color_name(self->window, "background");
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->window));

	// TODO: Search bar not tabbable because not a CmkWidget
	self->searchBox = CLUTTER_TEXT(clutter_text_new());
	clutter_text_set_editable(self->searchBox, TRUE);
	clutter_text_set_activatable(self->searchBox, TRUE);
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->searchBox), TRUE);
	g_signal_connect_swapped(self->searchBox, "text-changed", G_CALLBACK(on_search_box_text_changed), self);
	g_signal_connect_swapped(self->searchBox, "activate", G_CALLBACK(on_search_box_activate), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->searchBox));
	//cmk_redirect_keyboard_focus(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->searchBox));

	self->searchSeparator = separator_new();
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->searchSeparator);

	// Despite the scroll box looking like its inside the popup window, it
	// isn't actually a child of the window actor; it is a child of self.
	// This makes allocation/sizing easer, and helps keep the scroll window
	// from expanding too far.
	self->scroll = cmk_scroll_box_new(CLUTTER_SCROLL_VERTICALLY);
	ClutterBoxLayout *listLayout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(listLayout, CLUTTER_ORIENTATION_VERTICAL); 
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->scroll), CLUTTER_LAYOUT_MANAGER(listLayout));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->scroll));

	self->searchIcon = cmk_icon_new_full("gnome-searchtool", NULL, 16, TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(self->searchIcon), CLUTTER_ACTOR_ALIGN_CENTER);
	clutter_actor_set_y_align(CLUTTER_ACTOR(self->searchIcon), CLUTTER_ACTOR_ALIGN_CENTER);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->searchIcon));

	PangoFontDescription *desc = pango_font_description_new();
	pango_font_description_set_size(desc, 16*PANGO_SCALE); // 16pt
	clutter_text_set_font_description(self->searchBox, desc);
	pango_font_description_free(desc);

	// Load applications
	self->appTree = gmenu_tree_new("gnome-applications.menu", GMENU_TREE_FLAGS_SORT_DISPLAY_NAME);
	popup_applist_refresh(self);
}

static void graphene_launcher_popup_dispose(GObject *self_)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);
	g_clear_object(&self->appTree);
	g_clear_pointer(&self->filter, g_free);
	G_OBJECT_CLASS(graphene_launcher_popup_parent_class)->dispose(self_);
}

static void graphene_launcher_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);
	
	gfloat width = LAUNCHER_WIDTH * cmk_widget_style_get_scale_factor(CMK_WIDGET(self_));
	ClutterActorBox windowBox = {box->x1, box->y1, MIN(box->x1 + width, box->x2/2), box->y2};
	ClutterActorBox sdcBox = {box->x1-40, box->y1-40, windowBox.x2 + 40, box->y2 + 40};

	// I'm so sorry for how ugly this icon/searchbar allocation is.
	// Eventually I'll move the search icon and the input box into its
	// own CMK class.
	gfloat searchMin, searchNat, sepMin, sepNat, iconMinW, iconNatW;
	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->searchBox), width, &searchMin, &searchNat);
	clutter_actor_get_preferred_width(CLUTTER_ACTOR(self->searchIcon), searchNat, &iconMinW, &iconNatW);
	clutter_actor_get_preferred_height(CLUTTER_ACTOR(self->searchSeparator), width, &sepMin, &sepNat);
	
	ClutterActorBox iconBox = {windowBox.x1, windowBox.y1, windowBox.x1 + iconNatW, windowBox.y1 + searchNat};
	ClutterActorBox searchBox = {iconBox.x2, windowBox.y1, windowBox.x2, windowBox.y1 + searchNat};
	ClutterActorBox separatorBox = {windowBox.x1, searchBox.y2, windowBox.x2, searchBox.y2 + sepNat}; 
	ClutterActorBox scrollBox = {windowBox.x1, separatorBox.y2, windowBox.x2, windowBox.y2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->window), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->sdc), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->searchBox), &searchBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->searchIcon), &iconBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->searchSeparator), &separatorBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->scroll), &scrollBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_launcher_popup_parent_class)->allocate(self_, box, flags);
}

static void on_style_changed(CmkWidget *self_)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);

	float padding = cmk_widget_style_get_padding(self_)/2;
	ClutterMargin margin = {padding, padding, padding, padding};
	ClutterMargin margin2 = {padding, 0, 0, 0};
	clutter_actor_set_margin(CLUTTER_ACTOR(self->searchBox), &margin);
	clutter_actor_set_margin(CLUTTER_ACTOR(self->searchIcon), &margin2);

	clutter_actor_queue_relayout(CLUTTER_ACTOR(self_));
	CMK_WIDGET_CLASS(graphene_launcher_popup_parent_class)->style_changed(self_);
}

static void on_background_changed(CmkWidget *self_)
{
	const ClutterColor *color = cmk_widget_get_foreground_color(self_);
	clutter_text_set_color(GRAPHENE_LAUNCHER_POPUP(self_)->searchBox, color);
	CMK_WIDGET_CLASS(graphene_launcher_popup_parent_class)->background_changed(self_);
}

static void on_search_box_text_changed(GrapheneLauncherPopup *self, ClutterText *searchBox)
{
	g_clear_pointer(&self->filter, g_free);
	self->filter = g_utf8_strdown(clutter_text_get_text(searchBox), -1);
	popup_applist_populate(self);

	//self->scrollAmount = 0;
	//ClutterPoint p = {0, self->scrollAmount};
	//clutter_scroll_actor_scroll_to_point(self->scroll, &p);
}

static void on_search_box_activate(GrapheneLauncherPopup *self, ClutterText *searchBox)
{
	if(!self->filter || g_utf8_strlen(self->filter, -1) == 0)
		return;
	if(!self->firstApp)
		return;

	g_signal_emit_by_name(self->firstApp, "activate");
}

static void popup_applist_refresh(GrapheneLauncherPopup *self)
{
	// TODO: This lags the entire WM. Do it not synced, also cache it
	gmenu_tree_load_sync(self->appTree, NULL);

	popup_applist_populate(self);
}

static void popup_applist_populate(GrapheneLauncherPopup *self)
{
	clutter_actor_destroy_all_children(CLUTTER_ACTOR(self->scroll));
	self->firstApp = NULL;
	GMenuTreeDirectory *directory = gmenu_tree_get_root_directory(self->appTree);
	popup_applist_populate_directory(self, directory);
	gmenu_tree_item_unref(directory);
}

static ClutterActor * separator_new()
{
	ClutterActor *sep = clutter_actor_new();
	ClutterColor c = {0,0,0,25};
	clutter_actor_set_background_color(sep, &c);
	clutter_actor_set_x_expand(sep, TRUE);
	clutter_actor_set_height(sep, 2);
	return sep;
}

static gboolean add_app(GrapheneLauncherPopup *self, GDesktopAppInfo *appInfo)
{	
	if(g_desktop_app_info_get_nodisplay(appInfo))
		return FALSE;

	if(self->filter)
	{
		gchar *displayNameDown = g_utf8_strdown(g_app_info_get_display_name(G_APP_INFO(appInfo)), -1);
		gboolean passedFilter = g_strstr_len(displayNameDown, -1, self->filter) != NULL;
		g_free(displayNameDown);
		if(!passedFilter)
			return FALSE;
	}
	
	CmkButton *button = cmk_button_new();
	const gchar *iconName;
	GIcon *gicon = g_app_info_get_icon(G_APP_INFO(appInfo));
	if(G_IS_THEMED_ICON(gicon))
	{
		const gchar * const * names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
		iconName = names[0];
	}

	CmkIcon *icon = cmk_icon_new_from_name(iconName ? iconName : "open-menu-symbolic", 24);
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, g_app_info_get_display_name(G_APP_INFO(appInfo)));
	cmk_widget_set_style_parent(CMK_WIDGET(button), self->window);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(button));
	
	g_object_set_data_full(G_OBJECT(button), "appinfo", g_object_ref(appInfo), g_object_unref);
	g_signal_connect_swapped(button, "activate", G_CALLBACK(applist_on_item_clicked), self);

	if(!self->firstApp)
		self->firstApp = button;
	
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), separator_new());
	return TRUE;
}

static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory)
{
	guint count = 0;
	GMenuTreeIter *it = gmenu_tree_directory_iter(directory);
	
	while(TRUE)
	{
		GMenuTreeItemType type = gmenu_tree_iter_next(it);
		if(type == GMENU_TREE_ITEM_INVALID)
			break;
			
		if(type == GMENU_TREE_ITEM_ENTRY)
		{
			GMenuTreeEntry *entry = gmenu_tree_iter_get_entry(it);
			if(add_app(self, gmenu_tree_entry_get_app_info(entry)))
				count += 1;
			gmenu_tree_item_unref(entry);
		}
		else if(type == GMENU_TREE_ITEM_DIRECTORY)
		{
			GMenuTreeDirectory *directory = gmenu_tree_iter_get_directory(it);
	
			CmkLabel *label = cmk_label_new_with_text(gmenu_tree_directory_get_name(directory));
			cmk_widget_set_style_parent(CMK_WIDGET(label), self->window);
			clutter_actor_set_x_expand(CLUTTER_ACTOR(label), TRUE);
			clutter_actor_set_x_align(CLUTTER_ACTOR(label), CLUTTER_ACTOR_ALIGN_START);
			ClutterMargin margin = {50, 40, 20, 20};
			clutter_actor_set_margin(CLUTTER_ACTOR(label), &margin);
			clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(label));

			ClutterActor *sep = separator_new();
			clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), sep);
			
			guint subcount = popup_applist_populate_directory(self, directory);
			gmenu_tree_item_unref(directory);

			if(subcount == 0)
			{
				clutter_actor_destroy(CLUTTER_ACTOR(label));
				clutter_actor_destroy(sep);
			}
		}
	}
	
	gmenu_tree_iter_unref(it);
	return count;
}

static void applist_on_item_clicked(GrapheneLauncherPopup *self, CmkButton *button)
{
	// Delay so the click animation can be seen
	clutter_threads_add_timeout(200, (GSourceFunc)clutter_actor_destroy, self);

	GDesktopAppInfo *appInfo = g_object_get_data(G_OBJECT(button), "appinfo");
	if(appInfo)
		g_app_info_launch(G_APP_INFO(appInfo), NULL, NULL, NULL);
}

static gboolean on_key_pressed(ClutterActor *self, ClutterKeyEvent *event)
{
	if(event->keyval == CLUTTER_KEY_Tab || event->unicode_value == 0)
		return CLUTTER_EVENT_PROPAGATE;
	ClutterActor *bar = CLUTTER_ACTOR(GRAPHENE_LAUNCHER_POPUP(self)->searchBox);
	clutter_actor_grab_key_focus(bar);
	clutter_actor_event(bar, (ClutterEvent *)event, FALSE);
	return CLUTTER_EVENT_STOP;
}
