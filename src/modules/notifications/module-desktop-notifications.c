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

#include <errno.h>
#include <string.h>

#include <pulse/proplist.h>
#include <pulse/xmalloc.h>

#include <pulsecore/card.h>
#include <pulsecore/core.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/database.h>
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
    pa_database *database;
    pa_ui_notification_manager *manager;
};

struct notification_userdata {
    struct userdata *u;
    pa_card *card;
};

static void card_set_default(pa_card *card, pa_core *core) {
    pa_log_debug("Setting %s as default.", pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));
}

static void card_save(pa_card *card, pa_database *database, bool use) {
    pa_datum key, data;
    char *card_desc;

    pa_assert(card);
    pa_assert(database);

    key.data = card_desc = pa_xstrdup(pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));
    key.size = strlen(card_desc);

    data.data = pa_xnew(bool, 1);
    data.size = sizeof(bool);

    *(bool*)(data.data) = use;

    pa_assert_se(pa_database_set(database, &key, &data, true) == 0);

    pa_xfree(card_desc);
    pa_xfree(data.data);
}

static bool* card_load(pa_card *card, pa_database *database) {
    pa_datum key, data;
    char *card_desc;

    pa_assert(card);
    pa_assert(database);

    key.data = card_desc = pa_xstrdup(pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));
    key.size = strlen(card_desc);

    if(pa_database_get(database, &key, &data) == NULL)
        return NULL;
    else
        return (bool*) data.data;

    pa_xfree(card_desc);
    /* TODO: who frees data.data? */
}

static void notification_reply_cb(pa_ui_notification_reply* reply) {
    struct notification_userdata *nu;

    pa_assert(reply);

    nu = reply->source->userdata;

    pa_log_debug("Got notification reply for %s.", pa_proplist_gets(nu->card->proplist, PA_PROP_DEVICE_DESCRIPTION));

    switch(reply->type) {
    case PA_UI_NOTIFCATION_REPLY_ERROR:
        pa_log_error("An error has occured with the notification for %s", pa_proplist_gets(nu->card->proplist, PA_PROP_DEVICE_DESCRIPTION));

    case PA_UI_NOTIFCATION_REPLY_CANCELLED:
    case PA_UI_NOTIFCATION_REPLY_DISMISSED:
    case PA_UI_NOTIFCATION_REPLY_EXPIRED:
    default:
        break;

    case PA_UI_NOTIFCATION_REPLY_ACTION_INVOKED:
        if (pa_streq(reply->action_key, "0")) {
            card_set_default(nu->card, NULL); /* TODO: get the core here */
            card_save(nu->card, nu->u->database, true);
        } else if (pa_streq(reply->action_key, "1")) {
            card_save(nu->card, nu->u->database, false);
        }

        break;
    }

    pa_xfree(nu);
    pa_ui_notification_reply_free(reply);
}

static pa_hook_result_t card_put_cb(pa_core *core, pa_card *card, void *userdata) {
    const char *card_name;
    char *body;
    pa_ui_notification *n;
    struct userdata *u;
    struct notification_userdata *nu;
    bool *use_card;

    pa_assert(core);
    pa_assert(card);
    pa_assert(userdata);

    u = userdata;

    if((use_card = card_load(card, u->database)) == NULL) {
        nu = pa_xnew(struct notification_userdata, 1);

        nu->u = u;
        nu->card = card;
        card_name = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_log_debug("Card detected: %s.", card_name);

        body = pa_sprintf_malloc("Would you like to set %s as default?", card_name);
        n = pa_ui_notification_new(notification_reply_cb, "A new card has been connected.", body, -1, nu);
        pa_hashmap_put(n->actions, "0", pa_xstrdup("Yes"));
        pa_hashmap_put(n->actions, "1", pa_xstrdup("No"));

        pa_xfree(body);

        pa_ui_notification_manager_send(nu->u->manager, n);
    } else if (*use_card)
        card_set_default(card, core);

    if(use_card)
        pa_xfree(use_card);

    return PA_HOOK_OK;
}

int pa__init(pa_module*m) {
    struct userdata *u;
    char *fname;

    m->userdata = u = pa_xnew(struct userdata, 1);

    if (!(fname = pa_state_path("ui-seen-cards", FALSE)))
        goto fail;

    if (!(u->database = pa_database_open(fname, TRUE))) {
        pa_log_error("Failed to open volume database '%s': %s", fname, pa_cstrerror(errno));
        goto fail;
    }

    u->card_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CARD_PUT], PA_HOOK_LATE, (pa_hook_cb_t) card_put_cb, u);
    u->manager = pa_ui_notification_manager_get(m->core);

    return 0;

fail:
    if (fname)
        pa_xfree(fname);

    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->card_put_slot)
        pa_hook_slot_free(u->card_put_slot);

    if(u->database)
        pa_database_close(u->database);

    if(u->manager)
        pa_ui_notification_manager_unref(u->manager);

    pa_xfree(u);
}
