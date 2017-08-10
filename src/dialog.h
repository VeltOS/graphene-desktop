/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * This class creates a message dialog with button responses, but does not
 * handle displaying it to the user / centering it on screen.
 */

#ifndef __GRAPHENE_DIALOG_H__
#define __GRAPHENE_DIALOG_H__

#include <cmk/cmk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_DIALOG  graphene_dialog_get_type()
G_DECLARE_DERIVABLE_TYPE(GrapheneDialog, graphene_dialog, GRAPHENE, DIALOG, CmkWidget)

typedef struct _GrapheneDialogClass GrapheneDialogClass;

struct _GrapheneDialogClass
{
	CmkWidgetClass parentClass;
	
	void (*select) (GrapheneDialog *dialog, const gchar *selection);
};

GrapheneDialog * graphene_dialog_new();

/*
 * Creates a dialog with a message, icon, and a variable number of buttons.
 * End the button list with a NULL.
 */
GrapheneDialog * graphene_dialog_new_simple(const gchar *message, const gchar *icon, ...);

void graphene_dialog_set_content(GrapheneDialog *dialog, ClutterActor *content);
void graphene_dialog_set_message(GrapheneDialog *dialog, const gchar *message);

void graphene_dialog_set_buttons(GrapheneDialog *dialog, const gchar * const *buttons);

/*
 * Gets a list of the CmkButton actors that the dialog is using.
 * This can be used for making custom modifications to the buttons.
 */
GList * graphene_dialog_get_buttons(GrapheneDialog *dialog);

void graphene_dialog_set_icon(GrapheneDialog *dialog, const gchar *icon);
const gchar * graphene_dialog_get_icon(GrapheneDialog *dialog);

G_END_DECLS

#endif /* __GRAPHENE_DIALOG_H__ */
