/*
    packet.h
    Declares the packet structure and the send/receive functions used by
    both the client and server. Defines MAX_DATA and the interface for
    sending and receiving packets with a 4‑byte header and variable‑length
    data field.
 */

#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

/* Packet types */
#define REQUEST 1
#define DATA    2
#define ACK     3
#define EOT     4

/* Maximum payload size */
#define MAX_DATA 80

typedef struct {
    uint16_t type;
    uint16_t seq;
    uint16_t count;
    char data[MAX_DATA];
} packet_t;

int serialize_packet(const packet_t *pkt, unsigned char *buffer);

int deserialize_packet(packet_t *pkt, const unsigned char *buffer, int len);

#endif