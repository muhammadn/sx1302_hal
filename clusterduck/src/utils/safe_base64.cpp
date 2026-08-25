/*
Description:
    Base64 encoding & decoding for opaque/binary CDP payloads. See
    safe_base64.h for the rationale (this replaces libtools/base64.c,
    which calls exit() on invalid input, for this use case).

License: Revised BSD License, see LICENSE.TXT file include in the project
*/

#include "safe_base64.h"

extern "C" {

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

size_t safe_b64_encoded_len(size_t in_len) {
    return ((in_len + 2) / 3) * 4;
}

int safe_b64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_cap) {
    if (out == NULL || (in == NULL && in_len > 0)) {
        return -1;
    }

    size_t needed = safe_b64_encoded_len(in_len);
    if (out_cap < needed + 1) {
        return -1; /* output buffer too small (including null terminator) */
    }

    size_t i = 0;
    size_t o = 0;
    while (i + 3 <= in_len) {
        unsigned int v = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) |
                         (unsigned int)in[i + 2];
        out[o++] = B64_CHARS[(v >> 18) & 0x3F];
        out[o++] = B64_CHARS[(v >> 12) & 0x3F];
        out[o++] = B64_CHARS[(v >> 6) & 0x3F];
        out[o++] = B64_CHARS[v & 0x3F];
        i += 3;
    }

    size_t remaining = in_len - i;
    if (remaining == 1) {
        unsigned int v = (unsigned int)in[i] << 16;
        out[o++] = B64_CHARS[(v >> 18) & 0x3F];
        out[o++] = B64_CHARS[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (remaining == 2) {
        unsigned int v = ((unsigned int)in[i] << 16) | ((unsigned int)in[i + 1] << 8);
        out[o++] = B64_CHARS[(v >> 18) & 0x3F];
        out[o++] = B64_CHARS[(v >> 12) & 0x3F];
        out[o++] = B64_CHARS[(v >> 6) & 0x3F];
        out[o++] = '=';
    }

    out[o] = '\0';
    return (int)o;
}

/* Returns 0-63 for a valid base64 character, -1 for anything else. */
static int b64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return (c - 'a') + 26;
    if (c >= '0' && c <= '9') return (c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int safe_b64_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap) {
    if (in == NULL || out == NULL) {
        return -1;
    }

    /* Tolerate a single trailing newline/CR (common when base64 text is
     * copy/pasted or line-terminated), but nothing else invalid. */
    while (in_len > 0 && (in[in_len - 1] == '\n' || in[in_len - 1] == '\r')) {
        in_len--;
    }

    if (in_len == 0) {
        return 0;
    }
    if (in_len % 4 != 0) {
        return -1; /* standard base64 (with padding) is always a multiple of 4 chars */
    }

    size_t pad = 0;
    if (in[in_len - 1] == '=') {
        pad++;
        if (in_len >= 2 && in[in_len - 2] == '=') {
            pad++;
        }
    }

    size_t out_len = (in_len / 4) * 3 - pad;
    if (out_cap < out_len) {
        return -1;
    }

    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int is_last_block = (i + 4 == in_len);

        int c0 = (in[i] == '=') ? -2 : b64_char_value(in[i]);
        int c1 = (in[i + 1] == '=') ? -2 : b64_char_value(in[i + 1]);
        int c2 = (in[i + 2] == '=') ? -2 : b64_char_value(in[i + 2]);
        int c3 = (in[i + 3] == '=') ? -2 : b64_char_value(in[i + 3]);

        /* The first two characters of every 4-char quantum must always be
         * real data; '=' padding (or any other invalid byte, both of which
         * collapse to a negative value here) is only ever legal in the
         * last quantum's 3rd/4th positions. */
        if (c0 < 0 || c1 < 0) {
            return -1;
        }
        if (!is_last_block && (c2 < 0 || c3 < 0)) {
            return -1;
        }
        if (is_last_block) {
            if (pad == 0 && (c2 < 0 || c3 < 0)) return -1;
            if (pad == 1 && (c2 < 0 || c3 != -2)) return -1;
            if (pad == 2 && (c2 != -2 || c3 != -2)) return -1;
        }

        unsigned int v = ((unsigned int)c0 << 18) | ((unsigned int)c1 << 12) |
                         ((unsigned int)(c2 < 0 ? 0 : c2) << 6) |
                         (unsigned int)(c3 < 0 ? 0 : c3);

        out[o++] = (unsigned char)((v >> 16) & 0xFF);
        if (c2 >= 0) out[o++] = (unsigned char)((v >> 8) & 0xFF);
        if (c3 >= 0) out[o++] = (unsigned char)(v & 0xFF);
    }

    return (int)o;
}

int is_safe_json_text(const unsigned char *data, size_t len) {
    if (data == NULL) {
        return len == 0;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];
        if (c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        if (c < 0x20 || c == 0x7F || c >= 0x80) {
            return 0;
        }
    }
    return 1;
}

} /* extern "C" */
