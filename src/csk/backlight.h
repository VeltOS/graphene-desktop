/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * Methods for controlling hardware lighting. Getting or setting screen
 * backlight requires gnome-settings-daemon to be installed (as it uses the
 * gsd-backlight-helper tool with pkexec), and getting or setting the
 * keyboard brightness requires the UPower daemon running.
 *
 * These methods are probably only useful on laptops; calling them on
 * systems without adjustable backlights has no effect.
 */

/*
 * Attempt to get the main screen's backlight brightness in
 * the range [0, 1]. Returns a negative value on failure.
 */
gfloat csk_backlight_get_brightness(void);

/*
 * Attempt to set the main screen's backlight brightness 
 * in the range [0, 1]. Set delta to TRUE for value to be
 * relative. Returns the new brightness, or a negative value
 * on failure.
 */
gfloat csk_backlight_set_brightness(gfloat value, gboolean relative);

/*
 * Attempt to get the keyboard's backlight brightness
 * in the range [0, 1]. Returns a negative value on failure.
 */
gfloat csk_keyboard_backlight_get_brightness(void);

/*
 * Attempt to set the keyboard's backlight brightness 
 * in the range [0, 1]. Set delta to TRUE for value to be
 * relative. Returns the new brightness, or a negative value
 * on failure.
 */
gfloat csk_keyboard_backlight_set_brightness(gfloat value, gboolean relative);
