/*
    server.c - sends requested files over UDP using Go-Back-N
*/

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packet.h"
#include "lossy.h"

// checks whether a packet came from the requesting client
static int same_peer(const struct sockaddr_in *left,
                     const struct sockaddr_in *right)
{
    return left->sin_family == right->sin_family &&
           left->sin_port == right->sin_port &&
           left->sin_addr.s_addr == right->sin_addr.s_addr;
}

// gets the current monotonic time in milliseconds
static int monotonic_ms(long long *milliseconds)
{
    struct timespec current;
    if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) return -1;

    *milliseconds = (long long)current.tv_sec * 1000 +
                    current.tv_nsec / 1000000;
    return 0;
}

int main(int argc, char **argv) {
    // command line parsing
    if (argc != 6) {
        fprintf(stderr,
            "Usage: %s <port> <window-size> <timeout-ms> <data-loss-rate> <seed>\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    int window_size = atoi(argv[2]);
    int timeout_ms = atoi(argv[3]);
    double loss_rate = atof(argv[4]);
    unsigned int seed = (unsigned int)strtoul(argv[5], NULL, 10);

    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return EXIT_FAILURE;
    }
    if (window_size < 1 || window_size > 65534) {
        fprintf(stderr, "Invalid window size\n");
        return EXIT_FAILURE;
    }
    if (timeout_ms <= 0) {
        fprintf(stderr, "Invalid timeout\n");
        return EXIT_FAILURE;
    }
    if (loss_rate < 0.0 || loss_rate >= 1.0) {
        fprintf(stderr, "Invalid loss rate\n");
        return EXIT_FAILURE;
    }


    // create the UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // loss simulation initialization
    if (lossy_init(loss_rate, seed) < 0) {
        perror("lossy_init");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // create server address 
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(port); // Listen on UDP port

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

    // validate the request packet before using its filename
    if (request_pkt.type != REQUEST || request_pkt.count == 0 ||
        request_pkt.seq != 0) {
        fprintf(stderr, "Invalid REQUEST packet\n");
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

    // initialize the sender window and transfer statistics
    uint16_t base = 1;
    uint16_t next_seq = 1;

    packet_t packets[65535];
    int packet_count = 0;
    int retransmissions = 0;
    int timeouts = 0;
    int original_bytes = 0;
    long long timer_deadline = 0;

    unsigned char send_buf[HEADER_SIZE + MAX_DATA];

    // file loading loop: start reading file line by line
    char data_buf[MAX_DATA + 1];
    while (fgets(data_buf, sizeof(data_buf), fp) != NULL) {
        int data_len = strlen(data_buf);
        original_bytes += data_len;

        // build data packet 
        packet_t data_pkt;
        data_pkt.type = DATA;
        data_pkt.seq = next_seq;
        data_pkt.count = data_len;

        memcpy(data_pkt.data, data_buf, data_pkt.count);
        packets[next_seq] = data_pkt;

        packet_count++;
        next_seq++;
    }

    // reset base and next_seq for packet sending loop
    base = 1;
    next_seq = 1;

    // sending loop
    while (base <= packet_count) {
        // send all the packets we can in the window then recv ACKS
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

            // send packet
            int sending_error = lossy_sendto(
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

            printf("DATA %u sent with %u data bytes\n", packets[next_seq].seq, packets[next_seq].count);

            if (base == next_seq) {
                // start the timer when the first packet enters an empty window
                long long now;
                if (monotonic_ms(&now) < 0) {
                    perror("clock_gettime");
                    fclose(fp);
                    close(sockfd);
                    return EXIT_FAILURE;
                }
                timer_deadline = now + timeout_ms;
            }
            next_seq++;
        }

        long long now;
        if (monotonic_ms(&now) < 0) {
            perror("clock_gettime");
            fclose(fp);
            close(sockfd);
            return EXIT_FAILURE;
        }
        // wait only for the remaining time on the base packet's timer
        long long remaining_ms = timer_deadline - now;
        if (remaining_ms < 0) remaining_ms = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = (time_t)(remaining_ms / 1000);
        timeout.tv_usec = (suseconds_t)((remaining_ms % 1000) * 1000);

        int ready = select(
            sockfd + 1,
            &readfds,
            NULL,
            NULL,
            &timeout
        );

        if (ready == 0) {
            timeouts++;
            printf("Timeout for packet %u\n", base);

            // resend all the packets in the window that timed out
            for (uint16_t seq = base; seq < next_seq; seq++) {
                int packet_size = serialize_packet(&packets[seq], send_buf);

                if (packet_size < 0) {
                    fprintf(stderr, "Packet serialization failed\n");
                    fclose(fp);
                    close(sockfd);
                    exit(EXIT_FAILURE);
                }

                int sending_error = lossy_sendto(
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

                printf("DATA %u retransmitted with %u data bytes\n", packets[seq].seq, packets[seq].count);
                retransmissions++;
            }

            if (monotonic_ms(&now) < 0) {
                perror("clock_gettime");
                fclose(fp);
                close(sockfd);
                return EXIT_FAILURE;
            }
            timer_deadline = now + timeout_ms;
            continue;
        } else if (ready > 0) {
            // recv an ack
            unsigned char ack_buffer[HEADER_SIZE + MAX_DATA];
            struct sockaddr_in source_addr;
            socklen_t source_len = sizeof(source_addr);
            int ack_bytes = recvfrom(
                sockfd,
                ack_buffer,
                sizeof(ack_buffer),
                0,
                (struct sockaddr *)&source_addr,
                &source_len
            );

            if (ack_bytes < 0) {
                if (errno == EINTR) continue;
                perror("recvfrom");
                fclose(fp);
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            // ignore ACKs from any address other than the requesting client
            if (!same_peer(&source_addr, &client_addr)) continue;

            packet_t ack_pkt;
            if (deserialize_packet(&ack_pkt, ack_buffer, ack_bytes) < 0) {
                continue;
            }

            if (ack_pkt.type != ACK || ack_pkt.count != 0) continue;

            // advance the window only for an ACK of a sent packet
            if (ack_pkt.seq >= base && ack_pkt.seq < next_seq) {
                printf("ACK %u received\n", ack_pkt.seq);
                base = ack_pkt.seq + 1;
                if (base < next_seq) {
                    if (monotonic_ms(&now) < 0) {
                        perror("clock_gettime");
                        fclose(fp);
                        close(sockfd);
                        return EXIT_FAILURE;
                    }
                    timer_deadline = now + timeout_ms;
                }
            }
        } else if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    // send EOT packet
    packet_t eot_pkt;
    eot_pkt.type = EOT;
    eot_pkt.count = 0;
    eot_pkt.seq = packet_count + 1;

    int eot_size = serialize_packet(&eot_pkt, send_buf);

    if (eot_size < 0) {
        fprintf(stderr, "EOT serialization failed\n");
        fclose(fp);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (lossy_sendto(
        sockfd,
        send_buf,
        eot_size,
        0,
        (struct sockaddr *)&client_addr,
        client_len) < 0)
    {
        perror("lossy_sendto");
        fclose(fp);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("EOT %u sent\n", eot_pkt.seq);

    int eot_acked = 0;
    // start a separate timer for reliable EOT delivery
    long long eot_deadline;
    if (monotonic_ms(&eot_deadline) < 0) {
        perror("clock_gettime");
        fclose(fp);
        close(sockfd);
        return EXIT_FAILURE;
    }
    eot_deadline += timeout_ms;

    while (!eot_acked) {
        long long now;
        if (monotonic_ms(&now) < 0) {
            perror("clock_gettime");
            fclose(fp);
            close(sockfd);
            return EXIT_FAILURE;
        }
        // wait only for the remaining time on the EOT timer
        long long remaining_ms = eot_deadline - now;
        if (remaining_ms < 0) remaining_ms = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = (time_t)(remaining_ms / 1000);
        timeout.tv_usec = (suseconds_t)((remaining_ms % 1000) * 1000);

        int ready = select(
            sockfd + 1,
            &readfds,
            NULL,
            NULL,
            &timeout
        );
        if (ready == 0){
            if (lossy_sendto(
                    sockfd,
                    send_buf,
                    eot_size,
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len) < 0)
            {
                perror("lossy_sendto");
                fclose(fp);
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            printf("EOT %u retransmitted\n", eot_pkt.seq);
            timeouts++;

            if (monotonic_ms(&now) < 0) {
                perror("clock_gettime");
                fclose(fp);
                close(sockfd);
                return EXIT_FAILURE;
            }
            eot_deadline = now + timeout_ms;
            continue;
        } else if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // wait for the one EOT ACK
        unsigned char eot_ack_buffer[HEADER_SIZE + MAX_DATA];
        struct sockaddr_in source_addr;
        socklen_t source_len = sizeof(source_addr);

        int eot_ack_bytes = recvfrom(
            sockfd,
            eot_ack_buffer,
            sizeof(eot_ack_buffer),
            0,
            (struct sockaddr *)&source_addr,
            &source_len
        );

        if (eot_ack_bytes < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        // ignore EOT ACKs from any other source
        if (!same_peer(&source_addr, &client_addr)) continue;

        packet_t eot_ack;

        if (deserialize_packet(&eot_ack, eot_ack_buffer, eot_ack_bytes) < 0) {
            continue;
        }

        if (eot_ack.type != ACK || eot_ack.count != 0 ||
            eot_ack.seq != eot_pkt.seq) continue;

        printf("EOT %u acknowledged\n", eot_pkt.seq);
        eot_acked = 1;
    }

    printf("Number of original data packets: %d\n", packet_count);
    printf("Number of data-packet retransmissions: %d\n", retransmissions);
    printf("Total number of original data bytes: %d\n", original_bytes);
    printf("Number of timeouts: %d\n", timeouts);

    fclose(fp);
    close(sockfd);

    return 0;
}