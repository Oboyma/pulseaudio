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

#include "dn-backend.h"
#include "notification-backend.h"
#include "notification-manager.h"

#include "module-ui-notification-dn-backend-symdef.h"

PA_MODULE_AUTHOR("Ștefan Săftescu");
PA_MODULE_DESCRIPTION("Freedesktop D-Bus notifications (Desktop Notifications) backend.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

struct userdata {
    pa_ui_notification_manager *manager;
    pa_ui_notification_backend *backend;
};

int pa__init(pa_module*m) {
    pa_ui_notification_backend *backend;
    pa_ui_notification_manager *manager;
    struct userdata *u;

    pa_assert(m);

    if((backend = dn_backend_new(m->core)) == NULL)
        goto fail;

    m->userdata = u = pa_xnew(struct userdata, 1);
    u->manager = manager = pa_ui_notification_manager_get(m->core);
    u->backend = backend;

    if(pa_ui_notification_manager_register_backend(manager, backend) >= 0)
        return 0;

fail:
    pa__done(m);
    return -1;
}

void pa__done(pa_module*m) {
    pa_ui_notification_backend *backend;
    struct userdata *u;

    pa_assert(m);
    pa_assert(u = m->userdata);

    backend = u->backend;
    if ((u = m->userdata)) {
        if(pa_ui_notification_manager_get_backend(u->manager) == backend)
            pa_ui_notification_manager_unregister_backend(u->manager);

        pa_ui_notification_manager_unref(u->manager);
    }

    if(backend)
        dn_backend_free(backend);
}
