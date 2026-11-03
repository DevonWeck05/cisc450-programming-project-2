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

int main() {
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


    
    printf("Received %d bytes from client\n", bytes_received);

    fclose(fp);
    close(sockfd);

    return 0;
}