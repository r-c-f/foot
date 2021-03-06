#include "shm.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <pixman.h>

#include <fcft/stride.h>
#include <tllist.h>

#define LOG_MODULE "shm"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "debug.h"
#include "macros.h"
#include "xmalloc.h"

#if !defined(MAP_UNINITIALIZED)
 #define MAP_UNINITIALIZED 0
#endif

#define TIME_SCROLL 0

#define FORCED_DOUBLE_BUFFERING 0

/*
 * Maximum memfd size allowed.
 *
 * On 64-bit, we could in theory use up to 2GB (wk_shm_create_pool()
 * is limited to int32_t), since we never mmap() the entire region.
 *
 * The compositor is different matter - it needs to mmap() the entire
 * range, and *keep* the mapping for as long as is has buffers
 * referencing it (thus - always). And if we open multiple terminals,
 * then the required address space multiples...
 *
 * That said, 128TB (the total amount of available user address space
 * on 64-bit) is *a lot*; we can fit 67108864 2GB memfds into
 * that. But, let's be conservative for now.
 *
 * On 32-bit the available address space is too small and SHM
 * scrolling is disabled.
 *
 * Note: this is the _default_ size. It can be overridden by calling
 * shm_set_max_pool_size();
 */
static off_t max_pool_size = 512 * 1024 * 1024;

static tll(struct buffer) buffers;

static bool can_punch_hole = false;
static bool can_punch_hole_initialized = false;

#undef MEASURE_SHM_ALLOCS
#if defined(MEASURE_SHM_ALLOCS)
static size_t max_alloced = 0;
#endif

void
shm_set_max_pool_size(off_t _max_pool_size)
{
    max_pool_size = _max_pool_size;
}

static void
buffer_destroy_dont_close(struct buffer *buf)
{
    if (buf->pix != NULL) {
        for (size_t i = 0; i < buf->pix_instances; i++)
            if (buf->pix[i] != NULL)
                pixman_image_unref(buf->pix[i]);
    }
    if (buf->wl_buf != NULL)
        wl_buffer_destroy(buf->wl_buf);

    free(buf->pix);
    buf->pix = NULL;
    buf->wl_buf = NULL;
    buf->mmapped = NULL;
}

static void
buffer_destroy(struct buffer *buf)
{
    buffer_destroy_dont_close(buf);
    if (buf->real_mmapped != MAP_FAILED)
        munmap(buf->real_mmapped, buf->mmap_size);
    if (buf->pool != NULL)
        wl_shm_pool_destroy(buf->pool);
    if (buf->fd >= 0)
        close(buf->fd);

    buf->real_mmapped = MAP_FAILED;
    buf->pool = NULL;
    buf->fd = -1;

    free(buf->scroll_damage);
    pixman_region32_fini(&buf->dirty);
}

void
shm_fini(void)
{
    tll_foreach(buffers, it) {
        buffer_destroy(&it->item);
        tll_remove(buffers, it);
    }

#if defined(MEASURE_SHM_ALLOCS) && MEASURE_SHM_ALLOCS
    LOG_INFO("max total allocations was: %zu MB", max_alloced / 1024 / 1024);
#endif
}

static void
buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    struct buffer *buffer = data;
    LOG_DBG("release: cookie=%lx (buf=%p)", buffer->cookie, (void *)buffer);
    xassert(buffer->wl_buf == wl_buffer);
    xassert(buffer->busy);
    buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = &buffer_release,
};

#if __SIZEOF_POINTER__ == 8
static size_t
page_size(void)
{
    static size_t size = 0;
    if (size == 0) {
        long n = sysconf(_SC_PAGE_SIZE);
        if (n <= 0) {
            LOG_ERRNO("failed to get page size");
            size = 4096;
        } else {
            size = (size_t)n;
        }
    }
    xassert(size > 0);
    return size;
}
#endif

static bool
instantiate_offset(struct wl_shm *shm, struct buffer *buf, off_t new_offset)
{
    xassert(buf->fd >= 0);
    xassert(buf->mmapped == NULL);
    xassert(buf->wl_buf == NULL);
    xassert(buf->pix == NULL);

    void *mmapped = MAP_FAILED;
    struct wl_buffer *wl_buf = NULL;
    pixman_image_t **pix = xcalloc(buf->pix_instances, sizeof(*pix));

    mmapped = (uint8_t *)buf->real_mmapped + new_offset;

    wl_buf = wl_shm_pool_create_buffer(
        buf->pool, new_offset, buf->width, buf->height, buf->stride, WL_SHM_FORMAT_ARGB8888);
    if (wl_buf == NULL) {
        LOG_ERR("failed to create SHM buffer");
        goto err;
    }

    /* One pixman image for each worker thread (do we really need multiple?) */
    for (size_t i = 0; i < buf->pix_instances; i++) {
        pix[i] = pixman_image_create_bits_no_clear(
            PIXMAN_a8r8g8b8, buf->width, buf->height, (uint32_t *)mmapped, buf->stride);
        if (pix[i] == NULL) {
            LOG_ERR("failed to create pixman image");
            goto err;
        }
    }

    buf->offset = new_offset;
    buf->mmapped = mmapped;
    buf->wl_buf = wl_buf;
    buf->pix = pix;

    wl_buffer_add_listener(wl_buf, &buffer_listener, buf);
    return true;

err:
    if (pix != NULL) {
        for (size_t i = 0; i < buf->pix_instances; i++)
            if (pix[i] != NULL)
                pixman_image_unref(pix[i]);
    }
    free(pix);
    if (wl_buf != NULL)
        wl_buffer_destroy(wl_buf);

    abort();
    return false;
}

struct buffer *
shm_get_buffer(struct wl_shm *shm, int width, int height, unsigned long cookie, bool scrollable, size_t pix_instances)
{
    /* Purge buffers marked for purging */
    tll_foreach(buffers, it) {
        if (it->item.cookie != cookie)
            continue;

        if (!it->item.purge)
            continue;

        xassert(!it->item.busy);

        LOG_DBG("cookie=%lx: purging buffer %p (width=%d, height=%d): %zu KB",
                cookie, (void *)&it->item, it->item.width, it->item.height,
                it->item.size / 1024);

        buffer_destroy(&it->item);
        tll_remove(buffers, it);
    }

    struct buffer *cached = NULL;

    tll_foreach(buffers, it) {
        if (it->item.width != width)
            continue;
        if (it->item.height != height)
            continue;
        if (it->item.cookie != cookie)
            continue;

        if (it->item.busy)
            it->item.age++;
        else
#if FORCED_DOUBLE_BUFFERING
            if (it->item.age == 0)
                it->item.age++;
            else
#endif
            {
                LOG_DBG("cookie=%lx: re-using buffer from cache (buf=%p)",
                        cookie, (void *)&it->item);
                it->item.busy = true;
                it->item.purge = false;
                pixman_region32_clear(&it->item.dirty);
                free(it->item.scroll_damage);
                it->item.scroll_damage = NULL;
                xassert(it->item.pix_instances == pix_instances);
                cached = &it->item;
            }
    }

    if (cached != NULL)
        return cached;

    /* Purge old buffers associated with this cookie */
    tll_foreach(buffers, it) {
        if (it->item.cookie != cookie)
            continue;

        if (it->item.busy)
            continue;

        if (it->item.width == width && it->item.height == height)
            continue;

        LOG_DBG("cookie=%lx: marking buffer %p for purging", cookie, (void *)&it->item);
        it->item.purge = true;
    }

    /*
     * No existing buffer available. Create a new one by:
     *
     * 1. open a memory backed "file" with memfd_create()
     * 2. mmap() the memory file, to be used by the pixman image
     * 3. create a wayland shm buffer for the same memory file
     *
     * The pixman image and the wayland buffer are now sharing memory.
     */

    int pool_fd = -1;
    const int stride = stride_for_format_and_width(PIXMAN_a8r8g8b8, width);
    const size_t size = stride * height;

    void *real_mmapped = MAP_FAILED;
    struct wl_shm_pool *pool = NULL;

    LOG_DBG("cookie=%lx: allocating new buffer: %zu KB", cookie, size / 1024);

    /* Backing memory for SHM */
#if defined(MEMFD_CREATE)
    pool_fd = memfd_create("foot-wayland-shm-buffer-pool", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#elif defined(__FreeBSD__)
    // memfd_create on FreeBSD 13 is SHM_ANON without sealing support
    pool_fd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
#else
    char name[] = "/tmp/foot-wayland-shm-buffer-pool-XXXXXX";
    pool_fd = mkostemp(name, O_CLOEXEC);
    unlink(name);
#endif
    if (pool_fd == -1) {
        LOG_ERRNO("failed to create SHM backing memory file");
        goto err;
    }

#if __SIZEOF_POINTER__ == 8
    off_t initial_offset = scrollable && max_pool_size > 0 ? (max_pool_size / 4) & ~(page_size() - 1) : 0;
    off_t memfd_size = scrollable && max_pool_size > 0 ? max_pool_size : size;
#else
    off_t initial_offset = 0;
    off_t memfd_size = size;
#endif

    LOG_DBG("memfd-size: %lu, initial offset: %lu", memfd_size, initial_offset);

    if (ftruncate(pool_fd, memfd_size) == -1) {
        LOG_ERRNO("failed to set size of SHM backing memory file");
        goto err;
    }

    if (!can_punch_hole_initialized) {
        can_punch_hole_initialized = true;
#if __SIZEOF_POINTER__ == 8 && defined(FALLOC_FL_PUNCH_HOLE)
        can_punch_hole = fallocate(
            pool_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, 1) == 0;

        if (!can_punch_hole) {
            LOG_WARN(
                "fallocate(FALLOC_FL_PUNCH_HOLE) not "
                "supported (%s): expect lower performance", strerror(errno));
        }
#else
        /* This is mostly to make sure we skip the warning issued
         * above */
        can_punch_hole = false;
#endif
    }

    if (scrollable && !can_punch_hole) {
        initial_offset = 0;
        memfd_size = size;
        scrollable = false;

        if (ftruncate(pool_fd, memfd_size) < 0) {
            LOG_ERRNO("failed to set size of SHM backing memory file");
            goto err;
        }
    }

    real_mmapped = mmap(
        NULL, memfd_size, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_UNINITIALIZED, pool_fd, 0);

    if (real_mmapped == MAP_FAILED) {
        LOG_ERRNO("failed to mmap SHM backing memory file");
        goto err;
    }

#if defined(MEMFD_CREATE)
    /* Seal file - we no longer allow any kind of resizing */
    /* TODO: wayland mmaps(PROT_WRITE), for some unknown reason, hence we cannot use F_SEAL_FUTURE_WRITE */
    if (fcntl(pool_fd, F_ADD_SEALS,
              F_SEAL_GROW | F_SEAL_SHRINK | /*F_SEAL_FUTURE_WRITE |*/ F_SEAL_SEAL) < 0)
    {
        LOG_ERRNO("failed to seal SHM backing memory file");
        /* This is not a fatal error */
    }
#endif

    pool = wl_shm_create_pool(shm, pool_fd, memfd_size);
    if (pool == NULL) {
        LOG_ERR("failed to create SHM pool");
        goto err;
    }

    /* Push to list of available buffers, but marked as 'busy' */
    tll_push_back(
        buffers,
        ((struct buffer){
            .cookie = cookie,
            .width = width,
            .height = height,
            .stride = stride,
            .busy = true,
            .size = size,
            .pix_instances = pix_instances,
            .fd = pool_fd,
            .pool = pool,
            .scrollable = scrollable,
            .real_mmapped = real_mmapped,
            .mmap_size = memfd_size,
            .offset = 0,
            .age = 1234,  /* Force a full repaint */
        }));

    struct buffer *ret = &tll_back(buffers);
    if (!instantiate_offset(shm, ret, initial_offset))
        goto err;

    pixman_region32_init(&ret->dirty);

#if defined(MEASURE_SHM_ALLOCS) && MEASURE_SHM_ALLOCS
    {
        size_t currently_alloced = 0;
        tll_foreach(buffers, it)
            currently_alloced += it->item.size;
        if (currently_alloced > max_alloced)
            max_alloced = currently_alloced;
    }
#endif

    return ret;

err:
    if (pool != NULL)
        wl_shm_pool_destroy(pool);
    if (real_mmapped != MAP_FAILED)
        munmap(real_mmapped, memfd_size);
    if (pool_fd != -1)
        close(pool_fd);

    /* We don't handle this */
    abort();
    return NULL;
}

bool
shm_can_scroll(const struct buffer *buf)
{
#if __SIZEOF_POINTER__ == 8
    return can_punch_hole && max_pool_size > 0 && buf->scrollable;
#else
    /* Not enough virtual address space in 32-bit */
    return false;
#endif
}

#if __SIZEOF_POINTER__ == 8 && defined(FALLOC_FL_PUNCH_HOLE)
static bool
wrap_buffer(struct wl_shm *shm, struct buffer *buf, off_t new_offset)
{
    /* We don't allow overlapping offsets */
    off_t UNUSED diff =
        new_offset < buf->offset ? buf->offset - new_offset : new_offset - buf->offset;
    xassert(diff > buf->size);

    memcpy((uint8_t *)buf->real_mmapped + new_offset, buf->mmapped, buf->size);

    off_t trim_ofs, trim_len;
    if (new_offset > buf->offset) {
        /* Trim everything *before* the new offset */
        trim_ofs = 0;
        trim_len = new_offset;
    } else {
        /* Trim everything *after* the new buffer location */
        trim_ofs = new_offset + buf->size;
        trim_len = buf->mmap_size - trim_ofs;
    }

    if (fallocate(
            buf->fd,
            FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
            trim_ofs, trim_len) < 0)
    {
        LOG_ERRNO("failed to trim SHM backing memory file");
        return false;
    }

    /* Re-instantiate pixman+wl_buffer+raw pointersw */
    buffer_destroy_dont_close(buf);
    return instantiate_offset(shm, buf, new_offset);
}

static bool
shm_scroll_forward(struct wl_shm *shm, struct buffer *buf, int rows,
                   int top_margin, int top_keep_rows,
                   int bottom_margin, int bottom_keep_rows)
{
    xassert(can_punch_hole);
    xassert(buf->busy);
    xassert(buf->pix);
    xassert(buf->wl_buf);
    xassert(buf->fd >= 0);

    LOG_DBG("scrolling %d rows (%d bytes)", rows, rows * buf->stride);

    const off_t diff = rows * buf->stride;
    xassert(rows > 0);
    xassert(diff < buf->size);

    if (buf->offset + diff + buf->size > max_pool_size) {
        LOG_DBG("memfd offset wrap around");
        if (!wrap_buffer(shm, buf, 0))
            goto err;
    }

    off_t new_offset = buf->offset + diff;
    xassert(new_offset > buf->offset);
    xassert(new_offset + buf->size <= max_pool_size);

#if TIME_SCROLL
    struct timeval time1;
    gettimeofday(&time1, NULL);

    struct timeval time2 = time1;
#endif

    if (top_keep_rows > 0) {
        /* Copy current 'top' region to its new location */
        memmove(
            (uint8_t *)buf->mmapped + (top_margin + rows) * buf->stride,
            (uint8_t *)buf->mmapped + (top_margin + 0) * buf->stride,
            top_keep_rows * buf->stride);

#if TIME_SCROLL
        gettimeofday(&time2, NULL);
        timersub(&time2, &time1, &tot);
        LOG_INFO("memmove (top region): %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif
    }

    /* Destroy old objects (they point to the old offset) */
    buffer_destroy_dont_close(buf);

    /* Free unused memory - everything up until the new offset */
    const off_t trim_ofs = 0;
    const off_t trim_len = new_offset;

    if (fallocate(
            buf->fd,
            FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
            trim_ofs, trim_len) < 0)
    {
        LOG_ERRNO("failed to trim SHM backing memory file");
        goto err;
    }

#if TIME_SCROLL
    struct timeval time3;
    gettimeofday(&time3, NULL);
    timersub(&time3, &time2, &tot);
    LOG_INFO("PUNCH HOLE: %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif

    /* Re-instantiate pixman+wl_buffer+raw pointersw */
    bool ret = instantiate_offset(shm, buf, new_offset);

#if TIME_SCROLL
    struct timeval time4;
    gettimeofday(&time4, NULL);
    timersub(&time4, &time3, &tot);
    LOG_INFO("instantiate offset: %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif

    if (ret && bottom_keep_rows > 0) {
        /* Copy 'bottom' region to its new location */
        memmove(
            (uint8_t *)buf->mmapped + buf->size - (bottom_margin + bottom_keep_rows) * buf->stride,
            (uint8_t *)buf->mmapped + buf->size - (bottom_margin + rows + bottom_keep_rows) * buf->stride,
            bottom_keep_rows * buf->stride);

#if TIME_SCROLL
        struct timeval time5;
        gettimeofday(&time5, NULL);

        timersub(&time5, &time4, &tot);
        LOG_INFO("memmove (bottom region): %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif
    }

    return ret;

err:
    abort();
    return false;
}

static bool
shm_scroll_reverse(struct wl_shm *shm, struct buffer *buf, int rows,
                   int top_margin, int top_keep_rows,
                   int bottom_margin, int bottom_keep_rows)
{
    xassert(rows > 0);

    const off_t diff = rows * buf->stride;
    if (diff > buf->offset) {
        LOG_DBG("memfd offset reverse wrap-around");
        if (!wrap_buffer(shm, buf, (max_pool_size - buf->size) & ~(page_size() - 1)))
            goto err;
    }

    off_t new_offset = buf->offset - diff;
    xassert(new_offset < buf->offset);
    xassert(new_offset <= max_pool_size);

#if TIME_SCROLL
    struct timeval time0;
    gettimeofday(&time0, NULL);

    struct timeval tot;
    struct timeval time1 = time0;
#endif

    if (bottom_keep_rows > 0) {
        /* Copy 'bottom' region to its new location */
        memmove(
            (uint8_t *)buf->mmapped + buf->size - (bottom_margin + rows + bottom_keep_rows) * buf->stride,
            (uint8_t *)buf->mmapped + buf->size - (bottom_margin + bottom_keep_rows) * buf->stride,
            bottom_keep_rows * buf->stride);

#if TIME_SCROLL
        gettimeofday(&time1, NULL);
        timersub(&time1, &time0, &tot);
        LOG_INFO("memmove (bottom region): %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif
    }

    /* Destroy old objects (they point to the old offset) */
    buffer_destroy_dont_close(buf);

    /* Free unused memory - everything after the relocated buffer */
    const off_t trim_ofs = new_offset + buf->size;
    const off_t trim_len = buf->mmap_size - trim_ofs;

    if (fallocate(
            buf->fd,
            FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
            trim_ofs, trim_len) < 0)
    {
        LOG_ERRNO("failed to trim SHM backing memory");
        goto err;
    }
#if TIME_SCROLL
    struct timeval time2;
    gettimeofday(&time2, NULL);
    timersub(&time2, &time1, &tot);
    LOG_INFO("fallocate: %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif

    /* Re-instantiate pixman+wl_buffer+raw pointers */
    bool ret = instantiate_offset(shm, buf, new_offset);

#if TIME_SCROLL
    struct timeval time3;
    gettimeofday(&time3, NULL);
    timersub(&time3, &time2, &tot);
    LOG_INFO("instantiate offset: %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif

    if (ret && top_keep_rows > 0) {
        /* Copy current 'top' region to its new location */
        memmove(
            (uint8_t *)buf->mmapped + (top_margin + 0) * buf->stride,
            (uint8_t *)buf->mmapped + (top_margin + rows) * buf->stride,
            top_keep_rows * buf->stride);

#if TIME_SCROLL
        struct timeval time4;
        gettimeofday(&time4, NULL);
        timersub(&time4, &time2, &tot);
        LOG_INFO("memmove (top region): %lds %ldus", tot.tv_sec, tot.tv_usec);
#endif
    }

    return ret;

err:
    abort();
    return false;
}
#endif /* FALLOC_FL_PUNCH_HOLE */

bool
shm_scroll(struct wl_shm *shm, struct buffer *buf, int rows,
           int top_margin, int top_keep_rows,
           int bottom_margin, int bottom_keep_rows)
{
#if __SIZEOF_POINTER__ == 8 && defined(FALLOC_FL_PUNCH_HOLE)
    if (!shm_can_scroll(buf))
        return false;

    xassert(rows != 0);
    return rows > 0
        ? shm_scroll_forward(shm, buf, rows, top_margin, top_keep_rows, bottom_margin, bottom_keep_rows)
        : shm_scroll_reverse(shm, buf, -rows, top_margin, top_keep_rows, bottom_margin, bottom_keep_rows);
#else
    return false;
#endif
}

void
shm_purge(struct wl_shm *shm, unsigned long cookie)
{
    LOG_DBG("cookie=%lx: purging all buffers", cookie);

    /* Purge old buffers associated with this cookie */
    tll_foreach(buffers, it) {
        if (it->item.cookie != cookie)
            continue;

        xassert(!it->item.busy);

        buffer_destroy(&it->item);
        tll_remove(buffers, it);
    }
}
