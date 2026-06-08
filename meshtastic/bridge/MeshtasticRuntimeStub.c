#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
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
#define MTK_IPC_DEFAULT_SOCKET_PATH "/tmp/meshtastic-sx1302.sock"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
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

static float read_floatle(const uint8_t *p)
{
    float out;
    uint32_t u = read_u32le(p);
    memcpy(&out, &u, sizeof(out));
    return out;
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

static int send_frame(int fd, uint8_t type, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[MTK_IPC_MAX_FRAME];
    size_t off = 0;
    ssize_t n;

    if ((size_t)(8U + payload_len) > sizeof(frame)) {
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

    if ((payload != NULL) && (payload_len > 0U)) {
        memcpy(frame + off, payload, payload_len);
        off += payload_len;
    }

    n = send(fd, frame, off, MSG_NOSIGNAL);
    if (n < 0) {
        return -1;
    }

    return ((size_t)n == off) ? 0 : -1;
}

static int send_echo_downlink(
    int fd,
    const uint8_t *payload,
    uint16_t payload_len,
    uint32_t freq_hz,
    uint32_t tmst,
    uint32_t bw_hz,
    uint8_t sf,
    uint8_t cr,
    uint8_t rf_chain)
{
    uint8_t msg[1300];
    size_t off = 0;

    if ((size_t)(19U + payload_len) > sizeof(msg)) {
        return -1;
    }

    if (append_u32le(msg, sizeof(msg), &off, freq_hz) != 0 ||
        append_u32le(msg, sizeof(msg), &off, tmst + 200000U) != 0 ||
        append_i16le(msg, sizeof(msg), &off, 14) != 0 ||
        append_u32le(msg, sizeof(msg), &off, bw_hz) != 0 ||
        append_u8(msg, sizeof(msg), &off, sf) != 0 ||
        append_u8(msg, sizeof(msg), &off, cr) != 0 ||
        append_u8(msg, sizeof(msg), &off, rf_chain) != 0 ||
        append_u16le(msg, sizeof(msg), &off, payload_len) != 0) {
        return -1;
    }

    memcpy(msg + off, payload, payload_len);
    off += payload_len;

    return send_frame(fd, MTK_IPC_TYPE_DOWNLINK, msg, (uint16_t)off);
}

int main(void)
{
    const char *socket_path = getenv("MESHTASTIC_IPC_SOCKET");
    const char *echo_env = getenv("MTK_STUB_ECHO_DOWNLINK");
    bool echo_downlink = (echo_env != NULL) && (strcmp(echo_env, "1") == 0);
    int listen_fd = -1;
    int client_fd = -1;
    struct sockaddr_un addr;

    if ((socket_path == NULL) || (socket_path[0] == '\0')) {
        socket_path = MTK_IPC_DEFAULT_SOCKET_PATH;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    unlink(socket_path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) != 0) {
        perror("listen");
        close(listen_fd);
        unlink(socket_path);
        return 1;
    }

    printf("[MTK_STUB] listening on %s\n", socket_path);
    printf("[MTK_STUB] mode: %s\n", echo_downlink ? "echo-downlink" : "log-only");

    while (!g_stop) {
        uint8_t frame[MTK_IPC_MAX_FRAME];
        ssize_t n;

        if (client_fd < 0) {
            client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("accept");
                break;
            }
            printf("[MTK_STUB] client connected\n");
        }

        n = recv(client_fd, frame, sizeof(frame), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            close(client_fd);
            client_fd = -1;
            continue;
        }

        if (n == 0) {
            printf("[MTK_STUB] client disconnected\n");
            close(client_fd);
            client_fd = -1;
            continue;
        }

        if (n < 8) {
            continue;
        }

        if ((frame[0] != MTK_IPC_MAGIC0) || (frame[1] != MTK_IPC_MAGIC1) ||
            (frame[2] != MTK_IPC_MAGIC2) || (frame[3] != MTK_IPC_MAGIC3) ||
            (frame[4] != MTK_IPC_VERSION)) {
            continue;
        }

        {
            uint8_t type = frame[5];
            uint16_t payload_len = read_u16le(frame + 6);
            const uint8_t *payload = frame + 8;

            if ((size_t)(8U + payload_len) != (size_t)n) {
                continue;
            }

            if (type == MTK_IPC_TYPE_HELLO) {
                printf("[MTK_STUB] HELLO: %.*s\n", payload_len, (const char *)payload);
            } else if (type == MTK_IPC_TYPE_UPLINK) {
                if (payload_len >= 23U) {
                    uint32_t freq_hz = read_u32le(payload + 0);
                    uint32_t tmst = read_u32le(payload + 4);
                    int16_t rssi = read_i16le(payload + 8);
                    float snr = read_floatle(payload + 10);
                    uint32_t bw_hz = read_u32le(payload + 14);
                    uint8_t sf = payload[18];
                    uint8_t cr = payload[19];
                    uint8_t rf_chain = payload[20];
                    uint16_t data_len = read_u16le(payload + 21);
                    const uint8_t *data = payload + 23;

                    if ((uint16_t)(23U + data_len) == payload_len) {
                        printf("[MTK_STUB] UPLINK freq=%" PRIu32 " sf=%u bw=%" PRIu32 " cr=%u rssi=%d snr=%.2f len=%u\n",
                               freq_hz, sf, bw_hz, cr, rssi, snr, data_len);
                        if (echo_downlink) {
                            if (send_echo_downlink(client_fd, data, data_len, freq_hz, tmst, bw_hz, sf, cr, rf_chain) == 0) {
                                printf("[MTK_STUB] DOWNLINK echo queued len=%u\n", data_len);
                            }
                        }
                    }
                }
            } else if (type == MTK_IPC_TYPE_APP_SEND) {
                if (payload_len >= 4U) {
                    uint8_t topic = payload[0];
                    uint8_t target_len = payload[1];
                    uint16_t data_len = read_u16le(payload + 2);
                    if ((uint16_t)(4U + target_len + data_len) == payload_len) {
                        printf("[MTK_STUB] APP_SEND topic=%u target_len=%u payload_len=%u\n", topic, target_len, data_len);
                    }
                }
            }
        }
    }

    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    unlink(socket_path);

    printf("[MTK_STUB] stopped\n");
    return 0;
}
