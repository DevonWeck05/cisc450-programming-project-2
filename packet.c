/*
    packet.c
*/

#include "packet.h"

#include <string.h>
#include <arpa/inet.h>

// prep packet for sending
int serialize_packet(const packet_t *pkt, unsigned char *buffer) {

    // reject if the payload is greater than 80 bytes
    if (pkt->count > MAX_DATA) return -1;

    // convert to network byte order
    uint16_t type_net = htons(pkt->type);
    uint16_t seq_net = htons(pkt->seq);
    uint16_t count_net = htons(pkt->count);

    // put the network type, seq, andf count into the buffer
    memcpy(buffer, &type_net, 2);
    memcpy(buffer + 2, &seq_net, 2);
    memcpy(buffer + 4, &count_net, 2);

    // If theres data, put it in the buffer after the header
    if (pkt->count > 0) {
        memcpy(buffer + 6, pkt->data, pkt->count);
    }

    // return total packet size including header
    return 6 + pkt->count;
}

