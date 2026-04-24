/*
 * BLE transport via BlueZ (sd-bus). Receive-only observer for Stage 2.
 *
 * Lifecycle:
 *   bc_ble_new()    open the system bus, allocate context
 *   bc_ble_start()  pick an adapter, power it on, set UUID filter,
 *                   start discovery, register signal matches
 *   bc_ble_run()    blocking event loop — invokes the frame callback
 *                   on each BitChat GATT notification received
 *   bc_ble_free()   release the bus and tear everything down
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_BLE_H
#define BITCHAT_BLE_H

#include <stddef.h>
#include <stdint.h>

#define BC_BLE_SERVICE_UUID        "f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c"
#define BC_BLE_SERVICE_UUID_TEST   "f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5a"
#define BC_BLE_CHARACTERISTIC_UUID "a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d"

/*
 * Frame callback. Invoked once per GATT notification received from any
 * peer. `data` points at the raw BitchatPacket bytes (still padded, may be
 * compressed); caller should feed these directly into bc_packet_decode().
 * `peer_path` is the BlueZ object path of the emitting device, e.g.
 * "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF" — useful for logging and for
 * demuxing when multiple peers are in range.
 */
typedef void (*bc_ble_frame_cb_t)(const uint8_t *data, size_t len,
                                  const char *peer_path, void *user);

/* Optional peer event callback. event_type: "connect", "disconnect",
 * "services-resolved", "notify-started". */
typedef void (*bc_ble_peer_cb_t)(const char *peer_path,
                                 const char *event_type,
                                 void *user);

typedef struct bc_ble_ctx bc_ble_ctx_t;

bc_ble_ctx_t *bc_ble_new(bc_ble_frame_cb_t frame_cb,
                         bc_ble_peer_cb_t  peer_cb,
                         void *user);

/* Start scanning on the first powered adapter. If use_testnet is true, the
 * testnet service UUID is used instead of mainnet. Returns 0 on success,
 * a negative errno-style value on failure. */
int bc_ble_start(bc_ble_ctx_t *ctx, int use_testnet);

/* Blocking event loop. Returns 0 on clean shutdown (via bc_ble_stop), or
 * a negative errno on fatal error. */
int bc_ble_run(bc_ble_ctx_t *ctx);

/* Signal-safe-ish shutdown: sets a flag; the run loop returns on next tick. */
void bc_ble_stop(bc_ble_ctx_t *ctx);

void bc_ble_free(bc_ble_ctx_t *ctx);

#endif /* BITCHAT_BLE_H */
