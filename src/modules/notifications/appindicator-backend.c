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

#include <pulsecore/core.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core.h>
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>

#include "appindicator-backend.h"
#include "dn-backend.h"
#include "notification.h"
#include "notification-backend.h"

#define APPINDICATOR_DBUS_NAME "org.PulseAudio.AppIndicatorServer"
#define APPINDICATOR_DBUS_OBJECT "/org/PulseAudio/AppIndicatorServer"
#define APPINDICATOR_DBUS_INTERFACE "org.PulseAudio.AppIndicatorServer"

typedef struct appindicator_backend_userdata {
    pa_ui_notification_backend *dn_backend;
    pa_dbus_connection *conn;
    pa_hashmap *displaying;
    pa_idxset *handled;
    bool filter_set;
} appindicator_backend_userdata;

static void clear_all_actions(appindicator_backend_userdata *u) {
    DBusConnection *conn;
    DBusMessage *msg;

    pa_assert(u);

    conn = pa_dbus_connection_get(u->conn);
    pa_assert_se(msg = dbus_message_new_method_call(APPINDICATOR_DBUS_NAME, APPINDICATOR_DBUS_OBJECT, APPINDICATOR_DBUS_INTERFACE, "ClearAllActions"));

    dbus_message_set_no_reply(msg, TRUE);
    pa_dbus_send_message(conn, msg, NULL, NULL, NULL);
    dbus_message_unref(msg);
}

static void clear_actions(appindicator_backend_userdata *u, pa_ui_notification *notification) {
    DBusConnection *conn;
    DBusMessage *msg;

    pa_assert(u);
    pa_assert(notification);

    conn = pa_dbus_connection_get(u->conn);
    pa_assert_se(msg = dbus_message_new_method_call(APPINDICATOR_DBUS_NAME, APPINDICATOR_DBUS_OBJECT, APPINDICATOR_DBUS_INTERFACE, "ClearActions"));

    pa_assert_se(dbus_message_append_args(msg, DBUS_TYPE_STRING, &notification->title, DBUS_TYPE_INVALID));

    dbus_message_set_no_reply(msg, TRUE);
    pa_dbus_send_message(conn, msg, NULL, NULL, NULL);
    dbus_message_unref(msg);

    pa_hashmap_remove(u->displaying, notification->title);
}

static void appindicator_send_notification(pa_ui_notification_backend *backend, pa_ui_notification *notification) {
    appindicator_backend_userdata *u;
    char *key, *value;
    void *state;

    pa_assert(backend);
    pa_assert(notification);
    pa_assert(u = backend->userdata);

    if (!pa_hashmap_isempty(notification->actions)) {
        DBusConnection *conn;
        DBusMessage *msg;
        DBusMessageIter args, dict_iter;

        conn = pa_dbus_connection_get(u->conn);

        pa_assert_se(msg = dbus_message_new_method_call(APPINDICATOR_DBUS_NAME, APPINDICATOR_DBUS_OBJECT, APPINDICATOR_DBUS_INTERFACE, "ShowActions"));

        dbus_message_iter_init_append(msg, &args);

        pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &notification->title));

        pa_assert_se(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{ss}", &dict_iter));

        PA_HASHMAP_FOREACH_KEY(value, key, notification->actions, state) {
            pa_dbus_append_basic_dict_entry(&dict_iter, key, DBUS_TYPE_STRING, (void *) &value);
        }

        pa_assert_se(dbus_message_iter_close_container(&args, &dict_iter));

        dbus_message_set_no_reply(msg, TRUE);
        pa_dbus_send_message(conn, msg, NULL, NULL, NULL);
        dbus_message_unref(msg);

        pa_hashmap_put(u->displaying, notification->title, notification);
    }

    send_notification(u->dn_backend, notification, false);
}

static void appindicator_cancel_notification(pa_ui_notification_backend *backend, pa_ui_notification *notification) {
    appindicator_backend_userdata *u;

    pa_assert(backend);
    pa_assert(notification);

    pa_assert(u = backend->userdata);

    cancel_notification(u->dn_backend, notification);
}

static void handle_reply(pa_ui_notification_reply *reply, void *userdata) {
    appindicator_backend_userdata *u;

    pa_assert(reply);
    pa_assert(u = userdata);

    if (pa_idxset_remove_by_data(u->handled, reply->source->title, NULL) == NULL) {
        clear_actions(u, reply->source);
        reply->source->reply_cb(reply);
    }
}

static DBusHandlerResult signal_cb(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    DBusError err;
    pa_ui_notification_backend *backend;
    pa_ui_notification *notification;
    appindicator_backend_userdata *u;
    char *title, *action_key;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(backend = userdata);
    pa_assert(u = backend->userdata);

    dbus_error_init(&err);

    if (dbus_message_is_signal(msg, APPINDICATOR_DBUS_INTERFACE, "ActionInvoked")) {
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &title, DBUS_TYPE_STRING, &action_key, DBUS_TYPE_INVALID)) {
            pa_log_error("Failed to parse " APPINDICATOR_DBUS_INTERFACE ".NotificationClosed: %s.", err.message);
            goto finish;
        }

        if ((notification = pa_hashmap_remove(u->displaying, title)) != NULL) {
            pa_idxset_put(u->handled, title, NULL);
            pa_assert(pa_idxset_get_by_data(u->handled, title, NULL));

            u->dn_backend->cancel_notification(u->dn_backend, notification);
            notification->reply_cb(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ACTION_INVOKED, notification, action_key));
        }
    }

finish:
    dbus_error_free(&err);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

pa_ui_notification_backend* appindicator_backend_new(pa_core *core) {
    DBusError err;
    pa_ui_notification_backend *backend;
    appindicator_backend_userdata *u;
    dn_backend_userdata *dn_u;

    pa_assert(core);

    backend = pa_xnew(pa_ui_notification_backend, 1);
    backend->userdata = u = pa_xnew(appindicator_backend_userdata, 1);

    if (!(u->dn_backend = dn_backend_new_with_reply_handle(core, handle_reply, (void *) u)))
        goto fail;

    dn_u = u->dn_backend->userdata;
    u->conn = dn_u->conn;

    u->displaying = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    u->handled = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    if (!dbus_connection_add_filter(pa_dbus_connection_get(u->conn), signal_cb, backend, NULL)) {
        pa_log_error("Failed to add filter function.");
        goto fail;
    }
    u->filter_set = true;

    dbus_error_init(&err);
    if (pa_dbus_add_matches(pa_dbus_connection_get(u->conn), &err,
        "type='signal',sender='" APPINDICATOR_DBUS_NAME
        "',interface='" APPINDICATOR_DBUS_INTERFACE
        "',path='" APPINDICATOR_DBUS_OBJECT
        "'", NULL) < 0) {
        pa_log("Failed to add D-Bus matches: %s", err.message);
        goto fail;
    }

    backend->send_notification = appindicator_send_notification;
    backend->cancel_notification = appindicator_cancel_notification;

    return backend;

fail:
    appindicator_backend_free(backend);
    return NULL;
}

void appindicator_backend_free(pa_ui_notification_backend *backend) {
    appindicator_backend_userdata *u;

    pa_assert(backend);
    pa_assert(u = backend->userdata);

    clear_all_actions(u);

    if (u->displaying)
        pa_hashmap_free(u->displaying, NULL, NULL);

    if (u->handled)
        pa_idxset_free(u->handled, NULL, NULL);

    if (u->filter_set)
        dbus_connection_remove_filter(pa_dbus_connection_get(u->conn), signal_cb, backend);

    pa_dbus_remove_matches(pa_dbus_connection_get(u->conn),
        "type='signal',sender='" APPINDICATOR_DBUS_NAME
        "',interface='" APPINDICATOR_DBUS_INTERFACE
        "',path='" APPINDICATOR_DBUS_OBJECT
        "'", NULL);

    if (u->dn_backend)
        dn_backend_free(u->dn_backend);

    pa_xfree(backend);
}
