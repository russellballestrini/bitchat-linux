/*
 * BLE transport — receive-only observer via BlueZ D-Bus (sd-bus).
 *
 * BlueZ model:
 *   org.bluez                                  (service)
 *     /                                        (ObjectManager root)
 *     /org/bluez/hciX                          (Adapter1)
 *     /org/bluez/hciX/dev_AA_BB_..             (Device1)
 *     /org/bluez/hciX/dev_AA_BB_../serviceY    (GattService1)
 *     /org/bluez/hciX/dev_AA_BB_../serviceY/charZ  (GattCharacteristic1)
 *
 * Flow:
 *   1. GetManagedObjects → find an Adapter1, remember its path
 *   2. Adapter1.Powered = true (best effort)
 *   3. Adapter1.SetDiscoveryFilter  {UUIDs: [service_uuid], Transport: "le"}
 *   4. Adapter1.StartDiscovery
 *   5. Subscribe to InterfacesAdded / PropertiesChanged / InterfacesRemoved
 *   6. For each Device1 whose UUIDs include ours → Device1.Connect (async)
 *   7. For each GattCharacteristic1 whose UUID matches → StartNotify (async)
 *   8. PropertiesChanged on characteristic with "Value" property → frame_cb
 *
 * This is free and unencumbered software released into the public domain.
 */

#include "ble.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <systemd/sd-bus.h>

#define BLUEZ_DEST        "org.bluez"
#define ADAPTER_IFACE     "org.bluez.Adapter1"
#define DEVICE_IFACE      "org.bluez.Device1"
#define GATT_SERVICE_IFACE "org.bluez.GattService1"
#define GATT_CHAR_IFACE   "org.bluez.GattCharacteristic1"
#define OBJ_MGR_IFACE     "org.freedesktop.DBus.ObjectManager"
#define PROPS_IFACE       "org.freedesktop.DBus.Properties"

struct bc_ble_ctx {
    sd_bus           *bus;
    bc_ble_frame_cb_t frame_cb;
    bc_ble_peer_cb_t  peer_cb;
    void             *user;
    char             *adapter_path;   /* e.g. /org/bluez/hci0 */
    const char       *service_uuid;   /* lowercased, no braces */
    volatile int      running;

    sd_bus_slot *slot_added;
    sd_bus_slot *slot_removed;
    sd_bus_slot *slot_props;
};

/* ---------- logging ---------- */

__attribute__((format(printf, 1, 2)))
static void ble_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[ble] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ---------- uuid helpers ---------- */

static int uuid_eq(const char *a, const char *b) {
    return a && b && strcasecmp(a, b) == 0;
}

/* ---------- message parsers ---------- */

/* Drains the variant at the cursor and, if it matches "ay", writes the
 * decoded bytes into *out. Returns 1 if it was an ay variant, 0 otherwise.
 * Advances past the variant either way. */
static int variant_take_ay(sd_bus_message *m, const uint8_t **out, size_t *out_len) {
    const char *contents = NULL;
    char type;
    int r = sd_bus_message_peek_type(m, &type, &contents);
    if (r <= 0 || type != 'v') return 0;

    if (contents && strcmp(contents, "ay") == 0) {
        r = sd_bus_message_enter_container(m, 'v', "ay");
        if (r < 0) return 0;
        const void *p = NULL; size_t n = 0;
        r = sd_bus_message_read_array(m, 'y', &p, &n);
        if (r >= 0) { *out = (const uint8_t *)p; *out_len = n; }
        sd_bus_message_exit_container(m);
        return r >= 0 ? 1 : 0;
    }

    /* Not our variant type — skip it. */
    sd_bus_message_skip(m, "v");
    return 0;
}

/* Scan an "as" variant for the target uuid. Advances past the variant. */
static int variant_as_contains(sd_bus_message *m, const char *target) {
    const char *contents = NULL;
    char type;
    int r = sd_bus_message_peek_type(m, &type, &contents);
    if (r <= 0 || type != 'v') return 0;
    if (!contents || strcmp(contents, "as") != 0) {
        sd_bus_message_skip(m, "v");
        return 0;
    }
    int found = 0;
    r = sd_bus_message_enter_container(m, 'v', "as");
    if (r >= 0) {
        r = sd_bus_message_enter_container(m, 'a', "s");
        if (r >= 0) {
            const char *s;
            while ((r = sd_bus_message_read_basic(m, 's', &s)) > 0) {
                if (uuid_eq(s, target)) found = 1;
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    return found;
}

/* Scan an "s" variant and copy up to cap-1 bytes into out. Returns 1 on
 * success, 0 if variant wasn't a string. */
static int variant_take_s(sd_bus_message *m, char *out, size_t cap) {
    const char *contents = NULL;
    char type;
    int r = sd_bus_message_peek_type(m, &type, &contents);
    if (r <= 0 || type != 'v') return 0;
    if (!contents || strcmp(contents, "s") != 0) {
        sd_bus_message_skip(m, "v");
        return 0;
    }
    r = sd_bus_message_enter_container(m, 'v', "s");
    if (r < 0) return 0;
    const char *s = NULL;
    r = sd_bus_message_read_basic(m, 's', &s);
    if (r >= 0 && s) {
        snprintf(out, cap, "%s", s);
    }
    sd_bus_message_exit_container(m);
    return r >= 0;
}

/* ---------- bluez actions ---------- */

static int adapter_set_powered(bc_ble_ctx_t *ctx, int on) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(ctx->bus, BLUEZ_DEST, ctx->adapter_path,
                               PROPS_IFACE, "Set",
                               &err, NULL, "ssv",
                               ADAPTER_IFACE, "Powered", "b", on ? 1 : 0);
    if (r < 0) {
        ble_logf("adapter power %s failed: %s", on ? "on" : "off", err.message ? err.message : strerror(-r));
    }
    sd_bus_error_free(&err);
    return r;
}

static int adapter_set_discovery_filter(bc_ble_ctx_t *ctx) {
    sd_bus_message *msg = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r;

    r = sd_bus_message_new_method_call(ctx->bus, &msg, BLUEZ_DEST,
                                       ctx->adapter_path, ADAPTER_IFACE,
                                       "SetDiscoveryFilter");
    if (r < 0) goto done;

    r = sd_bus_message_open_container(msg, 'a', "{sv}");
    if (r < 0) goto done;

    /* UUIDs: [service_uuid] */
    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) goto done;
    r = sd_bus_message_append_basic(msg, 's', "UUIDs");
    if (r < 0) goto done;
    r = sd_bus_message_open_container(msg, 'v', "as");
    if (r < 0) goto done;
    r = sd_bus_message_append_strv(msg, (char *[]){ (char *)ctx->service_uuid, NULL });
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);  /* close variant */
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);  /* close dict entry */
    if (r < 0) goto done;

    /* Transport: "le" */
    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) goto done;
    r = sd_bus_message_append_basic(msg, 's', "Transport");
    if (r < 0) goto done;
    r = sd_bus_message_open_container(msg, 'v', "s");
    if (r < 0) goto done;
    r = sd_bus_message_append_basic(msg, 's', "le");
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);
    if (r < 0) goto done;

    /* DuplicateData: true — we want every advertisement to keep RSSI fresh */
    r = sd_bus_message_open_container(msg, 'e', "sv");
    if (r < 0) goto done;
    r = sd_bus_message_append_basic(msg, 's', "DuplicateData");
    if (r < 0) goto done;
    r = sd_bus_message_open_container(msg, 'v', "b");
    if (r < 0) goto done;
    r = sd_bus_message_append_basic(msg, 'b', &(int){1});
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);
    if (r < 0) goto done;
    r = sd_bus_message_close_container(msg);
    if (r < 0) goto done;

    r = sd_bus_message_close_container(msg);  /* close a{sv} */
    if (r < 0) goto done;

    r = sd_bus_call(ctx->bus, msg, 0, &err, NULL);
done:
    if (r < 0) ble_logf("SetDiscoveryFilter failed: %s", err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);
    sd_bus_message_unref(msg);
    return r;
}

static int adapter_start_discovery(bc_ble_ctx_t *ctx) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(ctx->bus, BLUEZ_DEST, ctx->adapter_path,
                               ADAPTER_IFACE, "StartDiscovery",
                               &err, NULL, NULL);
    if (r < 0) ble_logf("StartDiscovery failed: %s", err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);
    return r;
}

static int device_connect_async(bc_ble_ctx_t *ctx, const char *path) {
    int r = sd_bus_call_method_async(ctx->bus, NULL, BLUEZ_DEST, path,
                                     DEVICE_IFACE, "Connect",
                                     NULL, NULL, NULL);
    if (r < 0) ble_logf("Connect(%s) dispatch failed: %s", path, strerror(-r));
    return r;
}

static int char_start_notify_async(bc_ble_ctx_t *ctx, const char *path) {
    int r = sd_bus_call_method_async(ctx->bus, NULL, BLUEZ_DEST, path,
                                     GATT_CHAR_IFACE, "StartNotify",
                                     NULL, NULL, NULL);
    if (r < 0) ble_logf("StartNotify(%s) dispatch failed: %s", path, strerror(-r));
    return r;
}

/* ---------- interface handlers ---------- */

/* Parse one Device1 property dict. Returns 1 if it advertises our service. */
static int device_props_match(sd_bus_message *m, const char *target) {
    int found = 0;
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return 0;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key;
        if (sd_bus_message_read_basic(m, 's', &key) < 0) {
            sd_bus_message_exit_container(m);
            break;
        }
        if (strcmp(key, "UUIDs") == 0) {
            if (variant_as_contains(m, target)) found = 1;
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return found;
}

/* Parse one GattCharacteristic1 prop dict. Returns 1 iff UUID == target. */
static int char_props_match(sd_bus_message *m, const char *target) {
    int match = 0;
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return 0;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key;
        if (sd_bus_message_read_basic(m, 's', &key) < 0) {
            sd_bus_message_exit_container(m);
            break;
        }
        if (strcmp(key, "UUID") == 0) {
            char buf[64] = {0};
            if (variant_take_s(m, buf, sizeof(buf)) && uuid_eq(buf, target)) {
                match = 1;
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return match;
}

/* Handle one {iface -> a{sv}} pair for a given object path. */
static void handle_iface_entry(bc_ble_ctx_t *ctx, const char *path,
                               const char *iface, sd_bus_message *m) {
    if (strcmp(iface, DEVICE_IFACE) == 0) {
        if (device_props_match(m, ctx->service_uuid)) {
            ble_logf("device: %s matches, connecting", path);
            if (ctx->peer_cb) ctx->peer_cb(path, "discover", ctx->user);
            device_connect_async(ctx, path);
        } else {
            sd_bus_message_skip(m, "a{sv}");
        }
    } else if (strcmp(iface, GATT_CHAR_IFACE) == 0) {
        if (char_props_match(m, BC_BLE_CHARACTERISTIC_UUID)) {
            ble_logf("char: %s matches, start notify", path);
            if (ctx->peer_cb) ctx->peer_cb(path, "notify-started", ctx->user);
            char_start_notify_async(ctx, path);
        } else {
            sd_bus_message_skip(m, "a{sv}");
        }
    } else {
        sd_bus_message_skip(m, "a{sv}");
    }
}

/* ---------- signal handlers ---------- */

static int on_interfaces_added(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error) {
    (void)ret_error;
    bc_ble_ctx_t *ctx = (bc_ble_ctx_t *)userdata;
    const char *path = NULL;
    int r = sd_bus_message_read_basic(m, 'o', &path);
    if (r < 0 || !path) return 0;

    r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
    if (r < 0) return 0;
    while ((r = sd_bus_message_enter_container(m, 'e', "sa{sv}")) > 0) {
        const char *iface = NULL;
        if (sd_bus_message_read_basic(m, 's', &iface) < 0) {
            sd_bus_message_exit_container(m);
            break;
        }
        handle_iface_entry(ctx, path, iface, m);
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return 0;
}

static int on_interfaces_removed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error) {
    (void)ret_error;
    bc_ble_ctx_t *ctx = (bc_ble_ctx_t *)userdata;
    const char *path = NULL;
    if (sd_bus_message_read_basic(m, 'o', &path) < 0 || !path) return 0;

    /* We could filter further here, but log every removal for now. */
    if (ctx->peer_cb) ctx->peer_cb(path, "disconnect", ctx->user);
    return 0;
}

/* Looks at PropertiesChanged signal path to decide if it's ours. The path
 * structure is stable: GattCharacteristic1 sits under a device, and the
 * signal's path IS the characteristic path when Value changes there. */
static int on_properties_changed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error) {
    (void)ret_error;
    bc_ble_ctx_t *ctx = (bc_ble_ctx_t *)userdata;

    const char *iface = NULL;
    int r = sd_bus_message_read_basic(m, 's', &iface);
    if (r < 0 || !iface) return 0;

    /* We only care about characteristic Value changes. */
    if (strcmp(iface, GATT_CHAR_IFACE) != 0) {
        sd_bus_message_skip(m, "a{sv}as");
        return 0;
    }

    const char *path = sd_bus_message_get_path(m);

    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return 0;

    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key;
        if (sd_bus_message_read_basic(m, 's', &key) < 0) {
            sd_bus_message_exit_container(m);
            break;
        }
        if (strcmp(key, "Value") == 0) {
            const uint8_t *bytes = NULL;
            size_t n = 0;
            if (variant_take_ay(m, &bytes, &n) && ctx->frame_cb && n > 0) {
                ctx->frame_cb(bytes, n, path, ctx->user);
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    /* Skip invalidated array. */
    sd_bus_message_skip(m, "as");
    return 0;
}

/* ---------- startup: walk managed objects ---------- */

static int scan_managed_objects(bc_ble_ctx_t *ctx) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(ctx->bus, BLUEZ_DEST, "/",
                               OBJ_MGR_IFACE, "GetManagedObjects",
                               &err, &reply, NULL);
    if (r < 0) {
        ble_logf("GetManagedObjects failed: %s", err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return r;
    }

    r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    if (r < 0) goto done;

    while ((r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
        const char *path = NULL;
        if (sd_bus_message_read_basic(reply, 'o', &path) < 0) {
            sd_bus_message_exit_container(reply);
            break;
        }
        r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        if (r < 0) { sd_bus_message_exit_container(reply); break; }

        while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
            const char *iface = NULL;
            if (sd_bus_message_read_basic(reply, 's', &iface) < 0) {
                sd_bus_message_exit_container(reply);
                break;
            }
            if (strcmp(iface, ADAPTER_IFACE) == 0) {
                if (!ctx->adapter_path) {
                    ctx->adapter_path = strdup(path);
                    ble_logf("adapter: %s", path);
                }
                sd_bus_message_skip(reply, "a{sv}");
            } else {
                /* Pre-existing device / char — same logic as InterfacesAdded. */
                handle_iface_entry(ctx, path, iface, reply);
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);

done:
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return ctx->adapter_path ? 0 : -ENODEV;
}

/* ---------- public API ---------- */

bc_ble_ctx_t *bc_ble_new(bc_ble_frame_cb_t frame_cb,
                         bc_ble_peer_cb_t peer_cb,
                         void *user) {
    bc_ble_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->frame_cb = frame_cb;
    ctx->peer_cb  = peer_cb;
    ctx->user     = user;
    ctx->running  = 0;

    int r = sd_bus_open_system(&ctx->bus);
    if (r < 0) {
        ble_logf("sd_bus_open_system: %s", strerror(-r));
        free(ctx);
        return NULL;
    }
    return ctx;
}

int bc_ble_start(bc_ble_ctx_t *ctx, const char *adapter_path, int use_testnet) {
    ctx->service_uuid = use_testnet ? BC_BLE_SERVICE_UUID_TEST : BC_BLE_SERVICE_UUID;

    /* If the caller supplied an explicit adapter path, record it now so the
     * managed-objects walk doesn't overwrite with the first one found. */
    if (adapter_path && adapter_path[0]) {
        ctx->adapter_path = strdup(adapter_path);
        if (!ctx->adapter_path) return -ENOMEM;
        ble_logf("adapter (explicit): %s", ctx->adapter_path);
    }

    /* Register match rules first so we don't miss InterfacesAdded between
     * scan and StartDiscovery. */
    int r = sd_bus_match_signal(ctx->bus, &ctx->slot_added, BLUEZ_DEST,
                                NULL, OBJ_MGR_IFACE, "InterfacesAdded",
                                on_interfaces_added, ctx);
    if (r < 0) { ble_logf("match InterfacesAdded: %s", strerror(-r)); return r; }

    r = sd_bus_match_signal(ctx->bus, &ctx->slot_removed, BLUEZ_DEST,
                            NULL, OBJ_MGR_IFACE, "InterfacesRemoved",
                            on_interfaces_removed, ctx);
    if (r < 0) { ble_logf("match InterfacesRemoved: %s", strerror(-r)); return r; }

    r = sd_bus_match_signal(ctx->bus, &ctx->slot_props, BLUEZ_DEST,
                            NULL, PROPS_IFACE, "PropertiesChanged",
                            on_properties_changed, ctx);
    if (r < 0) { ble_logf("match PropertiesChanged: %s", strerror(-r)); return r; }

    /* Enumerate existing adapters and devices. */
    r = scan_managed_objects(ctx);
    if (r < 0) return r;

    adapter_set_powered(ctx, 1);          /* best effort */
    r = adapter_set_discovery_filter(ctx);
    if (r < 0) return r;
    r = adapter_start_discovery(ctx);
    if (r < 0) return r;

    ble_logf("discovering on service %s", ctx->service_uuid);
    ctx->running = 1;
    return 0;
}

int bc_ble_run(bc_ble_ctx_t *ctx) {
    while (ctx->running) {
        int r;
        do {
            r = sd_bus_process(ctx->bus, NULL);
            if (r < 0) {
                ble_logf("sd_bus_process: %s", strerror(-r));
                return r;
            }
        } while (r > 0 && ctx->running);

        if (!ctx->running) break;

        r = sd_bus_wait(ctx->bus, 1000000 /* 1s */);
        if (r < 0 && r != -EINTR) {
            ble_logf("sd_bus_wait: %s", strerror(-r));
            return r;
        }
    }
    return 0;
}

void bc_ble_stop(bc_ble_ctx_t *ctx) {
    if (ctx) ctx->running = 0;
}

void bc_ble_free(bc_ble_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->adapter_path) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_call_method(ctx->bus, BLUEZ_DEST, ctx->adapter_path,
                           ADAPTER_IFACE, "StopDiscovery",
                           &err, NULL, NULL);
        sd_bus_error_free(&err);
    }
    sd_bus_slot_unref(ctx->slot_added);
    sd_bus_slot_unref(ctx->slot_removed);
    sd_bus_slot_unref(ctx->slot_props);
    if (ctx->bus) sd_bus_unref(ctx->bus);
    free(ctx->adapter_path);
    free(ctx);
}
