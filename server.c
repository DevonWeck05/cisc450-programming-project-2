/*
    server.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packet.h"

#define PORT 9000

int main(int argc, char **argv) {
    // create the UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // create server address 
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(PORT); // Listen on UDP port 9000

    // bind socket to port
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // receive buffer large enough for the largest packet
    unsigned char buffer[HEADER_SIZE + MAX_DATA];

    // where recvfrom will store senders address information
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // server hangs here and waits for packets to come in
    int bytes_received = recvfrom(
        sockfd,
        buffer, // buffer filled up with raw bytes
        sizeof(buffer),
        0,
        (struct sockaddr *)&client_addr, // contains the sender address
        &client_len
    );
    if (bytes_received < 0) {
        perror("recvfrom");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // turn raw bytes back into a packet
    packet_t request_pkt;
    if (deserialize_packet(&request_pkt, buffer, bytes_received) < 0) {
        fprintf(stderr, "Invalid packet received\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // should receive a request packet first
    if (request_pkt.type != REQUEST) {
        fprintf(stderr, "Expected REQUEST packet\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // store the filename from request packet
    char filename[MAX_DATA + 1];
    memcpy(filename, request_pkt.data, request_pkt.count);
    filename[request_pkt.count] = '\0';

    printf("Client requested file: %s\n", filename);

    // read requested file
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("fopen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Opened file: %s\n", filename);

    uint16_t base = 1;
    uint16_t next_seq = 1;
    uint16_t window_size = 4;     // temporary hardcoded value

    packet_t packets[65535];
    int packet_sizes[65535];
    int packet_count = 0;

    unsigned char send_buf[HEADER_SIZE + MAX_DATA];

    // file loading loop: start reading file line by line
    char data_buf[MAX_DATA + 1];
    while (fgets(data_buf, sizeof(data_buf), fp) != NULL) {
        int data_len = strlen(data_buf);

        // build data packet 
        packet_t data_pkt;
        data_pkt.type = DATA;
        data_pkt.seq = next_seq;
        data_pkt.count = data_len;

        memcpy(data_pkt.data, data_buf, data_pkt.count);
        packets[next_seq] = data_pkt;
        packet_sizes[next_seq] = HEADER_SIZE + data_pkt.count;

        packet_count++;
        next_seq++;
    }

    // reset base and next_seq for packet sending loop
    base = 1;
    next_seq = 1;

    // sending loop
    while (base <= packet_count) {
        while (
            next_seq <= packet_count &&
            next_seq < base + window_size
        ) {
            int packet_size = serialize_packet(&packets[next_seq], send_buf);

            if (packet_size < 0) {
                fprintf(stderr, "Packet serialization failed\n");
                fclose(fp);
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            int sending_error = sendto(
                sockfd,
                send_buf,
                packet_size,
                0,
                (struct sockaddr *)&client_addr,
                client_len);

            if (sending_error < 0) {
                perror("sendto");
                fclose(fp);
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            printf("DATA %u sent with %u data bytes\n",
                packets[next_seq].seq,
                packets[next_seq].count);

            next_seq++;
        }

        unsigned char ack_buffer[HEADER_SIZE + MAX_DATA];

        int ack_bytes = recvfrom(
            sockfd,
            ack_buffer,
            sizeof(ack_buffer),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (ack_bytes < 0) {
            perror("recvfrom");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        packet_t ack_pkt;

        if (deserialize_packet(&ack_pkt, ack_buffer, ack_bytes) < 0) {
            fprintf(stderr, "Invalid ACK packet received\n");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (ack_pkt.type != ACK) {
            fprintf(stderr, "Expected ACK packet\n");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (ack_pkt.seq >= base && ack_pkt.seq < next_seq) {
            printf("ACK %u received\n", ack_pkt.seq);
            base = ack_pkt.seq + 1;
        }
    }

    printf("Number of original data packets: n");
    printf("Number of data-packet retransmissions: r");
    printf("Total number of original data bytes: b");
    printf("Number of timeouts: t");

    fclose(fp);
    close(sockfd);

    return 0;
}