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
#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/llist.h>

#include "dn-backend.h"
#include "notification.h"
#include "notification-backend.h"

static inline void cancel_notification_dbus(pa_ui_notification_backend *backend, pa_ui_notification *notification, unsigned *dbus_notification_id) {
    DBusConnection *conn;
    DBusMessage *msg;
    dn_backend_userdata *u;

    pa_assert(backend);
    pa_assert(notification);
    pa_assert(dbus_notification_id);
    pa_assert(u = backend->userdata);

    conn = pa_dbus_connection_get(u->conn);
    pa_assert_se(msg = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "CloseNotification"));
    pa_assert_se(dbus_message_append_args(msg, DBUS_TYPE_UINT32, dbus_notification_id, DBUS_TYPE_INVALID));

    dbus_message_set_no_reply(msg, TRUE);

    pa_dbus_send_message(conn, msg, NULL, NULL, NULL);
    dbus_message_unref(msg);

    pa_xfree(dbus_notification_id);
}

static void send_notification_reply(DBusPendingCall *pending, void *userdata) {
    DBusError err;
    DBusMessage *msg;
    pa_ui_notification_backend *backend;
    pa_ui_notification *notification;
    pa_dbus_pending *p;
    dn_backend_userdata *u;
    unsigned *dbus_notification_id;

    pa_assert(pending);

    dbus_error_init(&err);

    pa_assert_se(p = userdata);
    pa_assert_se(backend = p->context_data);
    pa_assert_se(notification = p->call_data);
    pa_assert_se(u = backend->userdata);
    pa_assert_se(msg = dbus_pending_call_steal_reply(pending));

    dbus_notification_id = pa_xnew(unsigned, 1);

    if (dbus_message_is_error(msg, DBUS_ERROR_SERVICE_UNKNOWN)) {
        pa_log_debug("No Notifications server registered.");

        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ERROR, notification, NULL), u->reply_handle_userdata);

        goto finish;
    }

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_ERROR) {
        pa_log_error("org.freedesktop.Notifications.Notify() failed: %s: %s", dbus_message_get_error_name(msg), pa_dbus_get_error_message(msg));

        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ERROR, notification, NULL), u->reply_handle_userdata);

        goto finish;
    }

    if (!dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, dbus_notification_id, DBUS_TYPE_INVALID)) {
        pa_log_error("Failed to parse org.freedesktop.Notifications.Notify(): %s", err.message);

        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ERROR, notification, NULL), u->reply_handle_userdata);

        goto finish;
    }

    if (pa_idxset_remove_by_data(u->cancelling, notification, NULL)) {
        cancel_notification_dbus(backend, notification, dbus_notification_id);

        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ERROR, notification, NULL), u->reply_handle_userdata);
    } else {
        pa_hashmap_put(u->displaying, notification, dbus_notification_id);
    }

finish:
    dbus_message_unref(msg);

    PA_LLIST_REMOVE(pa_dbus_pending, u->pending_send, p);
    pa_dbus_pending_free(p);
}

static void dn_send_notification(pa_ui_notification_backend *b, pa_ui_notification *n) {
    send_notification(b, n, true);
}

static void handle_reply(pa_ui_notification_reply *reply, void *useradata) {
    reply->source->reply_cb(reply);
}

static DBusHandlerResult signal_cb(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    DBusError err;
    pa_ui_notification_backend *backend;
    pa_ui_notification *notification;
    dn_backend_userdata *u;
    void *state;

    unsigned dbus_notification_id, reason, *id;
    char *action_key;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(backend = userdata);

   /*  pa_log_debug("Message received: %s.%s", dbus_message_get_interface(msg), dbus_message_get_member(msg)); */

    u = backend->userdata;

    dbus_error_init(&err);
    if (dbus_message_is_signal(msg, "org.freedesktop.Notifications", "NotificationClosed")) {
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &dbus_notification_id, DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID)) {
            pa_log_error("Failed to parse org.freedesktop.Notifications.NotificationClosed: %s.", err.message);
            goto finish;
        }

        /* The assumption here is that if an action was invoked, it will have
           already been processed since the ActionInvoked signal was emitted
           first. That might not always be the case. */
        PA_HASHMAP_FOREACH_KEY(id, notification, u->displaying, state) {
            if (*id == dbus_notification_id) {
                switch(reason) {
                case 1: /* expired */
                    u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_EXPIRED, notification, NULL), u->reply_handle_userdata);
                    break;

                case 2: /* dismissed */
                    /* what if ActionInvoked emitted after NotificationClosed */
                    u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_DISMISSED, notification, NULL), u->reply_handle_userdata);
                    break;

                case 3: /* CloseNotification */
                    /* handled when CloseNotification was called */
                    break;

                case 4: /* undefined/reserved */
                default:
                    u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ERROR, notification, NULL), u->reply_handle_userdata);
                    break;
                }

                pa_xfree(id);
                pa_hashmap_remove(u->displaying, notification);

                break;
            }
        }
    } else if (dbus_message_is_signal(msg, "org.freedesktop.Notifications", "ActionInvoked")) {
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &dbus_notification_id, DBUS_TYPE_STRING, &action_key, DBUS_TYPE_INVALID)) {
            pa_log_error("Failed to parse org.freedesktop.Notifications.ActionInvoked: %s.", err.message);
            goto finish;
        }

        /* pa_log_debug("org.freedesktop.Notifications.ActionInvoked(%u, %s)", dbus_notification_id, action_key); */
        PA_HASHMAP_FOREACH_KEY(id, notification, u->displaying, state) {
            if (*id == dbus_notification_id) {
                u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_ACTION_INVOKED, notification, action_key), u->reply_handle_userdata);
            }

            pa_xfree(id);
            pa_hashmap_remove(u->displaying, notification);
        }
    }

finish:
    dbus_error_free(&err);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void displaying_notifications_cancel(pa_ui_notification_backend *backend) {
    void *state;
    pa_ui_notification *notification;
    unsigned *dbus_notification_id;

    struct dn_backend_userdata *u;

    pa_assert(backend);
    pa_assert(u = backend->userdata);

    PA_HASHMAP_FOREACH_KEY(dbus_notification_id, notification, u->displaying, state) {
        pa_hashmap_remove(u->displaying, notification);
        cancel_notification_dbus(backend, notification, dbus_notification_id);
    }
}

static void pending_notifications_cancel(pa_ui_notification_backend *backend) {
    pa_dbus_pending *i;
    pa_ui_notification *notification;
    struct dn_backend_userdata *u;

    pa_assert(backend);
    pa_assert(u = backend->userdata);

    while ((i = u->pending_send)) {
        PA_LLIST_REMOVE(pa_dbus_pending, u->pending_send, i);

        notification = i->call_data;
        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_CANCELLED, notification, NULL), u->reply_handle_userdata);

        pa_dbus_pending_free(i);
    }
}

pa_ui_notification_backend* dn_backend_new(pa_core *core) {
    return dn_backend_new_with_reply_handle(core, handle_reply, NULL);
}

pa_ui_notification_backend* dn_backend_new_with_reply_handle(pa_core *core, dn_handle_reply_cb_t notification_reply_handle, void *userdata) {
    DBusError err;
    pa_ui_notification_backend *backend;
    dn_backend_userdata *u;

    pa_assert(core);

    backend = pa_xnew0(pa_ui_notification_backend, 1);
    backend->userdata = u = pa_xnew(dn_backend_userdata, 1);

    dbus_error_init(&err);
    u->conn = pa_dbus_bus_get(core, DBUS_BUS_SESSION, &err);

    if (dbus_error_is_set(&err)) {
        pa_log_error("Failed to aquire D-Bus connection: %s", err.message);
        goto fail;
    }

    u->app_name = pa_xstrdup("PulseAudio");
    u->displaying = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    u->cancelling = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    u->notification_reply_handle = notification_reply_handle;
    u->reply_handle_userdata = userdata;

    PA_LLIST_HEAD_INIT(pa_dbus_pending, u->pending_send);

    backend->send_notification = dn_send_notification;
    backend->cancel_notification = cancel_notification;


    if (!dbus_connection_add_filter(pa_dbus_connection_get(u->conn), signal_cb, backend, NULL)) {
        pa_log_error("Failed to add filter function.");
        goto fail;
    }
    u->filter_set = true;

    if (pa_dbus_add_matches(pa_dbus_connection_get(u->conn), &err, "type='signal',sender='org.freedesktop.Notifications',interface='org.freedesktop.Notifications',path='/org/freedesktop/Notifications'", NULL) < 0) {
        pa_log_error("Failed to add D-Bus matches: %s", err.message);
        goto fail;
    }
    dbus_error_free(&err);

    return backend;

fail:
    dbus_error_free(&err);
    dn_backend_free(backend);
    return NULL;
}

void dn_backend_free(pa_ui_notification_backend *backend) {
    dn_backend_userdata *u;

    pa_assert(backend);
    u = backend->userdata;

    if (u) {
        displaying_notifications_cancel(backend);
        pa_hashmap_free(u->displaying, NULL, NULL);
        pa_idxset_free(u->cancelling, NULL, NULL);

        pending_notifications_cancel(backend);

        if (u->filter_set)
            dbus_connection_remove_filter(pa_dbus_connection_get(u->conn), signal_cb, backend);

        pa_dbus_remove_matches(pa_dbus_connection_get(u->conn), "type='signal',sender='org.freedesktop.Notifications',interface='org.freedesktop.Notifications',path='/org/freedesktop/Notifications'", NULL);

        pa_xfree(u->app_name);
        pa_dbus_connection_unref(u->conn);
    }

    pa_xfree(u);
    pa_xfree(backend);
}

void send_notification(pa_ui_notification_backend *b, pa_ui_notification *n, bool use_actions) {
    DBusConnection *conn;
    DBusMessage *msg;
    DBusMessageIter args, dict_iter, array_iter;
    pa_dbus_pending *p;
    unsigned *replaces_id;
    char *key, *value;
    void *state;
    dn_backend_userdata *u;

    u = b->userdata;
    conn = pa_dbus_connection_get(u->conn);

    replaces_id = NULL;
    if (n->replaced_notification != NULL)
        replaces_id = pa_hashmap_remove(u->displaying, n->replaced_notification);

    if (replaces_id == NULL)
        replaces_id = pa_xnew0(unsigned, 1);

    pa_assert_se(msg = dbus_message_new_method_call("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify"));

    dbus_message_iter_init_append(msg, &args);

    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &u->app_name));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, (void *) replaces_id));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &n->icon));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &n->summary));
    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (void *) &n->body));

    pa_assert_se(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &array_iter));

    if (use_actions)
        PA_HASHMAP_FOREACH_KEY(value, key, n->actions, state) {
            pa_assert_se(dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, (void *) &key));
            pa_assert_se(dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, (void *) &value));
        }

    pa_assert_se(dbus_message_iter_close_container(&args, &array_iter));


    pa_assert_se(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));
    /* TODO: hints */
    pa_assert_se(dbus_message_iter_close_container(&args, &dict_iter));

    pa_assert_se(dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, (void *) &n->expire_timeout));

    p = pa_dbus_send_message(conn, msg, send_notification_reply, b, n);
    dbus_message_unref(msg);

    PA_LLIST_PREPEND(pa_dbus_pending, u->pending_send, p);

    pa_xfree(replaces_id);
}

void cancel_notification(pa_ui_notification_backend *backend, pa_ui_notification *notification) {
    dn_backend_userdata *u;
    unsigned *dbus_notification_id;

    u = backend->userdata;

    if ((dbus_notification_id = pa_hashmap_remove(u->displaying, notification))) {
        cancel_notification_dbus(backend, notification, dbus_notification_id);

        u->notification_reply_handle(pa_ui_notification_reply_new(PA_UI_NOTIFCATION_REPLY_CANCELLED, notification, NULL), u->reply_handle_userdata);
    } else {
        pa_idxset_put(u->cancelling, notification, NULL);
    }
}

pa_dbus_pending* pa_dbus_send_message(
    DBusConnection *conn,
    DBusMessage *msg,
    DBusPendingCallNotifyFunction func,
    void *context_data,
    void *call_data) {

    pa_dbus_pending *p;
    DBusPendingCall *pending;

    pa_assert(conn);
    pa_assert(msg);

    if (func) {
        pa_assert_se(dbus_connection_send_with_reply(conn, msg, &pending, -1));
        pa_assert(pending);

        p = pa_dbus_pending_new(conn, NULL, pending, context_data, call_data);
        dbus_pending_call_set_notify(pending, func, p, NULL);
    } else {
        pa_assert_se(dbus_connection_send(conn, msg, NULL));
        p = NULL;
    }

    return p;
}
