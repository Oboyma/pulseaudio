/* $Id$ */

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sndfile.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/log.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sample-util.h>

#include "sound-file-stream.h"

#define MEMBLOCKQ_MAXLENGTH (16*1024*1024)

typedef struct file_stream {
    pa_msgobject parent;
    pa_core *core;
    pa_sink_input *sink_input;

    SNDFILE *sndfile;
    sf_count_t (*readf_function)(SNDFILE *sndfile, void *ptr, sf_count_t frames);

    /* We need this memblockq here to easily fulfill rewind requests
     * (even beyond the file start!) */
    pa_memblockq *memblockq;
} file_stream;

enum {
    FILE_STREAM_MESSAGE_UNLINK
};

PA_DECLARE_CLASS(file_stream);
#define FILE_STREAM(o) (file_stream_cast(o))
static PA_DEFINE_CHECK_TYPE(file_stream, pa_msgobject);

/* Called from main context */
static void file_stream_unlink(file_stream *u) {
    pa_assert(u);

    if (!u->sink_input)
        return;

    pa_sink_input_unlink(u->sink_input);
    pa_sink_input_unref(u->sink_input);
    u->sink_input = NULL;

    /* Make sure we don't decrease the ref count twice. */
    file_stream_unref(u);
}

/* Called from main context */
static void file_stream_free(pa_object *o) {
    file_stream *u = FILE_STREAM(o);
    pa_assert(u);

    if (u->memblockq)
        pa_memblockq_free(u->memblockq);

    if (u->sndfile)
        sf_close(u->sndfile);

    pa_xfree(u);
}

/* Called from main context */
static int file_stream_process_msg(pa_msgobject *o, int code, void*userdata, int64_t offset, pa_memchunk *chunk) {
    file_stream *u = FILE_STREAM(o);
    file_stream_assert_ref(u);

    switch (code) {
        case FILE_STREAM_MESSAGE_UNLINK:
            file_stream_unlink(u);
            break;
    }

    return 0;
}

/* Called from main context */
static void sink_input_kill_cb(pa_sink_input *i) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    file_stream_unlink(u);
}

/* Called from IO thread context */
static void sink_input_state_change_cb(pa_sink_input *i, pa_sink_input_state_t state) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    /* If we are added for the first time, ask for a rewinding so that
     * we are heard right-away. */
    if (PA_SINK_INPUT_IS_LINKED(state) &&
        i->thread_info.state == PA_SINK_INPUT_INIT)
        pa_sink_input_request_rewind(i, 0, FALSE, TRUE);
}

/* Called from IO thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t length, pa_memchunk *chunk) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    pa_assert(chunk);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return -1;

    pa_log_debug("pop: %lu", (unsigned long) length);

    for (;;) {
        pa_memchunk tchunk;
        size_t fs;
        void *p;
        sf_count_t n;

        if (pa_memblockq_peek(u->memblockq, chunk) >= 0) {
            pa_memblockq_drop(u->memblockq, chunk->length);
            return 0;
        }

        if (!u->sndfile)
            break;

        tchunk.memblock = pa_memblock_new(i->sink->core->mempool, length);
        tchunk.index = 0;

        p = pa_memblock_acquire(tchunk.memblock);

        if (u->readf_function) {
            fs = pa_frame_size(&i->sample_spec);
            n = u->readf_function(u->sndfile, p, length/fs);
        } else {
            fs = 1;
            n = sf_read_raw(u->sndfile, p, length);
        }

        pa_memblock_release(tchunk.memblock);

        if (n <= 0) {
            pa_memblock_unref(tchunk.memblock);

            sf_close(u->sndfile);
            u->sndfile = NULL;
            break;
        }

        tchunk.length = n * fs;

        pa_memblockq_push(u->memblockq, &tchunk);
        pa_memblock_unref(tchunk.memblock);
    }

    pa_log_debug("peek fail");

    if (pa_sink_input_safe_to_remove(i)) {
        pa_log_debug("completed to play");

        pa_memblockq_free(u->memblockq);
        u->memblockq = NULL;

        pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(u), FILE_STREAM_MESSAGE_UNLINK, NULL, 0, NULL, NULL);
    }

    return -1;
 }

static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    pa_assert(nbytes > 0);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_rewind(u->memblockq, nbytes);
}

static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    file_stream *u;

    pa_sink_input_assert_ref(i);
    u = FILE_STREAM(i->userdata);
    file_stream_assert_ref(u);

    if (!u->memblockq)
        return;

    pa_memblockq_set_maxrewind(u->memblockq, nbytes);
}

int pa_play_file(
        pa_sink *sink,
        const char *fname,
        const pa_cvolume *volume) {

    file_stream *u = NULL;
    SF_INFO sfinfo;
    pa_sample_spec ss;
    pa_sink_input_new_data data;
    int fd;
    pa_memchunk silence;

    pa_assert(sink);
    pa_assert(fname);

    u = pa_msgobject_new(file_stream);
    u->parent.parent.free = file_stream_free;
    u->parent.process_msg = file_stream_process_msg;
    u->core = sink->core;
    u->sink_input = NULL;
    u->sndfile = NULL;
    u->readf_function = NULL;
    u->memblockq = NULL;

    memset(&sfinfo, 0, sizeof(sfinfo));

    if ((fd = open(fname, O_RDONLY
#ifdef O_NOCTTY
                   |O_NOCTTY
#endif
                   )) < 0) {
        pa_log("Failed to open file %s: %s", fname, pa_cstrerror(errno));
        goto fail;
    }

    /* FIXME: For now we just use posix_fadvise to avoid page faults
     * when accessing the file data. Eventually we should move the
     * file reader into the main event loop and pass the data over the
     * asyncmsgq. */

#ifdef HAVE_POSIX_FADVISE
    if (posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL) < 0) {
        pa_log_warn("POSIX_FADV_SEQUENTIAL failed: %s", pa_cstrerror(errno));
        goto fail;
    } else
        pa_log_debug("POSIX_FADV_SEQUENTIAL succeeded.");

    if (posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED) < 0) {
        pa_log_warn("POSIX_FADV_WILLNEED failed: %s", pa_cstrerror(errno));
        goto fail;
    } else
        pa_log_debug("POSIX_FADV_WILLNEED succeeded.");
#endif

    if (!(u->sndfile = sf_open_fd(fd, SFM_READ, &sfinfo, 1))) {
        pa_log("Failed to open file %s", fname);
        pa_close(fd);
        goto fail;
    }

    switch (sfinfo.format & 0xFF) {
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_U8:
        case SF_FORMAT_PCM_S8:
            ss.format = PA_SAMPLE_S16NE;
            u->readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_short;
            break;

        case SF_FORMAT_ULAW:
            ss.format = PA_SAMPLE_ULAW;
            break;

        case SF_FORMAT_ALAW:
            ss.format = PA_SAMPLE_ALAW;
            break;

        case SF_FORMAT_FLOAT:
        default:
            ss.format = PA_SAMPLE_FLOAT32NE;
            u->readf_function = (sf_count_t (*)(SNDFILE *sndfile, void *ptr, sf_count_t frames)) sf_readf_float;
            break;
    }

    ss.rate = sfinfo.samplerate;
    ss.channels = sfinfo.channels;

    if (!pa_sample_spec_valid(&ss)) {
        pa_log("Unsupported sample format in file %s", fname);
        goto fail;
    }

    pa_sink_input_new_data_init(&data);
    data.sink = sink;
    data.driver = __FILE__;
    pa_sink_input_new_data_set_sample_spec(&data, &ss);
    pa_sink_input_new_data_set_volume(&data, volume);
    pa_proplist_sets(data.proplist, PA_PROP_MEDIA_NAME, fname);
    pa_proplist_sets(data.proplist, PA_PROP_MEDIA_FILENAME, fname);

    u->sink_input = pa_sink_input_new(sink->core, &data, 0);
    pa_sink_input_new_data_done(&data);

    if (!u->sink_input)
        goto fail;

    u->sink_input->pop = sink_input_pop_cb;
    u->sink_input->process_rewind = sink_input_process_rewind_cb;
    u->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    u->sink_input->kill = sink_input_kill_cb;
    u->sink_input->state_change = sink_input_state_change_cb;
    u->sink_input->userdata = u;

    pa_sink_input_get_silence(u->sink_input, &silence);
    u->memblockq = pa_memblockq_new(0, MEMBLOCKQ_MAXLENGTH, 0, pa_frame_size(&u->sink_input->sample_spec), 1, 1, 0, &silence);
    pa_memblock_unref(silence.memblock);

    pa_sink_input_put(u->sink_input);

    /* The reference to u is dangling here, because we want to keep
     * this stream around until it is fully played. */

    return 0;

fail:
    if (u)
        file_stream_unref(u);

    return -1;
}
