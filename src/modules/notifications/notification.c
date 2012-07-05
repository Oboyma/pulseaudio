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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/xmalloc.h>

#include "notification.h"
#include "notification-backend.h"

pa_ui_notification* pa_ui_notification_new(pa_ui_notification_reply_cb_t reply_cb, void *userdata) {
    pa_ui_notification *n = pa_xnew(pa_ui_notification, 1);

    n->replaces_id = 0;
    n->summary = "";
    n->body = "";
    n->actions = NULL;
    n->num_actions = 0; /* TODO: actions */
    n->expire_timeout = -1;

    n->handle_reply = reply_cb;
    n->userdata = userdata;


    return n;
}

void pa_ui_notification_free(pa_ui_notification *n) {
    pa_xfree(n);
}
