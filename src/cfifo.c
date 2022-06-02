#include "cfifo.h"

#include "zmalloc.h"

struct ckfifo {
    unsigned char      *buffer;
    uint32_t            size;
    uint32_t            in;
    uint32_t            out;
    lwp_mutex_t         mutex;   /* relate to memory copy, it's not adapt to spinlock */
};

static uint32_t _ckfifo_len(const struct ckfifo *ring)
{
    return (ring->in - ring->out);
}

static uint32_t _ckfifo_get(struct ckfifo *ring, unsigned char *buffer, uint32_t size)
{
    uint32_t len, n;

    n  = min(size, ring->in - ring->out);
    len = min(n, ring->size - (ring->out & (ring->size - 1)));
    memcpy(buffer, ring->buffer + (ring->out & (ring->size - 1)), len);
    memcpy(buffer + len, ring->buffer, n - len);
    ring->out += n;
    return n;
}

static uint32_t _ckfifo_put(struct ckfifo *ring, const unsigned char *buffer, uint32_t size)
{
    uint32_t len, n;

    n = min(size, ring->size - ring->in + ring->out);
    len  = min(n, ring->size - (ring->in & (ring->size - 1)));
    memcpy(ring->buffer + (ring->in & (ring->size - 1)), buffer, len);
    memcpy(ring->buffer, buffer + len, n - len);
    ring->in += n;
    return n;
}

PORTABLEIMPL(struct ckfifo*) ckfifo_init(void *buffer, uint32_t size)
{
    struct ckfifo *ring;

    if (!is_powerof_2(size) || unlikely(!buffer)) {
        return NULL;
    }

    ring = (struct ckfifo *)ztrymalloc(sizeof(struct ckfifo));
    if ( unlikely(!ring) ) {
        return NULL;
    }

    memset(ring, 0, sizeof(struct ckfifo));
    ring->buffer = buffer;
    ring->size = size;
    ring->in = 0;
    ring->out = 0;
    lwp_mutex_init(&ring->mutex, YES);
    return ring;
}

PORTABLEIMPL(void) ckfifo_uninit(struct ckfifo *ring)
{
    if (unlikely(!ring)) {
        return;
    }

    zfree(ring);
}

PORTABLEIMPL(uint32_t) ckfifo_len(struct ckfifo *ring)
{
    uint32_t len;

    if (unlikely(!ring)) {
        return 0;
    }

    lwp_mutex_lock(&ring->mutex);
    len = _ckfifo_len(ring);
    lwp_mutex_unlock(&ring->mutex);
    return len;
}

PORTABLEIMPL(uint32_t) ckfifo_get(struct ckfifo *ring, void *buffer, uint32_t size)
{
    uint32_t n;

    if (unlikely(!ring || !buffer)) {
        return 0;
    }

    lwp_mutex_lock(&ring->mutex);
    n = _ckfifo_get(ring, buffer, size);
    if (ring->in == ring->out) {
        ring->in = ring->out = 0;
    }
    lwp_mutex_unlock(&ring->mutex);
    return n;
}

PORTABLEIMPL(uint32_t) ckfifo_put(struct ckfifo *ring, const void *buffer, uint32_t size)
{
    uint32_t n;

    if (unlikely(!ring || !buffer)) {
        return 0;
    }

    lwp_mutex_lock(&ring->mutex);
    n = _ckfifo_put(ring, buffer, size);
    lwp_mutex_unlock(&ring->mutex);
    return n;
}
