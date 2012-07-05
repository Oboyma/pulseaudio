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

#include <pulse/proplist.h>
#include <pulse/xmalloc.h>

#include <pulsecore/card.h>
#include <pulsecore/core.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/module.h>

#include "notification.h"
#include "notification-manager.h"

#include "module-desktop-notifications-symdef.h"

PA_MODULE_AUTHOR("Ștefan Săftescu");
PA_MODULE_DESCRIPTION("Shows a notification when a new card is plugged in.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);


struct userdata {
    pa_hook_slot *card_put_slot;
    pa_ui_notification_manager *manager;
};

static void notification_reply_cb(pa_ui_notification_reply* reply) {
    pa_ui_notification_free(reply->source);
    /* TODO: free reply */
}

static pa_hook_result_t card_put_cb(pa_core *c, pa_card *card, void *userdata) {
    char *card_name;
    pa_ui_notification *n;
    struct userdata *u;

    u = userdata;
    card_name = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);
    pa_log_debug("Card detected: %s.", card_name);

    n = pa_ui_notification_new(notification_reply_cb, card);
    n->summary = "A new card has been detected.";
    n->body = pa_sprintf_malloc("%s has been detected. Would you like to set it as default?", card_name);
    /* TODO: free body? */

    pa_ui_notification_manager_send(u->manager, n);

    return PA_HOOK_OK;
}

int pa__init(pa_module*m) {
    struct userdata *u;

    m->userdata = u = pa_xnew(struct userdata, 1);

    u->card_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CARD_PUT], PA_HOOK_LATE, (pa_hook_cb_t) card_put_cb, u);
    u->manager = pa_ui_notification_manager_get(m->core);

    return 0;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->card_put_slot)
        pa_hook_slot_free(u->card_put_slot);

    if(u->manager)
        pa_ui_notification_manager_unref(u->manager);

    pa_xfree(u);
}
