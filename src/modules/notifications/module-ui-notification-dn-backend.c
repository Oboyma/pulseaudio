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

#include <dbus/dbus.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core.h>
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/llist.h>

#include "notification-backend.h"
#include "notification-manager.h"

#include "module-ui-notification-dn-backend-symdef.h"

PA_MODULE_AUTHOR("Ștefan Săftescu");
PA_MODULE_DESCRIPTION("Freedesktop D-Bus notifications (Desktop Notifications) backend.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

/* The userdata for the backend. */
struct backend_userdata {
    char *app_name;
    char *app_icon;

    pa_dbus_connection *conn;
    PA_LLIST_HEAD(pa_dbus_pending, pending);
};

struct module_userdata {
    pa_ui_notification_manager *manager;
    pa_ui_notification_backend *backend;
};

static void send_notification(pa_ui_notification_backend *b, pa_ui_notification *n) {
    DBusConnection* conn;
    DBusMessage* msg;
    DBusMessageIter args, dict_iter;
    DBusPendingCall* pending;
    pa_dbus_pending *p;
    struct backend_userdata *u;

    u = b->userdata;
    conn = pa_dbus_connection_get(u->conn);

    msg = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");

    /* TODO: msg != NULL */
    dbus_message_iter_init_append(msg, &args);

    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &u->app_name));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, (void *) &n->replaces_id));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &u->app_icon));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &n->summary));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &n->body));

    pa_dbus_append_basic_array(&args, DBUS_TYPE_STRING, (void *) n->actions, n->num_actions);


    pa_assert_se(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));
    /* TODO: hints */
    pa_assert_se(dbus_message_iter_close_container(&args, &dict_iter));

    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, (void *) &n->expire_timeout));

    pa_assert_se(dbus_connection_send_with_reply(conn, msg, &pending, -1));

    pa_log_debug("D-Bus message sent.");

    /* TODO: pending != NULL */

    dbus_connection_flush(conn);
    p = pa_dbus_pending_new(conn, msg, pending, NULL, NULL);

    /* TODO: set callback */

    PA_LLIST_PREPEND(pa_dbus_pending, u->pending, p);
}

static void cancel_notification(pa_ui_notification_backend *b, pa_ui_notification *n) {
    /* TODO */
}

int pa__init(pa_module*m) {
    DBusError err;
    pa_ui_notification_backend *backend;
    pa_ui_notification_manager *manager;
    struct backend_userdata *u;
    struct module_userdata *mu;

    pa_assert(m);

    backend = pa_xnew(pa_ui_notification_backend, 1);
    backend->userdata = u = pa_xnew(struct backend_userdata, 1);

    dbus_error_init(&err);

    u->app_name = "PulseAudio";
    u->app_icon = "";
    u->conn = pa_dbus_bus_get(m->core, DBUS_BUS_SESSION, &err);

    /* TODO: error checking */

    PA_LLIST_HEAD_INIT(pa_dbus_pending, u->pending);

    backend->send_notification = send_notification;
    backend->cancel_notification = cancel_notification;

    dbus_error_free(&err);

    m->userdata = mu = pa_xnew(struct module_userdata, 1);
    mu->manager = manager = pa_ui_notification_manager_get(m->core);
    mu->backend = backend;

    if(pa_ui_notification_manager_register_backend(manager, backend) >= 0)
        return 0;
    else {
        pa__done(m);
        return -1;
    }
}

void pa__done(pa_module*m) {
    pa_ui_notification_backend *b;
    struct backend_userdata *u;
    struct module_userdata *mu;

    pa_assert(m);

    b = NULL;
    if((mu = m->userdata)) {
        if((b = pa_ui_notification_manager_get_backend(mu->manager)) == mu->backend)
            pa_ui_notification_manager_unregister_backend(mu->manager);

        pa_ui_notification_manager_unref(mu->manager);
    }

    if(b) {
        u = b->userdata;

        if(u) {
            pa_dbus_free_pending_list(&u->pending);
            pa_dbus_connection_unref(u->conn);
        }

        pa_xfree(u);
        pa_xfree(b);
    }
}
