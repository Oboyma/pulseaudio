

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

#include <pulsecore/core.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/shared.h>

#include "notification.h"
#include "notification-backend.h"
#include "notification-manager.h"

struct pa_ui_notification_manager {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_ui_notification_backend *backend;
};

static pa_ui_notification_manager* pa_ui_notification_manager_new(pa_core *c) {
    pa_ui_notification_manager *m;

    pa_assert(c);

    m = pa_xnew(pa_ui_notification_manager, 1);
    PA_REFCNT_INIT(m);

    m->core =  c;
    m->backend = NULL;

    pa_assert_se(pa_shared_set(c, "ui-notification-manager", m) >= 0);

    return m;
}

static void pa_ui_notification_manager_free(pa_ui_notification_manager *m) {
    pa_assert(m);

    pa_assert_se(pa_shared_remove(m->core, "ui-notification-manager") >= 0);

    pa_xfree(m);
}

pa_ui_notification_manager* pa_ui_notification_manager_get(pa_core *c) {
    pa_ui_notification_manager *m;

    if((m = pa_shared_get(c, "ui-notification-manager")))
        return pa_ui_notification_manager_ref(m);

    return pa_ui_notification_manager_new(c);
}

pa_ui_notification_manager* pa_ui_notification_manager_ref(pa_ui_notification_manager *m) {
    pa_assert(m);
    pa_assert(PA_REFCNT_VALUE(m) >= 1);

    PA_REFCNT_INC(m);

    return m;
}

void pa_ui_notification_manager_unref(pa_ui_notification_manager *m) {
    pa_assert(m);
    pa_assert(PA_REFCNT_VALUE(m) >= 1);

    if(PA_REFCNT_DEC(m) <= 0)
        pa_ui_notification_manager_free(m);
}

int pa_ui_notification_manager_register_backend(pa_ui_notification_manager *m, pa_ui_notification_backend *b) {
    pa_assert(m);
    pa_assert(b);

    if(m->backend != NULL) {
        pa_log_error("A UI notification backend is already registered.");
        return -1;
    }

    m->backend = b;

    return 0;
}

void pa_ui_notification_manager_unregister_backend(pa_ui_notification_manager *m) {
    pa_assert(m);

    m->backend = NULL;
}

pa_ui_notification_backend* pa_ui_notification_manager_get_backend(pa_ui_notification_manager *m) {
    pa_assert(m);

    return m->backend;
}

int pa_ui_notification_manager_send(pa_ui_notification_manager *m, pa_ui_notification *n) {
    pa_assert(m);
    pa_assert(n);

    if(m->backend == NULL) {
        pa_log_error("No UI notification backend is registered.");
        return -1;
    }

    m->backend->send_notification(m->backend, n);

    return 0;
}

int pa_ui_notification_manager_cancel(pa_ui_notification_manager *m, pa_ui_notification *n) {
    pa_assert(m);
    pa_assert(n);

    if(m->backend == NULL) {
        pa_log_error("No UI notification backend is registered.");
        return -1;
    }

    m->backend->cancel_notification(m->backend, n);

    return 0;
}
