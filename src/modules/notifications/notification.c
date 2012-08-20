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

#include <pulsecore/hashmap.h>

#include "notification.h"
#include "notification-backend.h"

pa_ui_notification* pa_ui_notification_new(pa_ui_notification_reply_cb_t reply_cb, const char* icon, const char *title, const char* summary, const char* body, int timeout, void *userdata) {
    pa_ui_notification *n;

    pa_assert(reply_cb);

    n = pa_xnew0(pa_ui_notification, 1);

    if (icon)
        n->icon = pa_xstrdup(icon);

    if (title)
        n->title = pa_xstrdup(title);

    if (summary)
        n->summary = pa_xstrdup(summary);

    if (body)
        n->body = pa_xstrdup(body);

    n->expire_timeout = timeout;
    n->actions = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    n->reply_cb = reply_cb;
    n->userdata = userdata;

    return n;
}

static void free_func(void* p, void* userdata) {
    pa_xfree(p);
}

void pa_ui_notification_free(pa_ui_notification *n) {
    pa_assert(n);

    pa_xfree(n->icon);
    pa_xfree(n->title);
    pa_xfree(n->summary);
    pa_xfree(n->body);

    pa_hashmap_free(n->actions, free_func, NULL); /* TODO: correct free function? */
    pa_xfree(n);
}

pa_ui_notification_reply* pa_ui_notification_reply_new(pa_ui_notification_reply_type_t type, pa_ui_notification *source, char *action_key) {
    pa_assert(source);

    pa_ui_notification_reply *reply = pa_xnew(pa_ui_notification_reply, 1);

    reply->type = type;
    reply->source = source;
    reply->action_key = action_key;

    return reply;
}

void pa_ui_notification_reply_free(pa_ui_notification_reply *reply) {
    pa_assert(reply);

    pa_ui_notification_free(reply->source);
    pa_xfree(reply);
}
