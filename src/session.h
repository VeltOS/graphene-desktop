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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GRAPHENE_SESSION_H__
#define __GRAPHENE_SESSION_H__

#include <glib.h>
#include <clutter/clutter.h>

G_BEGIN_DECLS

typedef void (*CSMStartupCompleteCallback)(gpointer userdata);
typedef void (*CSMDialogCallback)(ClutterActor *dialog, gpointer userdata);
typedef void (*CSMQuitCallback)(gboolean failed, gpointer userdata);

void graphene_session_init(CSMStartupCompleteCallback startupCb, CSMDialogCallback dialogCb, CSMQuitCallback quitCb, gpointer userdata);

/*
 * Immediately exits the session, attempting to close clients.
 * Pass TRUE to failed if this exit is due to an error.
 * Return value for internal purposes. Ignore.
 */
gboolean graphene_session_exit(gboolean failed);

/*
 * Shows the logout dialog, same as the logout DBus command.
 */
void graphene_session_request_logout();

G_END_DECLS

#endif /* __GRAPHENE_SESSION_H__ */
