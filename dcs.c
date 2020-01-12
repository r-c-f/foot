#include "dcs.h"

#define LOG_MODULE "dcs"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "vt.h"

static void
bsu(struct terminal *term)
{
    LOG_WARN("unimplemented: BSU - Begin Synchronized Update (params: %.*s)",
             (int)term->vt.dcs.idx, term->vt.dcs.data);
    abort();
}

static void
esu(struct terminal *term)
{
    LOG_WARN("unimplemented: ESU - Begin Synchronized Update (params: %.*s)",
             (int)term->vt.dcs.idx, term->vt.dcs.data);
    abort();
}

void
dcs_hook(struct terminal *term, uint8_t final)
{
    LOG_DBG("hook: %c (intermediate(s): %.2s, param=%d)", final, term->vt.private,
            vt_param_get(term, 0, 0));

    assert(term->vt.dcs.data == NULL);
    assert(term->vt.dcs.size == 0);
    assert(term->vt.dcs.unhook_handler == NULL);

    switch (term->vt.private[0]) {
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
    assert(new_size > 0);

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
    if (!ensure_size(term, term->vt.dcs.idx + 1))
        return;
    term->vt.dcs.data[term->vt.dcs.idx++] = c;
}

void
dcs_unhook(struct terminal *term)
{
    assert(term->vt.dcs.unhook_handler != NULL);
    if (term->vt.dcs.unhook_handler != NULL)
        term->vt.dcs.unhook_handler(term);

    term->vt.dcs.unhook_handler = NULL;

    free(term->vt.dcs.data);
    term->vt.dcs.data = NULL;
    term->vt.dcs.size = 0;
    term->vt.dcs.idx = 0;
}
