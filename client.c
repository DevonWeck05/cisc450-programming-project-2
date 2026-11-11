/*
    client.c
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
#define LINGER_MS 5000

static long long now_ms(void)
{
    struct timespec t;
    // stores seconds and nanoseconds
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
    // math is pretty much just converting each to ms
}

static int send_packet(int fd, const struct sockaddr *peer, socklen_t size,
                       const packet_t *pkt)
{
    // helper func that sends packets to the server, in this case it is ACKs
    unsigned char wire[HEADER_SIZE + MAX_DATA];
    int n = serialize_packet(pkt, wire);
    if (n < 0) return -1;
    if (lossy_sendto(fd, wire, (size_t)n, 0, peer, size) != n) {
        perror("lossy_sendto");
        return -1;
    }
    return 0;
    // returns 0 on success and -1 on failure
}

int main(int argc, char **argv)
{
    char filename[MAX_DATA + 2];
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "9000";
    // default values for hostname and port if they are not supplied
    double loss = 0.0;
    uint32_t seed = 1;
    char *end;
    if (argc > 6) {
        // in the case where there are more arguments than allowed, an error is thrown
        fprintf(stderr, "Usage: %s [host [port [filename [ack_loss [seed]]]]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 4) {
        // only runs when the user supplies a loss argument
        errno = 0;
        loss = strtod(argv[4], &end);
        if (errno || end == argv[4] || *end) {
            fprintf(stderr, "Invalid loss rate\n");
            return EXIT_FAILURE;
        }
    }
    if (argc > 5) {
        errno = 0;
        unsigned long value = strtoul(argv[5], &end, 10);
        if (errno || end == argv[5] || *end || argv[5][0] == '-' || value > UINT32_MAX) {
            fprintf(stderr, "Invalid seed\n");
            return EXIT_FAILURE;
        }
        // check for an invalid seed, if it is valid, it is stored in seed.
        seed = (uint32_t)value;
    }
    if (lossy_init(loss, seed) < 0) {
        perror("lossy_init");
        return EXIT_FAILURE;
    }
    if (argc > 3) {
        if (!*argv[3] || strlen(argv[3]) > MAX_DATA) {
            fprintf(stderr, "Filename must contain 1 to %d bytes\n", MAX_DATA);
            return EXIT_FAILURE;
        }
        strcpy(filename, argv[3]);
    } else {
        // client prompts user for input, reads from the keyboard
        printf("Filename: ");
        fflush(stdout);
        if (!fgets(filename, sizeof(filename), stdin)) return EXIT_FAILURE;
        filename[strcspn(filename, "\r\n")] = '\0';
        if (!*filename || strlen(filename) > MAX_DATA) {
            fprintf(stderr, "Filename must contain 1 to %d bytes\n", MAX_DATA);
            return EXIT_FAILURE;
        }
    }
    struct addrinfo hints = {0}, *peer = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    // hints describes the types of address that is needed (IPv4, datagram socket)
    int error = getaddrinfo(host, port, &hints, &peer);
    // turns supplied information into an address struct that sock functions can use
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return EXIT_FAILURE;
    }
    int fd = socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    // socket that is used for all server communication
    if (fd < 0) {
        perror("socket");
        freeaddrinfo(peer);
        return EXIT_FAILURE;
    }
    FILE *out = fopen("out.txt", "wb");
    // opens out.txt for writing
    if (!out) {
        perror("out.txt");
        close(fd);
        freeaddrinfo(peer);
        return EXIT_FAILURE;
    }
    // if opening fails, client closes socket and frees addr
    int status = EXIT_FAILURE;
    // client assumes failure initially
    packet_t request = {0};
    request.type = REQUEST;
    request.count = (uint16_t)strlen(filename);
    memcpy(request.data, filename, request.count);
    // creation of a request packet with the given filename
    unsigned char wire[65536];
    int size = serialize_packet(&request, wire);
    // previously created request is converted into wire bytes
    if (sendto(fd, wire, (size_t)size, 0, peer->ai_addr, peer->ai_addrlen) != size) {
        perror("sendto REQUEST");
        goto cleanup;
    }
    // sends request directly to the serrver
    uint16_t expected = 1;
    uint16_t last_ack = 0;
    uint16_t eot_seq = 0;
    // necessary variables for tracking seq #s (default data 1, last_ack 0, and seq 0)
    unsigned long long accepted = 0, discarded = 0, bytes = 0;
    // pretty intuitive, these are meant for tracking accepted, discarded packets, and bytes written
    int lingering = 0;
    long long deadline = now_ms() + IDLE_MS;
    for (;;) {
        long long remaining = deadline - now_ms();
        // calculates remaining time
        if (remaining <= 0) {
           // this is the case of a timeout
            if (lingering) status = EXIT_SUCCESS;
            else fprintf(stderr, "Transfer timed out before EOT; out.txt may be incomplete\n");
            break;
        }
        struct pollfd ready = {fd, POLLIN, 0};
        int result = poll(&ready, 1, (int)remaining);
        // waits for incoming data, time expires, or an error occurs
        if (result < 0) {
            if (errno == EINTR) continue;
            // if a signal gets interrupted, try again
            perror("poll");
            break;
        }
        if (result == 0) continue;
        if (!(ready.revents & POLLIN)) {
            fprintf(stderr, "Socket error\n");
            break;
        }
        struct sockaddr_in source;
        socklen_t source_len = sizeof(source);
        // address of whoever sent the next datagram
        ssize_t n = recvfrom(fd, wire, sizeof(wire), 0,
                             (struct sockaddr *)&source, &source_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }
        const struct sockaddr_in *server = (const struct sockaddr_in *)peer->ai_addr;
        // treats server address as IPv4 address struct
        if (source.sin_family != AF_INET || source.sin_port != server->sin_port ||
            source.sin_addr.s_addr != server->sin_addr.s_addr) continue;
            // conditional checks to make sure that traffic is valid, ignores unrelated UDP traffic
        packet_t pkt;
        if (deserialize_packet(&pkt, wire, (int)n) < 0) {
            // converts bytes into packet structure, and checks that attributes match
            ++discarded;
            continue;
        }
        packet_t ack = {0};
        ack.type = ACK;
        ack.seq = last_ack;
        // necessary for GBN ACKing, prepares ACK for last data packet
        if (pkt.type == DATA) {
            if (!lingering && pkt.seq == expected) {
                if (fwrite(pkt.data, 1, pkt.count, out) != pkt.count) {
                    perror("fwrite");
                    break;
                }
                // packet is acceped if client hasnt already accepted EOT, and seq # matches
                // fwrite writes countn bytes to output file
                ++accepted;
                bytes += pkt.count;
                last_ack = pkt.seq;
                expected = (uint16_t)(expected + 1);
                deadline = now_ms() + IDLE_MS;
                // state is updated after packet is accepted
                printf("DATA %u accepted (%u bytes)\n", pkt.seq, pkt.count);
            } else {
                // duplicate and out of order data is discarded, last cumulative ACK sent again
                ++discarded;
                printf("DATA %u discarded\n", pkt.seq);
            }
            ack.seq = last_ack;
        } else if (pkt.type == EOT && pkt.count == 0) {
            // end of transfer, no payload
            if (!lingering && pkt.seq == expected) {
                if (fflush(out) == EOF) {
                    perror("fflush");
                    break;
                }
                lingering = 1;
                eot_seq = pkt.seq;
                deadline = now_ms() + LINGER_MS;
                // records EOT sequence, and waits for 5 seconds (linger period)
                printf("EOT %u received; lingering for retransmissions\n", pkt.seq);
            }
            if (lingering && pkt.seq == eot_seq) ack.seq = eot_seq;
            else ++discarded;
            // accepted EOT and copies of it receive the same ACK, in case EOT ACK gets or has been lost
        } else {
            // other packet types are simply discarded
            ++discarded;
            continue;
        }
        if (send_packet(fd, peer->ai_addr, peer->ai_addrlen, &ack) < 0) break;
        // sends the ACK
        printf("ACK %u sent\n", ack.seq);
    }
    printf("Accepted data packets: %llu\nDiscarded packets: %llu\nBytes written: %llu\n",
           accepted, discarded, bytes);
           // printing the counters after ACK is sent
cleanup:
    if (fclose(out) == EOF) {
        perror("fclose");
        status = EXIT_FAILURE;
    }
    close(fd);
    freeaddrinfo(peer);
    return status;
}
