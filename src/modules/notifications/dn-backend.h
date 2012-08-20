#ifndef foodnbackendfoo
#define foodnbackendfoo
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
#include <dbus/dbus.h>

#include <pulsecore/core.h>
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/llist.h>

#include "notification-backend.h"

typedef void (*dn_handle_reply_cb_t)(pa_ui_notification_reply *reply, void *userdata);

pa_ui_notification_backend* dn_backend_new(pa_core *core);
pa_ui_notification_backend* dn_backend_new_with_reply_handle(pa_core *core, bool use_actions, dn_handle_reply_cb_t notification_reply_handle, void *userdata);
void dn_backend_free(pa_ui_notification_backend *backend);

#endif
