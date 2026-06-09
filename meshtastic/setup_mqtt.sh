#!/bin/bash
# One-time MQTT configuration for the public Meshtastic broker.
# Run this AFTER meshtasticd is running and reachable on port 4403.
# Settings are persisted in the protobuf state — only needs to run once.
#
# Usage: ./setup_mqtt.sh [host]   (default host: localhost)

HOST="${1:-localhost}"
MTK="meshtastic --host ${HOST}"

echo "Configuring MQTT on meshtasticd at ${HOST}:4403 ..."

# Enable MQTT and point to the public Meshtastic broker
$MTK --set mqtt.enabled true
$MTK --set mqtt.address mqtt.meshtastic.org
$MTK --set mqtt.username meshdev
$MTK --set mqtt.password large4cats
$MTK --set mqtt.encryption_enabled true   # relay encrypted packets as-is
$MTK --set mqtt.tls_enabled false         # public broker uses plain MQTT on 1883
$MTK --set mqtt.json_enabled false        # keep packets encrypted (recommended)
$MTK --set mqtt.root msh/MY_919           # topic prefix for Malaysia 919 MHz

# Allow this gateway's packets to be uplinked
$MTK --set lora.ok_to_mqtt true

echo ""
echo "Done. Restart meshtasticd for settings to take effect."
echo ""
echo "To verify, subscribe to the topic:"
echo "  mosquitto_sub -h mqtt.meshtastic.org -u meshdev -P large4cats -t 'msh/MY_919/#' -v"
