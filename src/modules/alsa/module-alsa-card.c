/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
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
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>

#include "alsa-util.h"
#include "alsa-sink.h"
#include "alsa-source.h"
#include "module-alsa-card-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("ALSA Card");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "name=<name for the sink/source> "
        "device_id=<ALSA card index> "
        "format=<sample format> "
        "rate=<sample rate> "
        "fragments=<number of fragments> "
        "fragment_size=<fragment size> "
        "mmap=<enable memory mapping?> "
        "tsched=<enable system timer based scheduling mode?> "
        "tsched_buffer_size=<buffer size when using timer based scheduling> "
        "tsched_buffer_watermark=<lower fill watermark> "
        "profile=<profile name>");

static const char* const valid_modargs[] = {
    "name",
    "device_id",
    "format",
    "rate",
    "fragments",
    "fragment_size",
    "mmap",
    "tsched",
    "tsched_buffer_size",
    "tsched_buffer_watermark",
    "profile",
    NULL
};

#define DEFAULT_DEVICE_ID "0"

struct userdata {
    pa_core *core;
    pa_module *module;

    char *device_id;

    pa_card *card;
    pa_sink *sink;
    pa_source *source;

    pa_modargs *modargs;
};

struct profile_data {
    const pa_alsa_profile_info *sink_profile, *source_profile;
};

static void enumerate_cb(
        const pa_alsa_profile_info *sink,
        const pa_alsa_profile_info *source,
        void *userdata) {

    pa_hashmap *profiles = (pa_hashmap *) userdata;
    char *t, *n;
    pa_card_profile *p;
    struct profile_data *d;

    if (sink && source) {
        n = pa_sprintf_malloc("output-%s+input-%s", sink->name, source->name);
        t = pa_sprintf_malloc("Output %s + Input %s", sink->description, source->description);
    } else if (sink) {
        n = pa_sprintf_malloc("output-%s", sink->name);
        t = pa_sprintf_malloc("Output %s", sink->description);
    } else {
        pa_assert(source);
        n = pa_sprintf_malloc("input-%s", source->name);
        t = pa_sprintf_malloc("Input %s", source->description);
    }

    pa_log_info("Found output profile '%s'", t);

    p = pa_card_profile_new(n, t, sizeof(struct profile_data));

    pa_xfree(t);
    pa_xfree(n);

    p->priority = (sink ? sink->priority : 0)*100 + (source ? source->priority : 0);
    p->n_sinks = !!sink;
    p->n_sources = !!source;

    if (sink)
        p->max_sink_channels = sink->map.channels;
    if (source)
        p->max_source_channels = source->map.channels;

    d = PA_CARD_PROFILE_DATA(p);

    d->sink_profile = sink;
    d->source_profile = source;

    pa_hashmap_put(profiles, p->name, p);
}

static void add_disabled_profile(pa_hashmap *profiles) {
    pa_card_profile *p;
    struct profile_data *d;

    p = pa_card_profile_new("off", "Off", sizeof(struct profile_data));

    d = PA_CARD_PROFILE_DATA(p);
    d->sink_profile = d->source_profile = NULL;

    pa_hashmap_put(profiles, p->name, p);
}

static int card_set_profile(pa_card *c, pa_card_profile *new_profile) {
    struct userdata *u;
    struct profile_data *nd, *od;

    pa_assert(c);
    pa_assert(new_profile);
    pa_assert_se(u = c->userdata);

    nd = PA_CARD_PROFILE_DATA(new_profile);
    od = PA_CARD_PROFILE_DATA(c->active_profile);

    if (od->sink_profile != nd->sink_profile) {
        if (u->sink) {
            pa_alsa_sink_free(u->sink);
            u->sink = NULL;
        }

        if (nd->sink_profile)
            u->sink = pa_alsa_sink_new(c->module, u->modargs, __FILE__, c, nd->sink_profile);
    }

    if (od->source_profile != nd->source_profile) {
        if (u->source) {
            pa_alsa_source_free(u->source);
            u->source = NULL;
        }

        if (nd->source_profile)
            u->source = pa_alsa_source_new(c->module, u->modargs, __FILE__, c, nd->source_profile);
    }

    return 0;
}

static void init_profile(struct userdata *u) {
    struct profile_data *d;

    pa_assert(u);

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    if (d->sink_profile)
        u->sink = pa_alsa_sink_new(u->module, u->modargs, __FILE__, u->card, d->sink_profile);

    if (d->source_profile)
        u->source = pa_alsa_source_new(u->module, u->modargs, __FILE__, u->card, d->source_profile);
}

int pa__init(pa_module*m) {
    pa_card_new_data data;
    pa_modargs *ma;
    int alsa_card_index;
    struct userdata *u;

    pa_alsa_redirect_errors_inc();
    snd_config_update_free_global();

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    u->device_id = pa_xstrdup(pa_modargs_get_value(ma, "device_id", DEFAULT_DEVICE_ID));
    u->card = NULL;
    u->sink = NULL;
    u->source = NULL;
    u->modargs = ma;

    if ((alsa_card_index = snd_card_get_index(u->device_id)) < 0) {
        pa_log("Card '%s' doesn't exist: %s", u->device_id, snd_strerror(alsa_card_index));
        goto fail;
    }

    pa_card_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;
    pa_alsa_init_proplist_card(data.proplist, alsa_card_index);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_STRING, u->device_id);
    pa_card_new_data_set_name(&data, pa_modargs_get_value(ma, "name", u->device_id));

    data.profiles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if (pa_alsa_probe_profiles(u->device_id, &m->core->default_sample_spec, enumerate_cb, data.profiles) < 0) {
        pa_card_new_data_done(&data);
        goto fail;
    }

    if (pa_hashmap_isempty(data.profiles)) {
        pa_log("Failed to find a working profile.");
        pa_card_new_data_done(&data);
        goto fail;
    }

    add_disabled_profile(data.profiles);

    u->card = pa_card_new(m->core, &data);
    pa_card_new_data_done(&data);

    if (!u->card)
        goto fail;

    u->card->userdata = u;
    u->card->set_profile = card_set_profile;

    init_profile(u);

    return 0;

fail:

    pa__done(m);
    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return
        (u->sink ? pa_sink_linked_by(u->sink) : 0) +
        (u->source ? pa_source_linked_by(u->source) : 0);
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        goto finish;

    if (u->sink)
        pa_alsa_sink_free(u->sink);

    if (u->source)
        pa_alsa_source_free(u->source);

    if (u->card)
        pa_card_free(u->card);

    if (u->modargs)
        pa_modargs_free(u->modargs);

    pa_xfree(u->device_id);
    pa_xfree(u);

finish:
    snd_config_update_free_global();
    pa_alsa_redirect_errors_dec();
}