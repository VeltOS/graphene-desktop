/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_STATUS_NOTIFIER_HOST_H__
#define __GRAPHENE_STATUS_NOTIFIER_HOST_H__

#include <libcmk/cmk-widget.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_STATUS_NOTIFIER_HOST  graphene_status_notifier_host_get_type()
G_DECLARE_FINAL_TYPE(GrapheneStatusNotifierHost, graphene_status_notifier_host, GRAPHENE, STATUS_NOTIFIER_HOST, CmkWidget)
GrapheneStatusNotifierHost * graphene_status_notifier_host_new();

G_END_DECLS

#endif /* __GRAPHENE_STATUS_NOTIFIER_HOST_H__ */
