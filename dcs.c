#include "dcs.h"

#define LOG_MODULE "dcs"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "sixel.h"
#include "vt.h"

static void
bsu(struct terminal *term)
{
    /* https://gitlab.com/gnachman/iterm2/-/wikis/synchronized-updates-spec */

    size_t n = term->vt.dcs.idx;
    if (unlikely(n > 0))
        LOG_DBG("BSU with unknown params: %.*s)", (int)n, term->vt.dcs.data);

    term_enable_app_sync_updates(term);
}

static void
esu(struct terminal *term)
{
    /* https://gitlab.com/gnachman/iterm2/-/wikis/synchronized-updates-spec */

    size_t n = term->vt.dcs.idx;
    if (unlikely(n > 0))
        LOG_DBG("ESU with unknown params: %.*s)", (int)n, term->vt.dcs.data);

    term_disable_app_sync_updates(term);
}

void
dcs_hook(struct terminal *term, uint8_t final)
{
    LOG_DBG("hook: %c (intermediate(s): %.2s, param=%d)", final,
            (const char *)&term->vt.private, vt_param_get(term, 0, 0));

    xassert(term->vt.dcs.data == NULL);
    xassert(term->vt.dcs.size == 0);
    xassert(term->vt.dcs.put_handler == NULL);
    xassert(term->vt.dcs.unhook_handler == NULL);

    switch (term->vt.private) {
    case 0:
        switch (final) {
        case 'q': {
            int p1 = vt_param_get(term, 0, 0);
            int p2 = vt_param_get(term, 1,0);
            int p3 = vt_param_get(term, 2, 0);

            sixel_init(term, p1, p2, p3);
            term->vt.dcs.put_handler = &sixel_put;
            term->vt.dcs.unhook_handler = &sixel_unhook;
            break;
        }
        }
        break;

    case '=':
        switch (final) {
        case 's':
            switch (vt_param_get(term, 0, 0)) {
            case 1: term->vt.dcs.unhook_handler = &bsu; return;
            case 2: term->vt.dcs.unhook_handler = &esu; return;
            }
            break;
        }
        break;
    }
}

static bool
ensure_size(struct terminal *term, size_t required_size)
{
    if (required_size <= term->vt.dcs.size)
        return true;

    size_t new_size = (required_size + 127) / 128 * 128;
    xassert(new_size > 0);

    uint8_t *new_data = realloc(term->vt.dcs.data, new_size);
    if (new_data == NULL) {
        LOG_ERRNO("failed to increase size of DCS buffer");
        return false;
    }

    term->vt.dcs.data = new_data;
    term->vt.dcs.size = new_size;
    return true;
}

void
dcs_put(struct terminal *term, uint8_t c)
{
    LOG_DBG("PUT: %c", c);
    if (term->vt.dcs.put_handler != NULL)
        term->vt.dcs.put_handler(term, c);
    else {
        if (!ensure_size(term, term->vt.dcs.idx + 1))
            return;
        term->vt.dcs.data[term->vt.dcs.idx++] = c;
    }
}

void
dcs_unhook(struct terminal *term)
{
    if (term->vt.dcs.unhook_handler != NULL)
        term->vt.dcs.unhook_handler(term);

    term->vt.dcs.unhook_handler = NULL;
    term->vt.dcs.put_handler = NULL;

    free(term->vt.dcs.data);
    term->vt.dcs.data = NULL;
    term->vt.dcs.size = 0;
    term->vt.dcs.idx = 0;
}
