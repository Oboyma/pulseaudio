#ifndef foonotificationfoo
#define foonotificationfoo
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
#include <pulsecore/hashmap.h>

typedef struct pa_ui_notification pa_ui_notification;
typedef struct pa_ui_notification_reply pa_ui_notification_reply;

typedef void(*pa_ui_notification_reply_cb_t)(pa_ui_notification_reply *reply);

struct pa_ui_notification {
    pa_ui_notification *replaced_notification;
    char *summary;
    char *body;
    pa_hashmap *actions;
    int expire_timeout;

    pa_ui_notification_reply_cb_t reply_cb;
    void* userdata;
};

typedef enum pa_ui_notification_reply_type {
    PA_UI_NOTIFCATION_REPLY_ERROR,
    PA_UI_NOTIFCATION_REPLY_CANCELLED,
    PA_UI_NOTIFCATION_REPLY_DISMISSED,
    PA_UI_NOTIFCATION_REPLY_EXPIRED,
    PA_UI_NOTIFCATION_REPLY_ACTION_INVOKED,
    PA_UI_NOTIFCATION_REPLY_MAX
} pa_ui_notification_reply_type_t;

struct pa_ui_notification_reply {
    pa_ui_notification_reply_type_t type;
    pa_ui_notification *source;
    char *action_key;
};

pa_ui_notification* pa_ui_notification_new(pa_ui_notification_reply_cb_t reply_cb, const char *summary, const char *body, int timeout, void *userdata);
void pa_ui_notification_free(pa_ui_notification *notification);

pa_ui_notification_reply* pa_ui_notification_reply_new(pa_ui_notification_reply_type_t type, pa_ui_notification *source, char *action_key);
void pa_ui_notification_reply_free(pa_ui_notification_reply *reply);

#endif
