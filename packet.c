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
        memcpy(buffer + HEADER_SIZE, pkt->data, pkt->count);
    }

    // return total packet size including header
    return HEADER_SIZE + pkt->count;
}

int deserialize_packet(packet_t *pkt, const unsigned char *buffer, int len) {
    // verify complete header at least
    if (len < HEADER_SIZE) return -1;

    uint16_t type_net;
    uint16_t seq_net;
    uint16_t count_net;

    // put the packet header into the buffer
    memcpy(&type_net, buffer, 2);
    memcpy(&seq_net, buffer + 2, 2);
    memcpy(&count_net, buffer + 4, 2);

    // convert to host byte order and rebuild the packet
    pkt->type = ntohs(type_net);
    pkt->seq = ntohs(seq_net);
    pkt->count = ntohs(count_net);

    // validate count is correct
    if (pkt->count > MAX_DATA) return -1;

    // validate that the claimed length equals the recieved length
    if (len != HEADER_SIZE + pkt->count) return -1;

    // copy the data into the buffer after validating 
    if (pkt->count > 0) {
        memcpy(pkt->data, buffer + HEADER_SIZE, pkt->count);
    }

    // no errors
    return 0;
}