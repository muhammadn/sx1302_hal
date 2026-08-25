/*
Description:
    Base64 encoding & decoding for opaque/binary CDP payloads (sealed
    uplink, session-encrypted data, identity announcements, encrypted
    downlink commands). Replaces the vendored libtools/base64.c for this
    purpose: that implementation calls exit() on any invalid input
    character, which lets a single malformed MQTT message or corrupted
    radio packet kill the whole clusterduckd process. Every function here
    reports failure via a return value instead.

License: Revised BSD License, see LICENSE.TXT file include in the project
*/

#ifndef SAFE_BASE64_H
#define SAFE_BASE64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the exact encoded length (excluding the null terminator)
 * for a given input size. Useful for sizing output buffers.
 */
size_t safe_b64_encoded_len(size_t in_len);

/**
 * @brief Encode binary data as a standard (RFC 4648, padded) Base64 string.
 * Never aborts the process on any input.
 *
 * @param in       pointer to the binary data to encode (may be NULL only if in_len is 0).
 * @param in_len   number of bytes to encode.
 * @param out      destination buffer for the encoded, null-terminated string.
 * @param out_cap  capacity of out in bytes, including the terminating null byte.
 * @return number of characters written (excluding the null terminator) on
 *         success, or -1 if out_cap is too small or a required pointer is NULL.
 */
int safe_b64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_cap);

/**
 * @brief Decode a Base64 string into binary data.
 *
 * The entire input is validated (character set, padding, length) before
 * any byte is written to `out`. Invalid input (bad characters, malformed
 * padding, a length that is not a multiple of 4) always returns -1 rather
 * than aborting the process, so it is safe to call directly on untrusted
 * data coming from MQTT commands or the LoRa network.
 *
 * @param in       base64-encoded input; does not need to be null-terminated.
 * @param in_len   number of characters in `in` to decode.
 * @param out      destination buffer for the decoded bytes.
 * @param out_cap  capacity of out in bytes.
 * @return number of bytes written on success, or -1 on any invalid input
 *         or if out_cap is too small.
 */
int safe_b64_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap);

/**
 * @brief Report whether every byte in [data, data+len) is safe to embed
 * directly in a JSON string as human-readable text (printable ASCII plus
 * tab/newline/CR, no NUL, no other control bytes, no raw high-bit bytes).
 *
 * Used to tell legacy plaintext / protobuf-decoded CDP payloads (leave
 * as-is) apart from opaque binary payloads such as ciphertext or a raw
 * public key (must be base64-encoded before they can safely be placed in
 * a JSON message field) -- without needing to hardcode which CDP topics
 * are "the encrypted ones".
 *
 * @return 1 if the whole buffer is safe JSON text, 0 otherwise.
 */
int is_safe_json_text(const unsigned char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SAFE_BASE64_H */
