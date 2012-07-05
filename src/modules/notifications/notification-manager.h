#ifndef foonotificationmanagerfoo
#define foonotificationmanagerfoo

/***
  This file is part of PulseAudio.

  Copyright 2012 Ștefan Săftescu

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <pulsecore/core.h>

#include "notification.h"
#include "notification-backend.h"

typedef struct pa_ui_notification_manager pa_ui_notification_manager;

pa_ui_notification_manager* pa_ui_notification_manager_get(pa_core *c);

pa_ui_notification_manager* pa_ui_notification_manager_ref(pa_ui_notification_manager *m);
void pa_ui_notification_manager_unref(pa_ui_notification_manager *m);

int pa_ui_notification_manager_register_backend(pa_ui_notification_manager *m, pa_ui_notification_backend *b);
void pa_ui_notification_manager_unregister_backend(pa_ui_notification_manager *m);

pa_ui_notification_backend* pa_ui_notification_manager_get_backend(pa_ui_notification_manager *m);

int pa_ui_notification_manager_send(pa_ui_notification_manager *m, pa_ui_notification *n);
int pa_ui_notification_manager_cancel(pa_ui_notification_manager *m, pa_ui_notification *n);

#endif
