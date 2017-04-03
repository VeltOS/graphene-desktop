/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_NOTIFICATION_BOX_H__
#define __GRAPHENE_NOTIFICATION_BOX_H__

#include <libcmk/cmk-widget.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_NOTIFICATION_BOX  graphene_notification_box_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNotificationBox, graphene_notification_box, GRAPHENE, NOTIFICATION_BOX, CmkWidget)

typedef void (*NotificationAddedCb)(gpointer userdata, ClutterActor *notification);

GrapheneNotificationBox * graphene_notification_box_new(NotificationAddedCb notificationAddedCb, gpointer userdata);

G_END_DECLS

#endif /* __GRAPHENE_NOTIFICATION_BOX_H__ */
