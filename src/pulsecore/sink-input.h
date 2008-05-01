#ifndef foopulsesinkinputhfoo
#define foopulsesinkinputhfoo

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

#include <inttypes.h>

typedef struct pa_sink_input pa_sink_input;

#include <pulse/sample.h>
#include <pulsecore/hook-list.h>
#include <pulsecore/memblockq.h>
#include <pulsecore/resampler.h>
#include <pulsecore/module.h>
#include <pulsecore/client.h>
#include <pulsecore/sink.h>
#include <pulsecore/core.h>

typedef enum pa_sink_input_state {
    PA_SINK_INPUT_INIT,         /*< The stream is not active yet, because pa_sink_put() has not been called yet */
    PA_SINK_INPUT_DRAINED,      /*< The stream stopped playing because there was no data to play */
    PA_SINK_INPUT_RUNNING,      /*< The stream is alive and kicking */
    PA_SINK_INPUT_CORKED,       /*< The stream was corked on user request */
    PA_SINK_INPUT_UNLINKED      /*< The stream is dead */
} pa_sink_input_state_t;

static inline pa_bool_t PA_SINK_INPUT_IS_LINKED(pa_sink_input_state_t x) {
    return x == PA_SINK_INPUT_DRAINED || x == PA_SINK_INPUT_RUNNING || x == PA_SINK_INPUT_CORKED;
}

typedef enum pa_sink_input_flags {
    PA_SINK_INPUT_VARIABLE_RATE = 1,
    PA_SINK_INPUT_DONT_MOVE = 2,
    PA_SINK_INPUT_START_CORKED = 4,
    PA_SINK_INPUT_NO_REMAP = 8,
    PA_SINK_INPUT_NO_REMIX = 16,
    PA_SINK_INPUT_FIX_FORMAT = 32,
    PA_SINK_INPUT_FIX_RATE = 64,
    PA_SINK_INPUT_FIX_CHANNELS = 128
} pa_sink_input_flags_t;

struct pa_sink_input {
    pa_msgobject parent;

    uint32_t index;
    pa_core *core;

    /* Please note that this state should only be read with
     * pa_sink_input_get_state(). That function will transparently
     * merge the thread_info.drained value in. */
    pa_sink_input_state_t state;
    pa_sink_input_flags_t flags;

    pa_proplist *proplist;
    char *driver;                       /* may be NULL */
    pa_module *module;                  /* may be NULL */
    pa_client *client;                  /* may be NULL */

    pa_sink *sink;

    pa_sample_spec sample_spec;
    pa_channel_map channel_map;

    pa_sink_input *sync_prev, *sync_next;

    pa_cvolume volume;
    pa_bool_t muted;

    pa_resample_method_t resample_method;

    /* Returns the chunk of audio data and drops it from the
     * queue. Returns -1 on failure. Called from IO thread context. If
     * data needs to be generated from scratch then please in the
     * specified length request_nbytes. This is an optimization
     * only. If less data is available, it's fine to return a smaller
     * block. If more data is already ready, it is better to return
     * the full block. */
    int (*pop) (pa_sink_input *i, size_t request_nbytes, pa_memchunk *chunk); /* may NOT be NULL */

    /* Rewind the queue by the specified number of bytes. Called just
     * before peek() if it is called at all. Only called if the sink
     * input driver ever plans to call
     * pa_sink_input_request_rewind(). Called from IO context. */
    void (*process_rewind) (pa_sink_input *i, size_t nbytes);     /* may NOT be NULL */

    /* Called whenever the maximum rewindable size of the sink
     * changes. Called from IO context. */
    void (*update_max_rewind) (pa_sink_input *i, size_t nbytes); /* may be NULL */

    /* If non-NULL this function is called when the input is first
     * connected to a sink or when the rtpoll/asyncmsgq fields
     * change. You usually don't need to implement this function
     * unless you rewrite a sink that is piggy-backed onto
     * another. Called from IO thread context */
    void (*attach) (pa_sink_input *i);           /* may be NULL */

    /* If non-NULL this function is called when the output is
     * disconnected from its sink. Called from IO thread context */
    void (*detach) (pa_sink_input *i);           /* may be NULL */

    /* If non-NULL called whenever the the sink this input is attached
     * to suspends or resumes. Called from main context */
    void (*suspend) (pa_sink_input *i, pa_bool_t b);   /* may be NULL */

    /* If non-NULL called whenever the the sink this input is attached
     * to changes. Called from main context */
    void (*moved) (pa_sink_input *i);   /* may be NULL */

    /* Supposed to unlink and destroy this stream. Called from main
     * context. */
    void (*kill) (pa_sink_input *i);             /* may be NULL */

    /* Return the current latency (i.e. length of bufferd audio) of
    this stream. Called from main context. If NULL a
    PA_SINK_INPUT_MESSAGE_GET_LATENCY message is sent to the IO thread
    instead. */
    pa_usec_t (*get_latency) (pa_sink_input *i); /* may be NULL */

    /* If non_NULL this function is called from thread context if the
     * state changes. The old state is found in thread_info.state.  */
    void (*state_change) (pa_sink_input *i, pa_sink_input_state_t state); /* may be NULL */

    struct {
        pa_sink_input_state_t state;
        pa_atomic_t drained, render_memblockq_is_empty;

        pa_bool_t attached; /* True only between ->attach() and ->detach() calls */

        pa_sample_spec sample_spec;

        pa_resampler *resampler;                     /* may be NULL */

        /* We maintain a history of resampled audio data here. */
        pa_memblockq *render_memblockq;

        size_t rewrite_nbytes;
        uint64_t underrun_for, playing_for;
        pa_bool_t ignore_rewind;

        pa_sink_input *sync_prev, *sync_next;

        pa_cvolume volume;
        pa_bool_t muted;

        /* The requested latency for the sink */
        pa_usec_t requested_sink_latency;
    } thread_info;

    void *userdata;
};

PA_DECLARE_CLASS(pa_sink_input);
#define PA_SINK_INPUT(o) pa_sink_input_cast(o)

enum {
    PA_SINK_INPUT_MESSAGE_SET_VOLUME,
    PA_SINK_INPUT_MESSAGE_SET_MUTE,
    PA_SINK_INPUT_MESSAGE_GET_LATENCY,
    PA_SINK_INPUT_MESSAGE_SET_RATE,
    PA_SINK_INPUT_MESSAGE_SET_STATE,
    PA_SINK_INPUT_MESSAGE_SET_REQUESTED_LATENCY,
    PA_SINK_INPUT_MESSAGE_MAX
};

typedef struct pa_sink_input_new_data {
    pa_proplist *proplist;

    const char *driver;
    pa_module *module;
    pa_client *client;

    pa_sink *sink;

    pa_sample_spec sample_spec;
    pa_bool_t sample_spec_is_set;
    pa_channel_map channel_map;
    pa_bool_t channel_map_is_set;

    pa_cvolume volume;
    pa_bool_t volume_is_set;
    pa_bool_t muted;
    pa_bool_t muted_is_set;

    pa_resample_method_t resample_method;

    pa_sink_input *sync_base;
} pa_sink_input_new_data;

pa_sink_input_new_data* pa_sink_input_new_data_init(pa_sink_input_new_data *data);
void pa_sink_input_new_data_set_sample_spec(pa_sink_input_new_data *data, const pa_sample_spec *spec);
void pa_sink_input_new_data_set_channel_map(pa_sink_input_new_data *data, const pa_channel_map *map);
void pa_sink_input_new_data_set_volume(pa_sink_input_new_data *data, const pa_cvolume *volume);
void pa_sink_input_new_data_set_muted(pa_sink_input_new_data *data, pa_bool_t mute);
void pa_sink_input_new_data_done(pa_sink_input_new_data *data);

typedef struct pa_sink_input_move_hook_data {
    pa_sink_input *sink_input;
    pa_sink *destination;
} pa_sink_input_move_hook_data;

/* To be called by the implementing module only */

pa_sink_input* pa_sink_input_new(
        pa_core *core,
        pa_sink_input_new_data *data,
        pa_sink_input_flags_t flags);

void pa_sink_input_put(pa_sink_input *i);
void pa_sink_input_unlink(pa_sink_input* i);

void pa_sink_input_set_name(pa_sink_input *i, const char *name);

pa_usec_t pa_sink_input_set_requested_latency(pa_sink_input *i, pa_usec_t usec);

/* Request that the specified number of bytes already written out to
the hw device is rewritten, if possible.  Please note that this is
only a kind request. The sink driver may not be able to fulfill it
fully -- or at all. If the request for a rewrite was successful, the
sink driver will call ->rewind() and pass the number of bytes that
could be rewound in the HW device. This functionality is required for
implementing the "zero latency" write-through functionality. */
void pa_sink_input_request_rewind(pa_sink_input *i, size_t nbytes, pa_bool_t ignore_rewind, pa_bool_t not_here);

/* Callable by everyone from main thread*/

/* External code may request disconnection with this function */
void pa_sink_input_kill(pa_sink_input*i);

pa_usec_t pa_sink_input_get_latency(pa_sink_input *i);

void pa_sink_input_set_volume(pa_sink_input *i, const pa_cvolume *volume);
const pa_cvolume *pa_sink_input_get_volume(pa_sink_input *i);
void pa_sink_input_set_mute(pa_sink_input *i, pa_bool_t mute);
int pa_sink_input_get_mute(pa_sink_input *i);

void pa_sink_input_cork(pa_sink_input *i, pa_bool_t b);

int pa_sink_input_set_rate(pa_sink_input *i, uint32_t rate);

pa_resample_method_t pa_sink_input_get_resample_method(pa_sink_input *i);

int pa_sink_input_move_to(pa_sink_input *i, pa_sink *dest, pa_bool_t immediately);

pa_sink_input_state_t pa_sink_input_get_state(pa_sink_input *i);

pa_bool_t pa_sink_input_safe_to_remove(pa_sink_input *i);
/* To be used exclusively by the sink driver IO thread */

int pa_sink_input_peek(pa_sink_input *i, size_t length, pa_memchunk *chunk, pa_cvolume *volume);
void pa_sink_input_drop(pa_sink_input *i, size_t length);
void pa_sink_input_process_rewind(pa_sink_input *i, size_t nbytes /* in the sink's sample spec */);
void pa_sink_input_update_max_rewind(pa_sink_input *i, size_t nbytes  /* in the sink's sample spec */);

void pa_sink_input_set_state_within_thread(pa_sink_input *i, pa_sink_input_state_t state);

int pa_sink_input_process_msg(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);

pa_usec_t pa_sink_input_set_requested_latency_within_thread(pa_sink_input *i, pa_usec_t usec);

typedef struct pa_sink_input_move_info {
    pa_sink_input *sink_input;
    pa_sink_input *ghost_sink_input;
    pa_memblockq *buffer;
    size_t buffer_bytes;
} pa_sink_input_move_info;

pa_memchunk* pa_sink_input_get_silence(pa_sink_input *i, pa_memchunk *ret);

#endif
