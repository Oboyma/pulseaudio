#ifndef foopulsesinkhfoo
#define foopulsesinkhfoo

/* $Id$ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

typedef struct pa_sink pa_sink;

#include <inttypes.h>

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/volume.h>

#include <pulsecore/core.h>
#include <pulsecore/idxset.h>
#include <pulsecore/source.h>
#include <pulsecore/module.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/msgobject.h>
#include <pulsecore/rtpoll.h>

#define PA_MAX_INPUTS_PER_SINK 32

typedef enum pa_sink_state {
    PA_SINK_INIT,
    PA_SINK_RUNNING,
    PA_SINK_SUSPENDED,
    PA_SINK_IDLE,
    PA_SINK_UNLINKED
} pa_sink_state_t;

static inline pa_bool_t PA_SINK_IS_OPENED(pa_sink_state_t x) {
    return x == PA_SINK_RUNNING || x == PA_SINK_IDLE;
}

static inline pa_bool_t PA_SINK_IS_LINKED(pa_sink_state_t x) {
    return x == PA_SINK_RUNNING || x == PA_SINK_IDLE || x == PA_SINK_SUSPENDED;
}

struct pa_sink {
    pa_msgobject parent;

    uint32_t index;
    pa_core *core;
    pa_sink_state_t state;
    pa_sink_flags_t flags;

    char *name;
    char *driver;                           /* may be NULL */
    pa_proplist *proplist;

    pa_module *module;                      /* may be NULL */

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;

    pa_idxset *inputs;
    unsigned n_corked;
    pa_source *monitor_source;

    pa_cvolume volume;
    pa_bool_t muted;
    pa_bool_t refresh_volume;
    pa_bool_t refresh_mute;

    pa_asyncmsgq *asyncmsgq;
    pa_rtpoll *rtpoll;

    pa_memchunk silence;

    pa_usec_t min_latency; /* we won't go below this latency */
    pa_usec_t max_latency; /* An upper limit for the latencies */

    /* Called when the main loop requests a state change. Called from
     * main loop context. If returns -1 the state change will be
     * inhibited */
    int (*set_state)(pa_sink *s, pa_sink_state_t state); /* may be NULL */

    /* Callled when the volume is queried. Called from main loop
     * context. If this is NULL a PA_SINK_MESSAGE_GET_VOLUME message
     * will be sent to the IO thread instead. */
    int (*get_volume)(pa_sink *s);             /* may be null */

    /* Called when the volume shall be changed. Called from main loop
     * context. If this is NULL a PA_SINK_MESSAGE_SET_VOLUME message
     * will be sent to the IO thread instead. */
    int (*set_volume)(pa_sink *s);             /* dito */

    /* Called when the mute setting is queried. Called from main loop
     * context. If this is NULL a PA_SINK_MESSAGE_GET_MUTE message
     * will be sent to the IO thread instead. */
    int (*get_mute)(pa_sink *s);               /* dito */

    /* Called when the mute setting shall be changed. Called from main
     * loop context. If this is NULL a PA_SINK_MESSAGE_SET_MUTE
     * message will be sent to the IO thread instead. */
    int (*set_mute)(pa_sink *s);               /* dito */

    /* Called when the latency is queried. Called from main loop
    context. If this is NULL a PA_SINK_MESSAGE_GET_LATENCY message
    will be sent to the IO thread instead. */
    pa_usec_t (*get_latency)(pa_sink *s); /* dito */

    /* Called when a rewind request is issued. Called from IO thread
     * context. */
    void (*request_rewind)(pa_sink *s);        /* dito */

    /* Called when a the requested latency is changed. Called from IO
     * thread context. */
    void (*update_requested_latency)(pa_sink *s); /* dito */

    /* Contains copies of the above data so that the real-time worker
     * thread can work without access locking */
    struct {
        pa_sink_state_t state;
        pa_hashmap *inputs;
        pa_cvolume soft_volume;
        pa_bool_t soft_muted;

        pa_bool_t requested_latency_valid;
        pa_usec_t requested_latency;

        /* The number of bytes we need keep around to be able to satisfy
         * every DMA buffer rewrite */
        size_t max_rewind;

        /* Maximum of what clients requested to rewind in this cycle */
        size_t rewind_nbytes;
    } thread_info;

    void *userdata;
};

PA_DECLARE_CLASS(pa_sink);
#define PA_SINK(s) (pa_sink_cast(s))

typedef enum pa_sink_message {
    PA_SINK_MESSAGE_ADD_INPUT,
    PA_SINK_MESSAGE_REMOVE_INPUT,
    PA_SINK_MESSAGE_GET_VOLUME,
    PA_SINK_MESSAGE_SET_VOLUME,
    PA_SINK_MESSAGE_GET_MUTE,
    PA_SINK_MESSAGE_SET_MUTE,
    PA_SINK_MESSAGE_GET_LATENCY,
    PA_SINK_MESSAGE_GET_REQUESTED_LATENCY,
    PA_SINK_MESSAGE_SET_STATE,
    PA_SINK_MESSAGE_REMOVE_INPUT_AND_BUFFER,
    PA_SINK_MESSAGE_ATTACH,
    PA_SINK_MESSAGE_DETACH,
    PA_SINK_MESSAGE_MAX
} pa_sink_message_t;

typedef struct pa_sink_new_data {
    char *name;
    pa_bool_t namereg_fail;
    pa_proplist *proplist;

    const char *driver;
    pa_module *module;

    pa_sample_spec sample_spec;
    pa_bool_t sample_spec_is_set;
    pa_channel_map channel_map;
    pa_bool_t channel_map_is_set;

    pa_cvolume volume;
    pa_bool_t volume_is_set;
    pa_bool_t muted;
    pa_bool_t muted_is_set;
} pa_sink_new_data;

pa_sink_new_data* pa_sink_new_data_init(pa_sink_new_data *data);
void pa_sink_new_data_set_name(pa_sink_new_data *data, const char *name);
void pa_sink_new_data_set_sample_spec(pa_sink_new_data *data, const pa_sample_spec *spec);
void pa_sink_new_data_set_channel_map(pa_sink_new_data *data, const pa_channel_map *map);
void pa_sink_new_data_set_volume(pa_sink_new_data *data, const pa_cvolume *volume);
void pa_sink_new_data_set_muted(pa_sink_new_data *data, pa_bool_t mute);
void pa_sink_new_data_done(pa_sink_new_data *data);

/* To be called exclusively by the sink driver, from main context */

pa_sink* pa_sink_new(
        pa_core *core,
        pa_sink_new_data *data,
        pa_sink_flags_t flags);

void pa_sink_put(pa_sink *s);
void pa_sink_unlink(pa_sink* s);

void pa_sink_set_description(pa_sink *s, const char *description);
void pa_sink_set_asyncmsgq(pa_sink *s, pa_asyncmsgq *q);
void pa_sink_set_rtpoll(pa_sink *s, pa_rtpoll *p);

void pa_sink_detach(pa_sink *s);
void pa_sink_attach(pa_sink *s);

/* May be called by everyone, from main context */

/* The returned value is supposed to be in the time domain of the sound card! */
pa_usec_t pa_sink_get_latency(pa_sink *s);
pa_usec_t pa_sink_get_requested_latency(pa_sink *s);

int pa_sink_update_status(pa_sink*s);
int pa_sink_suspend(pa_sink *s, pa_bool_t suspend);
int pa_sink_suspend_all(pa_core *c, pa_bool_t suspend);

void pa_sink_set_volume(pa_sink *sink, const pa_cvolume *volume);
const pa_cvolume *pa_sink_get_volume(pa_sink *sink);
void pa_sink_set_mute(pa_sink *sink, pa_bool_t mute);
pa_bool_t pa_sink_get_mute(pa_sink *sink);

unsigned pa_sink_linked_by(pa_sink *s); /* Number of connected streams */
unsigned pa_sink_used_by(pa_sink *s); /* Number of connected streams which are not corked */
#define pa_sink_get_state(s) ((s)->state)

/* To be called exclusively by the sink driver, from IO context */

void pa_sink_render(pa_sink*s, size_t length, pa_memchunk *result);
void pa_sink_render_full(pa_sink *s, size_t length, pa_memchunk *result);
void pa_sink_render_into(pa_sink*s, pa_memchunk *target);
void pa_sink_render_into_full(pa_sink *s, pa_memchunk *target);

void pa_sink_process_rewind(pa_sink *s, size_t nbytes);

int pa_sink_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);

void pa_sink_attach_within_thread(pa_sink *s);
void pa_sink_detach_within_thread(pa_sink *s);

pa_usec_t pa_sink_get_requested_latency_within_thread(pa_sink *s);

void pa_sink_set_max_rewind(pa_sink *s, size_t max_rewind);

/* To be called exclusively by sink input drivers, from IO context */

void pa_sink_request_rewind(pa_sink*s, size_t nbytes);

void pa_sink_invalidate_requested_latency(pa_sink *s);

#endif
