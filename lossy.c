/*
 * lossy.c
 *
 * Instructor-supplied UDP loss simulator for Programming Project 2.
 * This file uses a small deterministic pseudorandom generator so that a given
 * loss rate and seed produce the same loss pattern on every course machine.
 * The module is intended for single-threaded programs. It must not be modified.
 */

#include "lossy.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>

static double configured_loss_rate = 0.0;
static uint32_t generator_state = UINT32_C(0x6d2b79f5);
static int initialized = 0;

/* Return the next value from a deterministic xorshift32 generator. */
static uint32_t next_random_u32(void)
{
    uint32_t value = generator_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    generator_state = value;
    return value;
}

/* Return a deterministic value in the half-open interval [0.0, 1.0). */
static double next_unit_double(void)
{
    return (double)next_random_u32() / 4294967296.0;
}

int lossy_init(double loss_rate, uint32_t seed)
{
    if (!isfinite(loss_rate) || loss_rate < 0.0 || loss_rate >= 1.0) {
        errno = EINVAL;
        return -1;
    }

    configured_loss_rate = loss_rate;
    generator_state = (seed == 0) ? UINT32_C(0x6d2b79f5) : seed;
    initialized = 1;
    return 0;
}

ssize_t lossy_sendto(
    int socket_fd,
    const void *buffer,
    size_t length,
    int flags,
    const struct sockaddr *destination,
    socklen_t address_length)
{
    /* A program that omits lossy_init() behaves as though the loss rate is 0. */
    if (initialized && next_unit_double() < configured_loss_rate) {
        return (ssize_t)length;
    }

    return sendto(
        socket_fd,
        buffer,
        length,
        flags,
        destination,
        address_length);
}