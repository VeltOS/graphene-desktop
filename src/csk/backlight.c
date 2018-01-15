/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * TODO: This is a signficantly laggy when rapidly changing brightness
 * (i.e. pressing and holding brightness key), mainly due to calling pkexec.
 */
#include <glib-object.h>
#include <math.h>
#include <time.h>

#define BH_EXEC "/usr/lib/gnome-settings-daemon/gsd-backlight-helper" 
#define BH_GET_MAX "--get-max-brightness"
#define BH_GET "--get-brightness"
#define BH_SET "--set-brightness"

static gboolean backlight_command(const gchar *command, const gchar *value, gchar **stdout, gint *exitCode)
{
	const gchar *argv[] = {"pkexec", BH_EXEC, command, value, NULL};
	return g_spawn_sync(NULL,
		// Don't use pkexec if this is not a set- command
		((void*)command != (void*)BH_SET) ? (gchar **)&argv[1] : (gchar **)argv,
		NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, stdout, NULL, exitCode, NULL);
}

static gint64 get_max_backlight(void)
{
	static gint64 cache = -1;
	static time_t cacheTime = 0;
	if(cache >= 0 && time(NULL) - cacheTime < 5)
		return cache;

	gchar *stdout;
	gint exitCode;
	gboolean r = backlight_command(BH_GET_MAX, NULL, &stdout, &exitCode);
	cacheTime = time(NULL);
	cache = -1;
	if(!r || exitCode)
		return -1;
	gchar *end = NULL;
	cache = g_ascii_strtoll(stdout, &end, 10);
	if(stdout == end)
		return -1;
	return cache;
}

static gint64 get_backlight(void)
{
	gchar *stdout;
	gint exitCode;
	gboolean r = backlight_command(BH_GET, NULL, &stdout, &exitCode);
	if(!r || exitCode)
		return -1;
	gchar *end = NULL;
	gint64 val = g_ascii_strtoll(stdout, &end, 10);
	if(stdout == end)
		return -1;
	return val;
}

static gboolean set_backlight(gint64 val)
{
	gchar *sval = g_strdup_printf("%"G_GINT64_FORMAT, val);
	gint exitCode;
	gboolean r = backlight_command(BH_SET, sval, NULL, &exitCode);
	g_free(sval);
	return r && !exitCode;
}

gfloat csk_backlight_get_brightness(void)
{
	gint64 val = get_backlight();
	if(val < 0)
		return -1;
	gint64 max = get_max_backlight();
	if(max < 0)
		return -1;
	return (gfloat)val/max;
}

gfloat csk_backlight_set_brightness(gfloat value, gboolean relative)
{
	gint64 max = get_max_backlight();
	if(max < 0)
		return -1;
	gfloat fvalue = value * max;
	gint64 ivalue = (fvalue < 0) ? floor(fvalue) : ceil(fvalue);
	if(relative)
	{
		gint64 prev = get_backlight();
		if(prev < 0)
			return -1;
		gint64 newval = MIN(MAX(0, prev+ivalue), max);
		if(prev != newval)
			if(!set_backlight(newval))
				return -1;
		return (gfloat)newval / max;
	}
	else
	{
		gint64 newval = MIN(MAX(0, ivalue), max);
		if(!set_backlight(newval))
			return -1;
		return (gfloat)newval / max;
	}
}

gfloat csk_keyboard_backlight_get_brightness(void)
{
	// TODO
	return 0;
}

gfloat csk_keyboard_backlight_set_brightness(UNUSED gfloat value, UNUSED gboolean relative)
{
	// TODO
	return 0;
}
