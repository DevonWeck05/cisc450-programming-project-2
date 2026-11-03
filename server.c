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

    // Stuff will go here in steps 5 and 6 and 7

    printf("Received %d bytes from client\n", bytes_received);

    close(sockfd);

    return 0;
}