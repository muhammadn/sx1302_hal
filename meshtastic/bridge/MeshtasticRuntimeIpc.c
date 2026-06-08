#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include "MeshtasticSX126xShim.h"

#include "MeshtasticBridge.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MTK_IPC_MAGIC0 'M'
#define MTK_IPC_MAGIC1 'T'
#define MTK_IPC_MAGIC2 'K'
#define MTK_IPC_MAGIC3 '1'

#define MTK_IPC_VERSION 1

#define MTK_IPC_TYPE_HELLO 1
#define MTK_IPC_TYPE_UPLINK 2
#define MTK_IPC_TYPE_DOWNLINK 3
#define MTK_IPC_TYPE_APP_SEND 4

#define MTK_IPC_MAX_FRAME 2048
#define MTK_IPC_MAX_PAYLOAD 1024
#define MTK_IPC_MAX_TARGET_LEN 16
#define MTK_IPC_DEFAULT_SOCKET_PATH "/tmp/meshtastic-sx1302.sock"

#define MTK_UPLINK_QUEUE_CAP 128
#define MTK_UPLINK_MAX_BYTES 255

typedef struct {
    uint16_t payload_len;
    uint8_t payload[MTK_UPLINK_MAX_BYTES];
    int16_t rssi;
    float snr;
    uint32_t freq_hz;
    uint32_t tmst;
    uint8_t rf_chain;
    uint32_t bandwidth_hz;
    uint8_t datarate_sf;
    uint8_t coderate;
} mtk_uplink_item_t;

typedef struct {
    mtk_uplink_item_t items[MTK_UPLINK_QUEUE_CAP];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t dropped;
} mtk_uplink_queue_t;

static pthread_mutex_t g_ipc_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_uplink_mx = PTHREAD_MUTEX_INITIALIZER;

static mtk_uplink_queue_t g_uplink_q;

static int g_ipc_fd = -1;
static bool g_ipc_required = false;
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] = MTK_IPC_DEFAULT_SOCKET_PATH;
static uint64_t g_next_reconnect_ms = 0;
static uint32_t g_reconnect_backoff_ms = 1000;

static uint64_t now_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static size_t bounded_strlen(const char *s, size_t maxlen)
{
    size_t n = 0;

    if (s == NULL) {
        return 0;
    }

    while ((n < maxlen) && (s[n] != '\0')) {
        n++;
    }

    return n;
}

static void ipc_disconnect_locked(void)
{
    if (g_ipc_fd >= 0) {
        close(g_ipc_fd);
        g_ipc_fd = -1;
    }
}

static int append_u8(uint8_t *buf, size_t cap, size_t *off, uint8_t v)
{
    if ((*off + 1U) > cap) {
        return -1;
    }
    buf[*off] = v;
    *off += 1U;
    return 0;
}

static int append_u16le(uint8_t *buf, size_t cap, size_t *off, uint16_t v)
{
    if ((*off + 2U) > cap) {
        return -1;
    }
    buf[*off + 0U] = (uint8_t)(v & 0xFFU);
    buf[*off + 1U] = (uint8_t)((v >> 8) & 0xFFU);
    *off += 2U;
    return 0;
}

static int append_u32le(uint8_t *buf, size_t cap, size_t *off, uint32_t v)
{
    if ((*off + 4U) > cap) {
        return -1;
    }
    buf[*off + 0U] = (uint8_t)(v & 0xFFU);
    buf[*off + 1U] = (uint8_t)((v >> 8) & 0xFFU);
    buf[*off + 2U] = (uint8_t)((v >> 16) & 0xFFU);
    buf[*off + 3U] = (uint8_t)((v >> 24) & 0xFFU);
    *off += 4U;
    return 0;
}

static int append_i16le(uint8_t *buf, size_t cap, size_t *off, int16_t v)
{
    return append_u16le(buf, cap, off, (uint16_t)v);
}

static int append_floatle(uint8_t *buf, size_t cap, size_t *off, float v)
{
    uint32_t as_u32;
    memcpy(&as_u32, &v, sizeof(as_u32));
    return append_u32le(buf, cap, off, as_u32);
}

static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t read_i16le(const uint8_t *p)
{
    return (int16_t)read_u16le(p);
}

static int send_frame_locked(uint8_t type, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[MTK_IPC_MAX_FRAME];
    size_t off = 0;
    ssize_t sent;

    if (g_ipc_fd < 0) {
        return -1;
    }
    if (payload_len > MTK_IPC_MAX_PAYLOAD) {
        return -1;
    }

    if (append_u8(frame, sizeof(frame), &off, MTK_IPC_MAGIC0) != 0 ||
        append_u8(frame, sizeof(frame), &off, MTK_IPC_MAGIC1) != 0 ||
        append_u8(frame, sizeof(frame), &off, MTK_IPC_MAGIC2) != 0 ||
        append_u8(frame, sizeof(frame), &off, MTK_IPC_MAGIC3) != 0 ||
        append_u8(frame, sizeof(frame), &off, MTK_IPC_VERSION) != 0 ||
        append_u8(frame, sizeof(frame), &off, type) != 0 ||
        append_u16le(frame, sizeof(frame), &off, payload_len) != 0) {
        return -1;
    }

    if ((off + payload_len) > sizeof(frame)) {
        return -1;
    }
    if ((payload != NULL) && (payload_len > 0)) {
        memcpy(frame + off, payload, payload_len);
        off += payload_len;
    }

    sent = send(g_ipc_fd, frame, off, MSG_NOSIGNAL);
    if (sent < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return 1;
        }
        ipc_disconnect_locked();
        return -1;
    }

    if ((size_t)sent != off) {
        ipc_disconnect_locked();
        return -1;
    }

    return 0;
}

static int connect_ipc_locked(void)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    {
        size_t n = bounded_strlen(g_socket_path, sizeof(addr.sun_path) - 1);
        memcpy(addr.sun_path, g_socket_path, n);
        addr.sun_path[n] = '\0';
    }

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    g_ipc_fd = fd;
    g_reconnect_backoff_ms = 1000;
    g_next_reconnect_ms = 0;

    {
        const char hello[] = "sx1302_hal";
        (void)send_frame_locked(MTK_IPC_TYPE_HELLO, (const uint8_t *)hello, (uint16_t)(sizeof(hello) - 1));
    }

    printf("[MTK_IPC] connected to runtime socket: %s\n", g_socket_path);
    return 0;
}

static int ensure_connected(void)
{
    uint64_t now;

    pthread_mutex_lock(&g_ipc_mx);
    if (g_ipc_fd >= 0) {
        pthread_mutex_unlock(&g_ipc_mx);
        return 0;
    }

    now = now_monotonic_ms();
    if (now < g_next_reconnect_ms) {
        pthread_mutex_unlock(&g_ipc_mx);
        return -1;
    }

    if (connect_ipc_locked() != 0) {
        g_next_reconnect_ms = now + g_reconnect_backoff_ms;
        if (g_reconnect_backoff_ms < 10000) {
            g_reconnect_backoff_ms *= 2;
            if (g_reconnect_backoff_ms > 10000) {
                g_reconnect_backoff_ms = 10000;
            }
        }
        pthread_mutex_unlock(&g_ipc_mx);
        return -1;
    }

    pthread_mutex_unlock(&g_ipc_mx);
    return 0;
}

static void queue_uplink_packet(
    const uint8_t *payload, uint16_t size,
    int16_t rssi, float snr,
    uint32_t freq_hz, uint32_t tmst, uint8_t rf_chain,
    uint32_t bandwidth_hz, uint8_t datarate_sf, uint8_t coderate)
{
    mtk_uplink_item_t *slot;

    if ((payload == NULL) || (size == 0)) {
        return;
    }

    if (size > MTK_UPLINK_MAX_BYTES) {
        size = MTK_UPLINK_MAX_BYTES;
    }

    pthread_mutex_lock(&g_uplink_mx);
    if (g_uplink_q.count >= MTK_UPLINK_QUEUE_CAP) {
        g_uplink_q.dropped++;
        pthread_mutex_unlock(&g_uplink_mx);
        return;
    }

    slot = &g_uplink_q.items[g_uplink_q.tail];
    slot->payload_len = size;
    memcpy(slot->payload, payload, size);
    slot->rssi = rssi;
    slot->snr = snr;
    slot->freq_hz = freq_hz;
    slot->tmst = tmst;
    slot->rf_chain = rf_chain;
    slot->bandwidth_hz = bandwidth_hz;
    slot->datarate_sf = datarate_sf;
    slot->coderate = coderate;

    g_uplink_q.tail = (uint16_t)((g_uplink_q.tail + 1U) % MTK_UPLINK_QUEUE_CAP);
    g_uplink_q.count++;
    pthread_mutex_unlock(&g_uplink_mx);
}

static void rx_callback(
    const uint8_t *payload, uint16_t size,
    int16_t rssi, float snr,
    uint32_t freq_hz, uint32_t tmst, uint8_t rf_chain,
    uint32_t bandwidth_hz, uint8_t datarate_sf, uint8_t coderate)
{
    queue_uplink_packet(payload, size, rssi, snr, freq_hz, tmst, rf_chain, bandwidth_hz, datarate_sf, coderate);
}

static int flush_uplink_once(void)
{
    mtk_uplink_item_t item;
    uint8_t payload[MTK_IPC_MAX_PAYLOAD];
    size_t off = 0;
    int rc;

    pthread_mutex_lock(&g_uplink_mx);
    if (g_uplink_q.count == 0) {
        pthread_mutex_unlock(&g_uplink_mx);
        return 1;
    }

    item = g_uplink_q.items[g_uplink_q.head];
    pthread_mutex_unlock(&g_uplink_mx);

    if (append_u32le(payload, sizeof(payload), &off, item.freq_hz) != 0 ||
        append_u32le(payload, sizeof(payload), &off, item.tmst) != 0 ||
        append_i16le(payload, sizeof(payload), &off, item.rssi) != 0 ||
        append_floatle(payload, sizeof(payload), &off, item.snr) != 0 ||
        append_u32le(payload, sizeof(payload), &off, item.bandwidth_hz) != 0 ||
        append_u8(payload, sizeof(payload), &off, item.datarate_sf) != 0 ||
        append_u8(payload, sizeof(payload), &off, item.coderate) != 0 ||
        append_u8(payload, sizeof(payload), &off, item.rf_chain) != 0 ||
        append_u16le(payload, sizeof(payload), &off, item.payload_len) != 0) {
        return -1;
    }

    if ((off + item.payload_len) > sizeof(payload)) {
        return -1;
    }
    memcpy(payload + off, item.payload, item.payload_len);
    off += item.payload_len;

    pthread_mutex_lock(&g_ipc_mx);
    rc = send_frame_locked(MTK_IPC_TYPE_UPLINK, payload, (uint16_t)off);
    pthread_mutex_unlock(&g_ipc_mx);

    if (rc == 0) {
        pthread_mutex_lock(&g_uplink_mx);
        if (g_uplink_q.count > 0) {
            g_uplink_q.head = (uint16_t)((g_uplink_q.head + 1U) % MTK_UPLINK_QUEUE_CAP);
            g_uplink_q.count--;
        }
        pthread_mutex_unlock(&g_uplink_mx);
        return 0;
    }

    return rc;
}

static void flush_uplink_queue(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        int rc = flush_uplink_once();
        if (rc != 0) {
            break;
        }
    }
}

static int handle_downlink_payload(const uint8_t *payload, uint16_t len)
{
    uint32_t freq_hz;
    uint32_t tmst;
    int16_t tx_power_dbm;
    uint32_t bw_hz;
    uint8_t sf;
    uint8_t cr;
    uint8_t rf_chain;
    uint16_t data_len;
    const uint8_t *data;

    if ((payload == NULL) || (len < 21U)) {
        return -1;
    }

    freq_hz = read_u32le(payload + 0U);
    tmst = read_u32le(payload + 4U);
    tx_power_dbm = read_i16le(payload + 8U);
    bw_hz = read_u32le(payload + 10U);
    sf = payload[14U];
    cr = payload[15U];
    rf_chain = payload[16U];
    data_len = read_u16le(payload + 17U);

    if ((uint16_t)(19U + data_len) != len) {
        return -1;
    }

    data = payload + 19U;
    return mtk_bridge_enqueue_downlink_ext(data, data_len, freq_hz, tmst, tx_power_dbm, bw_hz, sf, cr, rf_chain);
}

static void poll_runtime_frames(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        uint8_t frame[MTK_IPC_MAX_FRAME];
        ssize_t rlen;
        uint8_t ver;
        uint8_t type;
        uint16_t payload_len;

        pthread_mutex_lock(&g_ipc_mx);
        if (g_ipc_fd < 0) {
            pthread_mutex_unlock(&g_ipc_mx);
            return;
        }

        rlen = recv(g_ipc_fd, frame, sizeof(frame), 0);
        if (rlen < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                pthread_mutex_unlock(&g_ipc_mx);
                return;
            }
            ipc_disconnect_locked();
            pthread_mutex_unlock(&g_ipc_mx);
            return;
        }

        if (rlen == 0) {
            ipc_disconnect_locked();
            pthread_mutex_unlock(&g_ipc_mx);
            return;
        }

        pthread_mutex_unlock(&g_ipc_mx);

        if (rlen < 8) {
            continue;
        }
        if ((frame[0] != MTK_IPC_MAGIC0) || (frame[1] != MTK_IPC_MAGIC1) || (frame[2] != MTK_IPC_MAGIC2) || (frame[3] != MTK_IPC_MAGIC3)) {
            continue;
        }

        ver = frame[4];
        type = frame[5];
        payload_len = read_u16le(frame + 6);

        if (ver != MTK_IPC_VERSION) {
            continue;
        }
        if ((size_t)(8U + payload_len) != (size_t)rlen) {
            continue;
        }

        if (type == MTK_IPC_TYPE_DOWNLINK) {
            (void)handle_downlink_payload(frame + 8, payload_len);
        }
    }
}

static void read_runtime_env(void)
{
    const char *sock_path = getenv("MESHTASTIC_IPC_SOCKET");
    const char *required = getenv("MESHTASTIC_IPC_REQUIRED");

    if ((sock_path != NULL) && (sock_path[0] != '\0')) {
        strncpy(g_socket_path, sock_path, sizeof(g_socket_path) - 1);
        g_socket_path[sizeof(g_socket_path) - 1] = '\0';
    }

    if ((required != NULL) &&
        ((strcmp(required, "1") == 0) || (strcasecmp(required, "true") == 0) || (strcasecmp(required, "yes") == 0))) {
        g_ipc_required = true;
    }
}

int meshtastic_sx126x_runtime_init(void)
{
    read_runtime_env();
    mtk_bridge_register_rx_callback(rx_callback);

    if (ensure_connected() != 0) {
        if (g_ipc_required) {
            printf("[MTK_IPC] ERROR: runtime IPC is required but unavailable at %s\n", g_socket_path);
            return -1;
        }
        printf("[MTK_IPC] WARNING: runtime IPC unavailable at %s (running degraded)\n", g_socket_path);
    }

    return 0;
}

void meshtastic_sx126x_runtime_loop(void)
{
    (void)ensure_connected();
    flush_uplink_queue();
    poll_runtime_frames();
}

int meshtastic_sx126x_runtime_send(
    const uint8_t *payload,
    uint16_t size,
    uint8_t topic,
    const uint8_t *target_device,
    uint8_t target_len)
{
    uint8_t out[MTK_IPC_MAX_PAYLOAD];
    size_t off = 0;
    int rc;

    if ((payload == NULL) || (size == 0)) {
        return -1;
    }

    if (target_len > MTK_IPC_MAX_TARGET_LEN) {
        target_len = MTK_IPC_MAX_TARGET_LEN;
    }

    if (ensure_connected() != 0) {
        return -1;
    }

    if (append_u8(out, sizeof(out), &off, topic) != 0 ||
        append_u8(out, sizeof(out), &off, target_len) != 0 ||
        append_u16le(out, sizeof(out), &off, size) != 0) {
        return -1;
    }

    if ((target_len > 0) && (target_device != NULL)) {
        if ((off + target_len) > sizeof(out)) {
            return -1;
        }
        memcpy(out + off, target_device, target_len);
        off += target_len;
    }

    if ((off + size) > sizeof(out)) {
        return -1;
    }
    memcpy(out + off, payload, size);
    off += size;

    pthread_mutex_lock(&g_ipc_mx);
    rc = send_frame_locked(MTK_IPC_TYPE_APP_SEND, out, (uint16_t)off);
    pthread_mutex_unlock(&g_ipc_mx);

    return (rc == 0) ? 0 : -1;
}
