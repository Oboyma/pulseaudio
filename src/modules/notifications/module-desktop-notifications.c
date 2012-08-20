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
#include <pulsecore/i18n.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/namereg.h>

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
    pa_core *core;
};

static void card_set_default(pa_card *card, pa_core *core) {
    pa_sink_input *i;
    pa_source_output *o;
    pa_sink *def_sink, *sink;
    pa_source *def_source, *source;
    uint32_t idx;

    pa_assert(card);
    pa_assert(core);

    pa_log_debug("Setting %s as default.", pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));

    /* get the first sink */
    sink = pa_idxset_first(card->sinks, &idx);

    /* find the first non-monitor source */
    PA_IDXSET_FOREACH(source, card->sources, idx)
        if (!source->monitor_of)
            break;

    /* from module-switch-on-connect */

    /* Don't want to run during startup or shutdown */
    if (core->state != PA_CORE_RUNNING)
        return;

    if (!sink) {
        pa_log_info("The card has no sinks.");
        goto source;
    }

    def_sink = pa_namereg_get_default_sink(core);
    if (def_sink == sink)
        goto source;

    /* Actually do the switch to the new sink */
    pa_namereg_set_default_sink(core, sink);

    /* Now move all old inputs over */
    if (pa_idxset_size(def_sink->inputs) <= 0) {
        pa_log_debug("No sink inputs to move away.");
        goto source;
    }

    PA_IDXSET_FOREACH(i, def_sink->inputs, idx) {
        if (i->save_sink || !PA_SINK_INPUT_IS_LINKED(i->state))
            continue;

        if (pa_sink_input_move_to(i, sink, FALSE) < 0)
            pa_log_info("Failed to move sink input %u \"%s\" to %s.", i->index,
                        pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)), sink->name);
        else
            pa_log_info("Successfully moved sink input %u \"%s\" to %s.", i->index,
                        pa_strnull(pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME)), sink->name);
    }

source:
    if (!source) {
        pa_log_info("The card has no sources.");
        return;
    }

    /* Don't switch to a monitoring source */
    if (source->monitor_of)
        return;

    def_source = pa_namereg_get_default_source(core);
    if (def_source == source)
        return;

    /* Actually do the switch to the new source */
    pa_namereg_set_default_source(core, source);

    /* Now move all old outputs over */
    if (pa_idxset_size(def_source->outputs) <= 0) {
        pa_log_debug("No source outputs to move away.");
        return;
    }

    PA_IDXSET_FOREACH(o, def_source->outputs, idx) {
        if (o->save_source || !PA_SOURCE_OUTPUT_IS_LINKED(o->state))
            continue;

        if (pa_source_output_move_to(o, source, FALSE) < 0)
            pa_log_info("Failed to move source output %u \"%s\" to %s.", o->index,
                        pa_strnull(pa_proplist_gets(o->proplist, PA_PROP_APPLICATION_NAME)), source->name);
        else
            pa_log_info("Successfully moved source output %u \"%s\" to %s.", o->index,
                        pa_strnull(pa_proplist_gets(o->proplist, PA_PROP_APPLICATION_NAME)), source->name);
    }
}

static void card_save(pa_card *card, pa_database *database, int use) {
    pa_datum key, data;
    char *card_desc;

    pa_assert(card);
    pa_assert(database);

    key.data = card_desc = pa_xstrdup(pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));
    key.size = strlen(card_desc);

    data.data = pa_xnew(int, 1);
    data.size = sizeof(int);

    *(int*)(data.data) = use;

    pa_assert_se(pa_database_set(database, &key, &data, true) == 0);

    pa_xfree(card_desc);
    pa_xfree(data.data);
}

static int card_load(pa_card *card, pa_database *database) {
    pa_datum key, data;
    char *card_desc;
    int result;

    pa_assert(card);
    pa_assert(database);

    key.data = card_desc = pa_xstrdup(pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION));
    key.size = strlen(card_desc);

    if (pa_database_get(database, &key, &data) == NULL)
        result = -1;
    else {
       result = *((int*)data.data);
       pa_xfree(data.data);
    }

    pa_xfree(card_desc);

    return result;
}

static void notification_reply_cb(pa_ui_notification_reply* reply) {
    struct notification_userdata *nu;

    pa_assert(reply);

    nu = reply->source->userdata;

    pa_log_debug("Got notification reply for %s.", pa_proplist_gets(nu->card->proplist, PA_PROP_DEVICE_DESCRIPTION));

    switch (reply->type) {
        case PA_UI_NOTIFCATION_REPLY_ERROR:
            pa_log_error("An error has occured with the notification for %s", pa_proplist_gets(nu->card->proplist, PA_PROP_DEVICE_DESCRIPTION));

        case PA_UI_NOTIFCATION_REPLY_CANCELLED:
        case PA_UI_NOTIFCATION_REPLY_DISMISSED:
        case PA_UI_NOTIFCATION_REPLY_EXPIRED:
        default:
            break;

        case PA_UI_NOTIFCATION_REPLY_ACTION_INVOKED:
            if (pa_streq(reply->action_key, "0")) {
                card_set_default(nu->card, nu->core);
                card_save(nu->card, nu->u->database, 0);
            } else if (pa_streq(reply->action_key, "1")) {
                card_save(nu->card, nu->u->database, 1);
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
    int use_card;

    pa_assert(core);
    pa_assert(card);

    pa_assert_se(u = userdata);

    if ((use_card = card_load(card, u->database)) < 0) {
        nu = pa_xnew(struct notification_userdata, 1);

        nu->u = u;
        nu->card = card;
        nu->core = core;
        card_name = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);
        pa_log_debug("Card detected: %s.", card_name);

        body = pa_sprintf_malloc(_("Would you like to set %s as default?"), card_name);
        n = pa_ui_notification_new(notification_reply_cb, "audio-card-symbolic", card_name, _("A new card has been connected."), body, -1, nu);
        pa_hashmap_put(n->actions, "0", pa_xstrdup(_("Yes")));
        pa_hashmap_put(n->actions, "1", pa_xstrdup(_("No")));

        pa_xfree(body);

        pa_ui_notification_manager_send(nu->u->manager, n);
    } else if (use_card == 0)
        card_set_default(card, core);

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

    if (u->database)
        pa_database_close(u->database);

    if (u->manager)
        pa_ui_notification_manager_unref(u->manager);

    pa_xfree(u);
}
