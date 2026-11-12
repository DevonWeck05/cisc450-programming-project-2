/*
    client.c - requests a file over UDP, writes in-order DATA packets to out.txt,
    and sends cumulative ACKs with reliable EOT acknowledgment.
*/

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "packet.h"
#include "lossy.h"

#define IDLE_MS 30000

static long long now_ms(void) {
    // stores seconds and nanoseconds
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    // math is pretty much just converting each to ms
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// helper func that sends packets to the server, in this case it is ACKs
static int send_packet(int fd, const struct sockaddr *peer, socklen_t size, const packet_t *pkt) {
    unsigned char wire[HEADER_SIZE + MAX_DATA];
    int n = serialize_packet(pkt, wire);
    if (n < 0) return -1;
    if (lossy_sendto(fd, wire, (size_t)n, 0, peer, size) != n) {
        perror("lossy_sendto");
        return -1;
    }

    // returns 0 on success and -1 on failure
    return 0;
}

int main(int argc, char **argv) {
    char filename[MAX_DATA + 2];

    if (argc != 6) {
        fprintf(stderr,
            "Usage: %s <server-host> <port> <timeout-ms> <ack-loss-rate> <seed>\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];
    const char *port = argv[2];

    int timeout_ms = atoi(argv[3]);
    double loss = atof(argv[4]);
    uint32_t seed = (uint32_t)strtoul(argv[5], NULL, 10);

    if (timeout_ms <= 0) {
        fprintf(stderr, "Invalid timeout\n");
        return EXIT_FAILURE;
    }

    if (loss < 0.0 || loss >= 1.0) {
        fprintf(stderr, "Invalid loss rate\n");
        return EXIT_FAILURE;
    }

    printf("Filename: ");
    fflush(stdout);

    if (!fgets(filename, sizeof(filename), stdin)) {
        return EXIT_FAILURE;
    }

    filename[strcspn(filename, "\r\n")] = '\0';

    if (!*filename || strlen(filename) > MAX_DATA) {
        fprintf(stderr, "Filename must contain 1 to %d bytes\n", MAX_DATA);
        return EXIT_FAILURE;
    }

    if (lossy_init(loss, seed) < 0) {
        perror("lossy_init");
        return EXIT_FAILURE;
    }

    // hints describes the types of address that is needed (IPv4, datagram socket)
    struct addrinfo hints = {0}, *peer = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    // turns supplied information into an address struct that sock functions can use
    int error = getaddrinfo(host, port, &hints, &peer);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return EXIT_FAILURE;
    }
     
    // socket that is used for all server communication
    int fd = socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(peer);
        return EXIT_FAILURE;
    }

    // opens out.txt for writing
    FILE *out = fopen("out.txt", "wb");
    if (!out) {
        perror("out.txt");
        close(fd);
        freeaddrinfo(peer);
        return EXIT_FAILURE;
    }

    // client assumes failure initially
    int status = EXIT_FAILURE;
    
    // creation of a request packet with the given filename
    packet_t request = {0};
    request.type = REQUEST;
    request.count = (uint16_t)strlen(filename);
    memcpy(request.data, filename, request.count);

    // previously created request is converted into wire bytes and send
    unsigned char wire[65536];
    int size = serialize_packet(&request, wire);
    if (sendto(fd, wire, (size_t)size, 0, peer->ai_addr, peer->ai_addrlen) != size) {
        perror("sendto REQUEST");
        goto cleanup;
    }

    // necessary variables for tracking seq #s (default data 1, last_ack 0, and seq 0)
    uint16_t expected = 1;
    uint16_t last_ack = 0;
    uint16_t eot_seq = 0;

    // pretty intuitive, these are meant for tracking accepted, discarded packets, and bytes written
    unsigned long long accepted = 0, discarded = 0, bytes = 0;
    int lingering = 0;
    long long deadline = now_ms() + IDLE_MS;
    for (;;) {
        // calculates remaining time
        long long remaining = deadline - now_ms();
        
        // this is the case of a timeout
        if (remaining <= 0) {
            if (lingering) status = EXIT_SUCCESS;
            else fprintf(stderr, "Transfer timed out before EOT; out.txt may be incomplete\n");
            break;
        }

        // waits for incoming data, time expires, or an error occurs
        struct pollfd ready = {fd, POLLIN, 0};
        int result = poll(&ready, 1, (int)remaining);
        if (result < 0) {
            // if a signal gets interrupted, try again
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (result == 0) continue;
        if (!(ready.revents & POLLIN)) {
            fprintf(stderr, "Socket error\n");
            break;
        }

        // address of whoever sent the next datagram
        struct sockaddr_in source;
        socklen_t source_len = sizeof(source);
        ssize_t n = recvfrom(
            fd,
            wire,
            sizeof(wire),
            0,
            (struct sockaddr *)&source,
            &source_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        // treats server address as IPv4 address struct
        const struct sockaddr_in *server = (const struct sockaddr_in *)peer->ai_addr;

        // conditional checks to make sure that traffic is valid, ignores unrelated UDP traffic
        if (source.sin_family != AF_INET || source.sin_port != server->sin_port ||
            source.sin_addr.s_addr != server->sin_addr.s_addr) continue;

        // converts bytes into packet structure, and checks that attributes match
        packet_t pkt;
        if (deserialize_packet(&pkt, wire, (int)n) < 0) {    
            continue;
        }
        if ((pkt.type == DATA &&
             (pkt.count == 0 || pkt.seq == 0 || pkt.seq == UINT16_MAX)) ||
            (pkt.type == EOT && (pkt.count != 0 || pkt.seq == 0))) {
            continue;
        }

        // necessary for GBN ACKing, prepares ACK for last data packet
        packet_t ack = {0};
        ack.type = ACK;
        ack.seq = last_ack;
        int resent_ack = 0;
        if (pkt.type == DATA) {
            if (!lingering && pkt.seq == expected) {
                // packet is acceped if client hasnt already accepted EOT, and seq # matches
                // fwrite writes countn bytes to output file
                if (fwrite(pkt.data, 1, pkt.count, out) != pkt.count) {
                    perror("fwrite");
                    break;
                }
                // state is updated after packet is accepted
                ++accepted;
                bytes += pkt.count;
                last_ack = pkt.seq;
                expected = (uint16_t)(expected + 1);
                deadline = now_ms() + IDLE_MS;

                printf("DATA %u received with %u data bytes\n", pkt.seq, pkt.count);
            } else { // duplicate and out of order data is discarded, last cumulative ACK sent again
                ++discarded;
                printf("DATA %u discarded; expected packet %u\n", pkt.seq, expected);
                resent_ack = 1;
            }

            ack.seq = last_ack;
        } else if (pkt.type == EOT) { // end of transfer, no payload
            if (!lingering && pkt.seq == expected) {
                if (fclose(out) == EOF) {
                    perror("fclose out.txt");
                    out = NULL;
                    break;
                }
                out = NULL;
                lingering = 1;
                eot_seq = pkt.seq;
                deadline = now_ms() + (2 * timeout_ms);
                printf("EOT %u received\n", pkt.seq);
            } else if (lingering && pkt.seq == eot_seq) {
                deadline = now_ms() + (2 * timeout_ms);
                printf("EOT %u received again\n", pkt.seq);
            } else {
                continue;
            }
            ack.seq = eot_seq;
        } else {
            // Other packet types do not contribute to the DATA discard count.
            continue;
        }

        // sends the ACK
        if (send_packet(fd, peer->ai_addr, peer->ai_addrlen, &ack) < 0) break;
        printf("ACK %u %s\n", ack.seq, resent_ack ? "resent" : "sent");
    }
    printf("Number of data packets accepted: %llu\n", accepted);
    printf("Number of data packets discarded: %llu\n", discarded);
    printf("Total number of data bytes written: %llu\n", bytes);

cleanup:
    if (out && fclose(out) == EOF) {
        perror("fclose");
        status = EXIT_FAILURE;
    }
    close(fd);
    freeaddrinfo(peer);
    return status;
}
