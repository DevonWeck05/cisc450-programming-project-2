/*
 * lossy.h
 *
 * Instructor-supplied UDP loss simulator for Programming Project 2.
 * The module is intended for single-threaded programs. It must not be modified.
 */

#ifndef LOSSY_H
#define LOSSY_H

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdint.h>
/*
 * Initialize the loss simulator.
 *
 * loss_rate must be in the range [0.0, 1.0). The seed controls the
 * deterministic pseudorandom sequence. A seed of 0 is permitted and is
 * internally replaced by a fixed nonzero value.
 *
 * Returns 0 on success. Returns -1 and sets errno to EINVAL if loss_rate is
 * outside the permitted range.
 */
int lossy_init(double loss_rate, uint32_t seed);

/*
 * Behaves like sendto(), except that the datagram is independently discarded
 * with the probability configured by lossy_init().
 *
 * When a datagram is simulated as lost, it is not passed to the operating
 * system, but this function returns length. Thus, the calling protocol cannot
 * directly determine whether that particular transmission was lost.
 *
 * If the datagram is not simulated as lost, the return value and errno are
 * those of sendto().
 */
ssize_t lossy_sendto(
    int socket_fd,
    const void *buffer,
    size_t length,
    int flags,
    const struct sockaddr *destination,
    socklen_t address_length);

#endif /* LOSSY_H */