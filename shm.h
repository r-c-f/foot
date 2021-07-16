#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include <pixman.h>
#include <wayland-client.h>

struct damage;

struct buffer {
    int width;
    int height;
    int stride;

    void *data;

    struct wl_buffer *wl_buf;
    pixman_image_t **pix;
    size_t pix_instances;

    unsigned age;

    struct damage *scroll_damage;
    size_t scroll_damage_count;
    pixman_region32_t dirty;
};

struct buffer_description {
    int width;
    int height;
    unsigned long cookie;
};

void shm_fini(void);
void shm_set_max_pool_size(off_t max_pool_size);

/*
 * Returns a single buffer.
 *
 * May returned a cached buffer. If so, the buffer’s age indicates how
 * many shm_get_buffer() calls have been made for the same
 * width/height/cookie while the buffer was still busy.
 *
 * A newly allocated buffer has an age of 1234.
 */
struct buffer *shm_get_buffer(
    struct wl_shm *shm, int width, int height, unsigned long cookie,
    bool scrollable, size_t pix_instances);

/*
 * Returns many buffers, described by ‘info’, all sharing the same SHM
 * buffer pool.
 *
 * Never returns cached buffers. However, the newly created buffers
 * are all inserted into the regular buffer cache, and are treated
 * just like buffers created by shm_get_buffer().
 *
 * This function is useful when allocating many small buffers, with
 * (roughly) the same life time.
 *
 * Buffers are tagged for immediate purging, and will be destroyed as
 * soon as the compositor releases them.
 */
void shm_get_many(
    struct wl_shm *shm, size_t count,
    struct buffer_description info[static count],
    struct buffer *bufs[static count], size_t pix_instances);

bool shm_can_scroll(const struct buffer *buf);
bool shm_scroll(struct wl_shm *shm, struct buffer *buf, int rows,
                int top_margin, int top_keep_rows,
                int bottom_margin, int bottom_keep_rows);

void shm_addref(struct buffer *buf);
void shm_unref(struct buffer *buf);
void shm_purge(struct wl_shm *shm, unsigned long cookie);

struct terminal;
static inline unsigned long shm_cookie_grid(const struct terminal *term) { return (unsigned long)((uintptr_t)term + 0); }
static inline unsigned long shm_cookie_search(const struct terminal *term) { return (unsigned long)((uintptr_t)term + 1); }
static inline unsigned long shm_cookie_scrollback_indicator(const struct terminal *term) { return (unsigned long)(uintptr_t)term + 2; }
static inline unsigned long shm_cookie_render_timer(const struct terminal *term) { return (unsigned long)(uintptr_t)term + 3; }
static inline unsigned long shm_cookie_csd(const struct terminal *term, int n) { return (unsigned long)((uintptr_t)term + 4 + (n)); }

struct url;
static inline unsigned long shm_cookie_url(const struct url *url) { return (unsigned long)(uintptr_t)url; }
