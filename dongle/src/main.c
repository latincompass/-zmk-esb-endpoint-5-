/*
 * Copyright (c) 2025 efogdev
 * SPDX-License-Identifier: MIT
 *
 * nRF52833 ESB dongle (PRX) firmware.
 *
 * Receives HID reports from a ZMK keyboard running the zmk-esb-endpoint
 * module and forwards them as USB HID to the host.
 *
 * Protocol: see zmk_esb/protocol.h for the full wire format.
 *
 * Architecture:
 *   - ESB is configured as PRX (receiver) on two pipes:
 *     pipe 0 (pairing):  receives BEACON, sends PAIR_REQ in ACK
 *     pipe 1 (data):     receives HID_REPORT, VERIFY_REQ, etc.
 *   - USB HID device sends keyboard/consumer/mouse reports to the host.
 *   - Channel hopping protocol mirrors the keyboard endpoint.
 *
 * Build with Zephyr + this module's vendor/nrf-esb library.
 *   west build -b nrf52833dk_nrf52833 -s dongle
 *   (or your own board if different, with a suitable .overlay to configure
 *    USB and the ESB endpoint addresses matching the keyboard's DTS).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_ppi.h>
#include <hal/nrf_ficr.h>
#include <hal/nrf_timer.h>
#include <esb.h>

#include <zmk_esb/protocol.h>

LOG_MODULE_REGISTER(zmk_esb_dongle, LOG_LEVEL_INF);

/* ===================================================================
 * USB HID Report Descriptors
 * =================================================================== */

/* Combined keyboard + consumer + mouse HID report descriptor */
static const uint8_t hid_report_desc[] = {
    /* Keyboard Report (report ID 1) */
    0x05, 0x01,                    /* Usage Page (Generic Desktop) */
    0x09, 0x06,                    /* Usage (Keyboard) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, 0x01,                    /*   Report ID (1) */
    0x05, 0x07,                    /*   Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,                    /*   Usage Minimum (Left Control) */
    0x29, 0xE7,                    /*   Usage Maximum (Right GUI) */
    0x15, 0x00,                    /*   Logical Minimum (0) */
    0x25, 0x01,                    /*   Logical Maximum (1) */
    0x75, 0x01,                    /*   Report Size (1) */
    0x95, 0x08,                    /*   Report Count (8) */
    0x81, 0x02,                    /*   Input (Data,Var,Abs) */
    0x95, 0x01,                    /*   Report Count (1) */
    0x75, 0x08,                    /*   Report Size (8) */
    0x81, 0x01,                    /*   Input (Const) - padding */
    0x95, 0x06,                    /*   Report Count (6) */
    0x75, 0x08,                    /*   Report Size (8) */
    0x15, 0x00,                    /*   Logical Minimum (0) */
    0x25, 0x65,                    /*   Logical Maximum (101) */
    0x05, 0x07,                    /*   Usage Page (Keyboard/Keypad) */
    0x19, 0x00,                    /*   Usage Minimum (0) */
    0x29, 0x65,                    /*   Usage Maximum (101) */
    0x81, 0x00,                    /*   Input (Data,Array) */
    0xC0,                          /* End Collection */

    /* Consumer Report (report ID 2) */
    0x05, 0x0C,                    /* Usage Page (Consumer) */
    0x09, 0x01,                    /* Usage (Consumer Control) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, 0x02,                    /*   Report ID (2) */
    0x15, 0x00,                    /*   Logical Minimum (0) */
    0x25, 0x01,                    /*   Logical Maximum (1) */
    0x09, 0xCD,                    /*   Usage (Play/Pause) */
    0x0A, 0x00, 0x02,             /*   Usage (Volume Increment) */
    0x0A, 0x01, 0x02,             /*   Usage (Volume Decrement) */
    0x0A, 0x23, 0x02,             /*   Usage (Mute) */
    0x0A, 0x6F, 0x02,             /*   Usage (Scan Next) */
    0x0A, 0x70, 0x02,             /*   Usage (Scan Previous) */
    0x0A, 0xEA, 0x01,             /*   Usage (Wheel) */
    0x75, 0x01,                    /*   Report Size (1) */
    0x95, 0x10,                    /*   Report Count (16) */
    0x81, 0x02,                    /*   Input (Data,Var,Abs) */
    0xC0,                          /* End Collection */

    /* Mouse Report (report ID 3) */
    0x05, 0x01,                    /* Usage Page (Generic Desktop) */
    0x09, 0x02,                    /* Usage (Mouse) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, 0x03,                    /*   Report ID (3) */
    0x09, 0x01,                    /*   Usage (Pointer) */
    0xA1, 0x00,                    /*   Collection (Physical) */
    0x05, 0x09,                    /*     Usage Page (Button) */
    0x19, 0x01,                    /*     Usage Minimum (Button 1) */
    0x29, 0x05,                    /*     Usage Maximum (Button 5) */
    0x15, 0x00,                    /*     Logical Minimum (0) */
    0x25, 0x01,                    /*     Logical Maximum (1) */
    0x75, 0x01,                    /*     Report Size (1) */
    0x95, 0x05,                    /*     Report Count (5) */
    0x81, 0x02,                    /*     Input (Data,Var,Abs) */
    0x95, 0x03,                    /*     Report Count (3) */
    0x81, 0x01,                    /*     Input (Const) - padding */
    0x05, 0x01,                    /*     Usage Page (Generic Desktop) */
    0x09, 0x30,                    /*     Usage (X) */
    0x09, 0x31,                    /*     Usage (Y) */
    0x09, 0x38,                    /*     Usage (Wheel) */
    0x15, 0x81,                    /*     Logical Minimum (-127) */
    0x25, 0x7F,                    /*     Logical Maximum (127) */
    0x75, 0x08,                    /*     Report Size (8) */
    0x95, 0x03,                    /*     Report Count (3) */
    0x81, 0x06,                    /*     Input (Data,Var,Rel) */
    0x05, 0x0C,                    /*     Usage Page (Consumer) */
    0x0A, 0x38, 0x02,             /*     Usage (AC Pan) */
    0x15, 0x81,                    /*     Logical Minimum (-127) */
    0x25, 0x7F,                    /*     Logical Maximum (127) */
    0x75, 0x08,                    /*     Report Size (8) */
    0x95, 0x01,                    /*     Report Count (1) */
    0x81, 0x06,                    /*     Input (Data,Var,Rel) */
    0xC0,                          /*   End Collection */
    0xC0,                          /* End Collection */
};

/* ===================================================================
 * USB HID callbacks
 * =================================================================== */

static const struct device *hid_dev;

static int hid_cb(const struct device *dev, struct usb_setup_packet *setup,
                  int32_t *len, uint8_t **data)
{
    return 0;
}

static const struct hid_ops hid_ops = {
    .intr_in_ready = NULL,
    .set_config = NULL,
    .set_idle = NULL,
    .get_idle = NULL,
    .protocol_change = NULL,
    .get_report = NULL,
};

/* ===================================================================
 * ESB PRX configuration
 * =================================================================== */

/* Addresses - MUST match the keyboard's DTS configuration.
 * The dongle's addresses are the same as the keyboard: */
static const uint8_t pairing_base_addr[4] = { 0x17, 0xF4, 0x07, 0xAA };
static const uint8_t data_base_addr[4]    = { 0xB9, 0x8A, 0x16, 0x22 };
static const uint8_t addr_prefixes[8]     = {
    0x24,       /* pairing pipe prefix */
    0xC2,       /* data pipe prefix */
    0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
};
static const uint8_t default_channel = 78;

/* On nRF52833: RADIO_IRQn = 1, NVIC vectors = 64 */
#define RADIO_IRQn 1
#define NVIC_NUM_VECTORS 64

/* BT_LL PPI mask - not needed in the dongle (no BLE), but kept for clarity */
static uint32_t m_ram_vtor[NVIC_NUM_VECTORS] __aligned(256);

/* Peer keyboard identity */
static struct {
    uint8_t device_id[6];
    bool    has_peer;
} m_peer;

/* Dongle's own FICR device_id */
static uint8_t m_my_device_id[6];

static struct esb_payload m_tx_payload;
static struct esb_payload m_rx_payload;

/* Pairing state */
enum dongle_state {
    DONGLE_IDLE,
    DONGLE_LISTENING,     /* waiting for BEACON */
    DONGLE_PAIRING,       /* PAIR_REQ sent, waiting for PAIR_RESP */
    DONGLE_CONNECTED,     /* paired and receiving data */
    DONGLE_VERIFYING,     /* reconnect verification */
};
static enum dongle_state m_state = DONGLE_IDLE;

/* ===================================================================
 * USB HID send helpers
 * =================================================================== */

static int send_hid_report(uint8_t report_id, const uint8_t *data, uint8_t len)
{
    return hid_int_ep_write(hid_dev, data, len, NULL);
}

static void forward_keyboard(const uint8_t *data, uint8_t len)
{
    /* Report ID 1 + 8-byte keyboard report (modifiers + reserved + keys[6]) */
    uint8_t report[9] = { 0 };
    report[0] = 1; /* Report ID */
    if (len > 8) {
        len = 8;
    }
    memcpy(&report[1], data, len);
    send_hid_report(1, report, sizeof(report));
}

static void forward_consumer(const uint8_t *data, uint8_t len)
{
    /* Report ID 2 + 2-byte consumer report */
    uint8_t report[3] = { 0 };
    report[0] = 2; /* Report ID */
    if (len > 2) {
        len = 2;
    }
    memcpy(&report[1], data, len);
    send_hid_report(2, report, sizeof(report));
}

static void forward_mouse(const uint8_t *data, uint8_t len)
{
    /* Report ID 3 + 6-byte mouse report (buttons[1], dx[1], dy[1], wheel[1], pan[1]) */
    uint8_t report[7] = { 0 };
    report[0] = 3; /* Report ID */
    if (len > 6) {
        len = 6;
    }
    memcpy(&report[1], data, len);
    send_hid_report(3, report, sizeof(report));
}

/* ===================================================================
 * ESB event handler
 * =================================================================== */

static void esb_evt_handler(struct esb_evt const *event)
{
    switch (event->evt_id) {
    case ESB_EVENT_RX_RECEIVED:
        while (esb_read_rx_payload(&m_rx_payload) == 0) {
            if (m_rx_payload.length < 1) {
                continue;
            }

            const uint8_t *data = m_rx_payload.data;
            const uint8_t len   = m_rx_payload.length;
            const uint8_t pipe  = m_rx_payload.pipe;

            switch (data[0]) {
            case ESB_PKT_BEACON:
                if (len >= sizeof(struct esb_pkt_beacon) && m_state == DONGLE_LISTENING) {
                    const struct esb_pkt_beacon *beacon = (const void *)data;
                    memcpy(m_peer.device_id, beacon->device_id,
                           sizeof(m_peer.device_id));
                    m_peer.has_peer = true;

                    /* Send PAIR_REQ in ACK */
                    struct esb_pkt_pair_req ack = {
                        .type = ESB_PKT_PAIR_REQ,
                    };
                    memcpy(ack.device_id, m_my_device_id, sizeof(m_my_device_id));
                    m_tx_payload.pipe = pipe;
                    m_tx_payload.length = sizeof(ack);
                    memcpy(m_tx_payload.data, &ack, sizeof(ack));
                    esb_write_payload(&m_tx_payload);

                    m_state = DONGLE_PAIRING;
                    LOG_INF("BEACON from %02X%02X%02X%02X%02X%02X, sending PAIR_REQ",
                            m_peer.device_id[0], m_peer.device_id[1],
                            m_peer.device_id[2], m_peer.device_id[3],
                            m_peer.device_id[4], m_peer.device_id[5]);
                }
                break;

            case ESB_PKT_PAIR_RESP:
                if (len >= sizeof(struct esb_pkt_pair_resp) && m_state == DONGLE_PAIRING) {
                    LOG_INF("PAIR_RESP received, connected");
                    m_state = DONGLE_CONNECTED;
                }
                break;

            case ESB_PKT_HID_REPORT:
                if (m_state != DONGLE_CONNECTED && m_state != DONGLE_VERIFYING) {
                    break;
                }
                if (len >= 2) {
                    const uint8_t report_type = data[1];
                    const uint8_t *body = &data[2];
                    const uint8_t body_len = len - 2;

                    switch (report_type) {
                    case ESB_REPORT_KEYBOARD:
                        forward_keyboard(body, body_len);
                        break;
                    case ESB_REPORT_CONSUMER:
                        forward_consumer(body, body_len);
                        break;
                    case ESB_REPORT_MOUSE:
                        forward_mouse(body, body_len);
                        break;
                    }
                }
                break;

            case ESB_PKT_VERIFY_REQ:
                if (len >= sizeof(struct esb_pkt_verify_req)) {
                    const struct esb_pkt_verify_req *req = (const void *)data;
                    if (m_peer.has_peer &&
                        memcmp(req->device_id, m_peer.device_id,
                               sizeof(m_peer.device_id)) == 0) {
                        /* Send VERIFY_RESP in ACK */
                        struct esb_pkt_verify_resp resp = {
                            .type = ESB_PKT_VERIFY_RESP,
                        };
                        memcpy(resp.device_id, m_my_device_id,
                               sizeof(m_my_device_id));
                        m_tx_payload.pipe = pipe;
                        m_tx_payload.length = sizeof(resp);
                        memcpy(m_tx_payload.data, &resp, sizeof(resp));
                        esb_write_payload(&m_tx_payload);

                        m_state = DONGLE_VERIFYING;
                        LOG_DBG("VERIFY_REQ accepted");
                    } else {
                        /* Unknown keyboard - send DISCONNECT */
                        struct esb_pkt_disconnect disc = {
                            .type = ESB_PKT_DISCONNECT,
                        };
                        m_tx_payload.pipe = pipe;
                        m_tx_payload.length = sizeof(disc);
                        memcpy(m_tx_payload.data, &disc, sizeof(disc));
                        esb_write_payload(&m_tx_payload);
                    }
                }
                break;

            case ESB_PKT_DISCONNECT:
                LOG_INF("Keyboard disconnected");
                m_state = DONGLE_LISTENING;
                break;

            default:
                break;
            }
        }
        break;

    case ESB_EVENT_TX_SUCCESS:
    case ESB_EVENT_TX_FAILED:
        break;
    }
}

/* ===================================================================
 * ESB init
 * =================================================================== */

static int esb_dongle_init(void)
{
    struct esb_config cfg = ESB_DEFAULT_CONFIG;
    cfg.protocol           = ESB_PROTOCOL_ESB_DPL;
    cfg.mode               = ESB_MODE_PRX;
    cfg.bitrate            = ESB_BITRATE_1MBPS_BLE;
    cfg.crc                = ESB_CRC_16BIT;
    cfg.retransmit_count   = 5;
    cfg.retransmit_delay   = 570;
    cfg.tx_mode            = ESB_TXMODE_AUTO;
    cfg.use_fast_ramp_up   = true;
    cfg.tx_output_power    = ESB_TX_POWER_8DBM;
    cfg.selective_auto_ack = true;
    cfg.event_handler      = esb_evt_handler;
    cfg.payload_length     = 32;

    int err = esb_init(&cfg);
    if (err) {
        LOG_ERR("esb_init failed: %d", err);
        return err;
    }

    /* Set addresses matching the keyboard's DTS */
    esb_set_base_address_0(pairing_base_addr);
    esb_set_base_address_1(data_base_addr);
    esb_set_prefixes(addr_prefixes, 2);
    esb_set_rf_channel(default_channel);

    /* Enable pipes 0 (pairing) and 1 (data) */
    esb_enable_pipes(0x03);

    LOG_INF("ESB PRX initialized on ch%d", default_channel);
    return 0;
}

/* ===================================================================
 * VTOR swap for RADIO IRQ
 * =================================================================== */

static void install_ram_vtor(void)
{
    const uint32_t *flash_vtor = (const uint32_t *)(volatile void *)
        ((volatile uint32_t *)0xE000ED08);  /* SCB->VTOR */
    memcpy(m_ram_vtor, flash_vtor, sizeof(m_ram_vtor));

    m_ram_vtor[16 + RADIO_IRQn] =
        (uint32_t)z_arm_irq_direct_dynamic_dispatch_reschedule;

    unsigned int key = irq_lock();
    __DSB();
    volatile uint32_t *scb_vtor = (volatile uint32_t *)0xE000ED08;
    *scb_vtor = (uint32_t)m_ram_vtor;
    __DSB();
    __ISB();
    irq_unlock(key);

    LOG_DBG("RAM VTOR installed");
}

/* ===================================================================
 * Build FICR device_id
 * =================================================================== */

static void build_device_id(void)
{
    const uint32_t lo = nrf_ficr_deviceid_get(NRF_FICR, 0);
    const uint32_t hi = nrf_ficr_deviceid_get(NRF_FICR, 1);
    memcpy(&m_my_device_id[0], &lo, 4);
    memcpy(&m_my_device_id[4], &hi, 2);
}

/* ===================================================================
 * Main
 * =================================================================== */

void main(void)
{
    LOG_INF("ZMK ESB Dongle (nRF52833 PRX) starting");

    /* Build local device_id */
    build_device_id();
    LOG_INF("Device ID: %02X%02X%02X%02X%02X%02X",
            m_my_device_id[0], m_my_device_id[1],
            m_my_device_id[2], m_my_device_id[3],
            m_my_device_id[4], m_my_device_id[5]);

    /* Install RAM vector table with ESB RADIO handler */
    install_ram_vtor();

    /* Initialize ESB as PRX */
    if (esb_dongle_init() != 0) {
        LOG_ERR("ESB init failed, halting");
        return;
    }

    /* Initialize USB HID */
    hid_dev = device_get_binding("HID_0");
    if (!hid_dev) {
        LOG_ERR("HID device not found");
        return;
    }

    if (usb_enable(NULL) != 0) {
        LOG_ERR("USB enable failed");
        return;
    }

    m_state = DONGLE_LISTENING;
    LOG_INF("Dongle ready, listening for keyboard BEACON");

    while (1) {
        k_sleep(K_FOREVER);
    }
}
