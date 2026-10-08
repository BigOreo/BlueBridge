/*
    GlideKVM -- mouse and keyboard sharing utility
    Copyright (C) GlideKVM contributors

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bridge.h"

#include "reports.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdio.h>
#include <string.h>

void ble_store_config_init(void);

static const char* TAG = "bridge";

#define DEVICE_NAME "GlideKVM Bridge"
#define APPEARANCE_HID 0x03C0
#define PAIRING_SECONDS 120
// a device that connects has this long to prove it was paired before
#define UNPAIRED_GRACE_SECONDS 10
#define NO_CONN BLE_HS_CONN_HANDLE_NONE

// what is remembered about each paired device
typedef struct {
    uint8_t used;
    uint8_t allowed;
    ble_addr_t addr;
    char name[33];
} stored_slot_t;

typedef struct {
    uint16_t conn;
    // the input reports the device asked to be told about
    bool kb_on;
    bool mouse_on;
    bool media_on;
} live_slot_t;

typedef struct {
    uint16_t conn;
    TickType_t since;
} pending_t;

static stored_slot_t s_slots[BRIDGE_SLOTS];
static live_slot_t s_live[BRIDGE_SLOTS];
static pending_t s_pending[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];
static SemaphoreHandle_t s_lock;
static TimerHandle_t s_tick;
static int s_target = -1;
static bool s_pairing;
static TickType_t s_pairing_until;
static uint8_t s_own_addr_type;
static bool s_synced;

static uint16_t s_kb_in_handle;
static uint16_t s_kb_out_handle;
static uint16_t s_mouse_handle;
static uint16_t s_media_handle;
static uint16_t s_battery_handle;

// Bluetooth events and the main computer's commands come from different
// tasks; some calls in here lead back in, so the lock is recursive.
#define LOCK() xSemaphoreTakeRecursive(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_lock)

static int gap_event(struct ble_gap_event* event, void* arg);
static void advertise(void);

// remembered devices ----------------------------------------------------------

static void save_slots(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bridge", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "slots", s_slots, sizeof s_slots);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void load_slots(void)
{
    nvs_handle_t nvs;
    size_t size = sizeof s_slots;
    if (nvs_open("bridge", NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "slots", s_slots, &size) != ESP_OK || size != sizeof s_slots) {
            memset(s_slots, 0, sizeof s_slots);
        }
        nvs_close(nvs);
    }
    // a device forgotten by the stack (its keys lost) is forgotten here too
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        struct ble_store_key_sec key = {0};
        struct ble_store_value_sec value;
        key.peer_addr = s_slots[i].addr;
        if (s_slots[i].used && ble_store_read_peer_sec(&key, &value) != 0) {
            s_slots[i].used = 0;
        }
    }
}

static int slot_of_addr(const ble_addr_t* addr)
{
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        if (s_slots[i].used && !ble_addr_cmp(&s_slots[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

static int slot_of_conn(uint16_t conn)
{
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        if (s_live[i].conn == conn) {
            return i;
        }
    }
    return -1;
}

static void addr_text(const ble_addr_t* addr, char out[18])
{
    const uint8_t* v = addr->val;
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", v[5], v[4], v[3], v[2], v[1], v[0]);
}

static void say_slot(int i)
{
    char addr[18];
    addr_text(&s_slots[i].addr, addr);
    const char* state = !s_slots[i].allowed ? "off" : s_live[i].conn != NO_CONN ? "connected" : "away";
    bridge_say("@slot %d %s %s %s", i, addr, state, s_slots[i].name);
}

// connections that have yet to show they were paired --------------------------

static void pending_add(uint16_t conn)
{
    for (int i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
        if (s_pending[i].conn == NO_CONN) {
            s_pending[i].conn = conn;
            s_pending[i].since = xTaskGetTickCount();
            return;
        }
    }
}

static void pending_remove(uint16_t conn)
{
    for (int i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
        if (s_pending[i].conn == conn) {
            s_pending[i].conn = NO_CONN;
        }
    }
}

static void tick(TimerHandle_t timer)
{
    LOCK();
    const TickType_t now = xTaskGetTickCount();
    const TickType_t grace = pdMS_TO_TICKS((s_pairing ? PAIRING_SECONDS : UNPAIRED_GRACE_SECONDS) * 1000);
    for (int i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
        if (s_pending[i].conn != NO_CONN && now - s_pending[i].since > grace) {
            ble_gap_terminate(s_pending[i].conn, BLE_ERR_REM_USER_CONN_TERM);
            s_pending[i].conn = NO_CONN;
        }
    }
    if (s_pairing && (int32_t)(now - s_pairing_until) > 0) {
        s_pairing = false;
        ble_hs_cfg.sm_bonding = 0;
        bridge_say("@pairing-ended");
        advertise();
    }
    UNLOCK();
}

// the HID service --------------------------------------------------------------

static const uint8_t s_hid_info[] = {0x11, 0x01, 0x00, 0x02};  // HID 1.11, normally connectable
static const uint8_t s_kb_in_ref[] = {REPORT_ID_KEYBOARD, 1};
static const uint8_t s_kb_out_ref[] = {REPORT_ID_KEYBOARD, 2};
static const uint8_t s_mouse_ref[] = {REPORT_ID_MOUSE, 1};
static const uint8_t s_media_ref[] = {REPORT_ID_CONSUMER, 1};
// Bluetooth SIG vendor ids, Espressif, product 1, version 1
static const uint8_t s_pnp_id[] = {0x01, 0xE5, 0x02, 0x01, 0x00, 0x01, 0x00};
static uint8_t s_protocol_mode = 1;  // report protocol
static uint8_t s_battery = 100;

static int reply(struct ble_gatt_access_ctxt* ctxt, const void* data, uint16_t size)
{
    return os_mbuf_append(ctxt->om, data, size) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_fixed(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR || ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        const uint8_t* value = arg;
        if (value == s_hid_info) return reply(ctxt, s_hid_info, sizeof s_hid_info);
        if (value == s_kb_in_ref || value == s_kb_out_ref || value == s_mouse_ref || value == s_media_ref) {
            return reply(ctxt, value, 2);
        }
        if (value == s_pnp_id) return reply(ctxt, s_pnp_id, sizeof s_pnp_id);
        if (value == &s_battery) return reply(ctxt, &s_battery, 1);
    }
    return 0;
}

static int access_report_map(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    return reply(ctxt, hid_report_map, (uint16_t)hid_report_map_size);
}

static int access_manufacturer(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    return reply(ctxt, "GlideKVM", 8);
}

static int access_protocol_mode(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return reply(ctxt, &s_protocol_mode, 1);
    }
    return 0;  // only the report protocol is offered; a change is ignored
}

static int access_report(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // the current state is "nothing held"
        static const uint8_t zeros[KEYBOARD_REPORT_SIZE] = {0};
        const uint16_t size = attr == s_kb_in_handle || attr == s_kb_out_handle ? KEYBOARD_REPORT_SIZE
                              : attr == s_mouse_handle                         ? MOUSE_REPORT_SIZE
                                                                               : CONSUMER_REPORT_SIZE;
        return reply(ctxt, zeros, attr == s_kb_out_handle ? 1 : size);
    }
    // the keyboard lights; nothing shows them
    return 0;
}

static int access_control_point(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    return 0;  // suspend and exit suspend: nothing to save
}

#define READ_SECURE (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define DSC_READ_SECURE (BLE_ATT_F_READ | BLE_ATT_F_READ_ENC)

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),  // human interface device
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = access_fixed, .arg = (void*)s_hid_info,
             .flags = BLE_GATT_CHR_F_READ},
            {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = access_report_map, .flags = READ_SECURE},
            {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = access_control_point,
             .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = access_protocol_mode,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_report, .val_handle = &s_kb_in_handle,
             .flags = READ_SECURE | BLE_GATT_CHR_F_NOTIFY,
             .descriptors = (struct ble_gatt_dsc_def[]) {
                 {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = DSC_READ_SECURE, .access_cb = access_fixed,
                  .arg = (void*)s_kb_in_ref},
                 {0},
             }},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_report, .val_handle = &s_kb_out_handle,
             .flags = READ_SECURE | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
             .descriptors = (struct ble_gatt_dsc_def[]) {
                 {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = DSC_READ_SECURE, .access_cb = access_fixed,
                  .arg = (void*)s_kb_out_ref},
                 {0},
             }},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_report, .val_handle = &s_mouse_handle,
             .flags = READ_SECURE | BLE_GATT_CHR_F_NOTIFY,
             .descriptors = (struct ble_gatt_dsc_def[]) {
                 {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = DSC_READ_SECURE, .access_cb = access_fixed,
                  .arg = (void*)s_mouse_ref},
                 {0},
             }},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = access_report, .val_handle = &s_media_handle,
             .flags = READ_SECURE | BLE_GATT_CHR_F_NOTIFY,
             .descriptors = (struct ble_gatt_dsc_def[]) {
                 {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = DSC_READ_SECURE, .access_cb = access_fixed,
                  .arg = (void*)s_media_ref},
                 {0},
             }},
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),  // device information
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = access_manufacturer, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = access_fixed, .arg = (void*)s_pnp_id,
             .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),  // battery: it runs off USB, so always full
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = access_fixed, .arg = &s_battery,
             .val_handle = &s_battery_handle, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
            {0},
        },
    },
    {0},
};

// connections ------------------------------------------------------------------

static int connection_count(void)
{
    int n = 0;
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        n += s_live[i].conn != NO_CONN;
    }
    for (int i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
        n += s_pending[i].conn != NO_CONN;
    }
    return n;
}

// Advertises while a device may want to connect: one being paired, or a paired
// one that is allowed in and away. Devices that are sent away don't keep
// coming back while nothing else needs the bridge to be seen.
static void advertise(void)
{
    if (!s_synced) {
        return;
    }
    bool wanted = s_pairing;
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        wanted |= s_slots[i].used && s_slots[i].allowed && s_live[i].conn == NO_CONN;
    }
    wanted &= connection_count() < MYNEWT_VAL(BLE_MAX_CONNECTIONS);

    if (!wanted) {
        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }
        return;
    }
    if (ble_gap_adv_active()) {
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = APPEARANCE_HID;
    fields.appearance_is_present = 1;
    fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(0x1812)};
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "advertising data: %d", rc);
        return;
    }

    struct ble_hs_adv_fields response;
    memset(&response, 0, sizeof response);
    response.name = (uint8_t*)DEVICE_NAME;
    response.name_len = strlen(DEVICE_NAME);
    response.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&response);

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof params);
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // quick to find while pairing, gentler otherwise
    params.itvl_min = s_pairing ? BLE_GAP_ADV_FAST_INTERVAL1_MIN : BLE_GAP_ADV_FAST_INTERVAL2_MIN;
    params.itvl_max = s_pairing ? BLE_GAP_ADV_FAST_INTERVAL1_MAX : BLE_GAP_ADV_FAST_INTERVAL2_MAX;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "advertising: %d", rc);
    }
}

static int name_read(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg)
{
    if (!attr || !attr->om) {
        return 0;
    }
    char name[33];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len > sizeof name - 1) {
        len = sizeof name - 1;
    }
    if (ble_hs_mbuf_to_flat(attr->om, name, len, &len) != 0) {
        return 0;
    }
    name[len] = 0;
    // one line per message: no line breaks in names
    for (char* c = name; *c; ++c) {
        if (*c == '\r' || *c == '\n') *c = ' ';
    }

    LOCK();
    const int slot = slot_of_conn(conn);
    if (slot >= 0 && strcmp(s_slots[slot].name, name) != 0) {
        strcpy(s_slots[slot].name, name);
        save_slots();
        bridge_say("@named %d %s", slot, name);
    }
    UNLOCK();
    return 0;
}

// A device's link is now encrypted: it is one paired before, or one being
// paired now. Anything else is sent away.
static void link_secured(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0) {
        return;
    }
    pending_remove(conn);
    int slot = desc.sec_state.bonded ? slot_of_addr(&desc.peer_id_addr) : -1;
    bool fresh = false;

    if (slot < 0 && desc.sec_state.bonded && s_pairing) {
        for (int i = 0; i < BRIDGE_SLOTS && slot < 0; ++i) {
            if (!s_slots[i].used) {
                slot = i;
            }
        }
        if (slot >= 0) {
            memset(&s_slots[slot], 0, sizeof s_slots[slot]);
            s_slots[slot].used = 1;
            s_slots[slot].allowed = 1;
            s_slots[slot].addr = desc.peer_id_addr;
            save_slots();
            fresh = true;
        }
    }
    if (slot < 0) {
        // not paired, or no room left: don't keep its keys either
        if (desc.sec_state.bonded) {
            ble_gap_unpair(&desc.peer_id_addr);
        }
        ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL);
        if (s_pairing && desc.sec_state.bonded) {
            bridge_say("@full");
        }
        return;
    }
    if (!s_slots[slot].allowed) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    // the same device back on a new link
    if (s_live[slot].conn != NO_CONN && s_live[slot].conn != conn) {
        ble_gap_terminate(s_live[slot].conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_live[slot] = (live_slot_t) {.conn = conn};

    if (fresh) {
        char addr[18];
        addr_text(&desc.peer_id_addr, addr);
        bridge_say("@paired %d %s", slot, addr);
        s_pairing = false;
        ble_hs_cfg.sm_bonding = 0;
    }
    bridge_say("@connected %d", slot);

    // short connection intervals keep the pointer smooth
    struct ble_gap_upd_params params = {
        .itvl_min = 6, .itvl_max = 12, .latency = 0, .supervision_timeout = 300,
    };
    ble_gap_update_params(conn, &params);
    ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME),
                           name_read, NULL);
    advertise();
}

static int gap_event(struct ble_gap_event* event, void* arg)
{
    LOCK();
    int result = 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            pending_add(event->connect.conn_handle);
        }
        advertise();
        break;

    case BLE_GAP_EVENT_DISCONNECT: {
        const uint16_t conn = event->disconnect.conn.conn_handle;
        pending_remove(conn);
        const int slot = slot_of_conn(conn);
        if (slot >= 0) {
            s_live[slot].conn = NO_CONN;
            bridge_say("@disconnected %d", slot);
        }
        advertise();
        break;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            link_secured(event->enc_change.conn_handle);
        } else {
            // usually a device that forgot the bridge: it has to pair again
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // a paired device that lost its keys pairs again, only while pairing
        struct ble_gap_conn_desc desc;
        if (s_pairing && ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            result = BLE_GAP_REPEAT_PAIRING_RETRY;
        } else {
            result = BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        break;
    }

    case BLE_GAP_EVENT_SUBSCRIBE: {
        const int slot = slot_of_conn(event->subscribe.conn_handle);
        if (slot >= 0) {
            const bool on = event->subscribe.cur_notify;
            const uint16_t attr = event->subscribe.attr_handle;
            if (attr == s_kb_in_handle) s_live[slot].kb_on = on;
            if (attr == s_mouse_handle) s_live[slot].mouse_on = on;
            if (attr == s_media_handle) s_live[slot].media_on = on;
        }
        break;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    default:
        break;
    }
    UNLOCK();
    return result;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    LOCK();
    s_synced = true;
    load_slots();
    advertise();
    UNLOCK();
    bridge_say("@ready");
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "bluetooth reset: %d", reason);
}

static void host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void bridge_start(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        s_live[i].conn = NO_CONN;
    }
    for (int i = 0; i < MYNEWT_VAL(BLE_MAX_CONNECTIONS); ++i) {
        s_pending[i].conn = NO_CONN;
    }

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // Just Works pairing, as keyboards without a screen do; new devices
    // only bond while the person asked to pair one
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_device_appearance_set(APPEARANCE_HID);
    ESP_ERROR_CHECK(ble_gatts_count_cfg(s_services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(s_services));
    ble_store_config_init();

    s_tick = xTimerCreate("bridge", pdMS_TO_TICKS(1000), pdTRUE, NULL, tick);
    xTimerStart(s_tick, 0);
    nimble_port_freertos_init(host_task);
}

// commands from the main computer ------------------------------------------------

void bridge_list(void)
{
    LOCK();
    for (int i = 0; i < BRIDGE_SLOTS; ++i) {
        if (s_slots[i].used) {
            say_slot(i);
        }
    }
    bridge_say("@end");
    UNLOCK();
}

void bridge_pair(bool on)
{
    LOCK();
    s_pairing = on;
    ble_hs_cfg.sm_bonding = on ? 1 : 0;
    s_pairing_until = xTaskGetTickCount() + pdMS_TO_TICKS(PAIRING_SECONDS * 1000);
    // advertise afresh, quickly while pairing
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    advertise();
    bridge_say(on ? "@pairing" : "@pairing-ended");
    UNLOCK();
}

void bridge_forget(int slot)
{
    if (slot < 0 || slot >= BRIDGE_SLOTS) {
        bridge_say("@error no such device");
        return;
    }
    LOCK();
    if (s_slots[slot].used) {
        if (s_live[slot].conn != NO_CONN) {
            ble_gap_terminate(s_live[slot].conn, BLE_ERR_REM_USER_CONN_TERM);
            s_live[slot].conn = NO_CONN;
        }
        ble_gap_unpair(&s_slots[slot].addr);
        memset(&s_slots[slot], 0, sizeof s_slots[slot]);
        save_slots();
        if (s_target == slot) {
            s_target = -1;
        }
    }
    bridge_say("@forgot %d", slot);
    advertise();
    UNLOCK();
}

void bridge_allow(int slot, bool allowed)
{
    if (slot < 0 || slot >= BRIDGE_SLOTS) {
        return;
    }
    LOCK();
    if (s_slots[slot].used && s_slots[slot].allowed != allowed) {
        s_slots[slot].allowed = allowed;
        // not saved: after a restart every device may come back
        if (!allowed && s_live[slot].conn != NO_CONN) {
            ble_gap_terminate(s_live[slot].conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        advertise();
    }
    UNLOCK();
}

void bridge_target(int slot)
{
    LOCK();
    s_target = slot >= 0 && slot < BRIDGE_SLOTS ? slot : -1;
    UNLOCK();
}

bool bridge_target_ready(void)
{
    LOCK();
    const bool ready = s_target >= 0 && s_live[s_target].conn != NO_CONN;
    UNLOCK();
    return ready;
}

bool bridge_send(uint8_t report_id, const uint8_t* data, int size)
{
    LOCK();
    bool sent = false;
    if (s_target >= 0 && s_live[s_target].conn != NO_CONN) {
        const live_slot_t* live = &s_live[s_target];
        uint16_t handle = 0;
        bool on = false;
        switch (report_id) {
        case REPORT_ID_KEYBOARD: handle = s_kb_in_handle; on = live->kb_on; break;
        case REPORT_ID_MOUSE: handle = s_mouse_handle; on = live->mouse_on; break;
        case REPORT_ID_CONSUMER: handle = s_media_handle; on = live->media_on; break;
        }
        if (on) {
            struct os_mbuf* om = ble_hs_mbuf_from_flat(data, (uint16_t)size);
            sent = om && ble_gatts_notify_custom(live->conn, handle, om) == 0;
        }
    }
    UNLOCK();
    return sent;
}
