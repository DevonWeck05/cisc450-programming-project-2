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

// Max bytes in a datapacket
#define MAX_DATA 80

int send_all(int sock, const void *buf, size_t len);
int send_packet(int sock, uint16_t seq, uint16_t count, const char *data);

int recv_exact(int sock, void *buf, size_t len);
int recv_packet(int sock, uint16_t *seq, uint16_t *count, char *data);

#endif