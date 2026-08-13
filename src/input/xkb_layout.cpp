/* ImWebBrowser - XKB keyboard layout handling. */

#include "input/xkb_layout.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <xkbcommon/xkbcommon.h>

#include "logging/log.h"

namespace imwb {

namespace {

/* Reads the system XKB RMLVO from XKB_DEFAULT_* env vars, or from
 * `setxkbmap -query` if the env vars are unset (typical under X11). */
void load_system_rmlvo(xkb_rule_names& names)
{
    /* Start with everything NULL so xkb_keymap_new_from_names reads the
     * XKB_DEFAULT_* env vars. */
    names = {};

    /* If XKB_DEFAULT_LAYOUT is already set, xkb_keymap_new_from_names will
     * pick up all the XKB_DEFAULT_* vars automatically — just return. */
    if (std::getenv("XKB_DEFAULT_LAYOUT"))
        return;

    /* Under X11 the env vars are usually unset; query setxkbmap instead. */
    FILE* pipe = popen("setxkbmap -query 2>/dev/null", "r");
    if (!pipe) {
        LOG_WARN("XKB: setxkbmap query failed; using libxkbcommon defaults");
        return;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), pipe)) {
        char* colon = std::strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char* val = colon + 1;
        while (*val == ' ' || *val == '\t')
            ++val;
        size_t len = std::strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == ' ' || val[len - 1] == '\t'))
            val[--len] = '\0';
        if (len == 0)
            continue;

        /* These are static-duration strings that persist for the process
         * lifetime; the xkb_rule_names struct just holds the pointers. */
        static char s_rules[64] = "evdev";
        static char s_model[64] = "pc105";
        static char s_layout[64] = {};
        static char s_variant[64] = {};
        static char s_options[128] = {};

        if (std::strcmp(line, "rules") == 0)
            std::snprintf(s_rules, sizeof(s_rules), "%s", val), names.rules = s_rules;
        else if (std::strcmp(line, "model") == 0)
            std::snprintf(s_model, sizeof(s_model), "%s", val), names.model = s_model;
        else if (std::strcmp(line, "layout") == 0)
            std::snprintf(s_layout, sizeof(s_layout), "%s", val), names.layout = s_layout;
        else if (std::strcmp(line, "variant") == 0)
            std::snprintf(s_variant, sizeof(s_variant), "%s", val), names.variant = s_variant;
        else if (std::strcmp(line, "options") == 0)
            std::snprintf(s_options, sizeof(s_options), "%s", val), names.options = s_options;
    }
    pclose(pipe);
}

} /* namespace */

XkbLayout::~XkbLayout()
{
    if (m_state)
        xkb_state_unref(static_cast<struct xkb_state*>(m_state));
    if (m_keymap)
        xkb_keymap_unref(static_cast<struct xkb_keymap*>(m_keymap));
    if (m_context)
        xkb_context_unref(static_cast<struct xkb_context*>(m_context));
}

bool XkbLayout::initialize()
{
    auto* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        LOG_ERROR("XKB: failed to create xkb_context");
        return false;
    }
    m_context = ctx;

    xkb_rule_names names;
    load_system_rmlvo(names);

    auto* keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        keymap = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (!keymap) {
        LOG_ERROR("XKB: failed to create xkb_keymap");
        return false;
    }
    m_keymap = keymap;

    auto* state = xkb_state_new(keymap);
    if (!state) {
        LOG_ERROR("XKB: failed to create xkb_state");
        return false;
    }
    m_state = state;

    LOG_INFO("XKB: keymap loaded (layout %s)",
             names.layout ? names.layout :
             (std::getenv("XKB_DEFAULT_LAYOUT") ? std::getenv("XKB_DEFAULT_LAYOUT") : "us"));
    return true;
}

uint32_t XkbLayout::keysym_for_keycode(uint32_t keycode, bool pressed)
{
    auto* state = static_cast<struct xkb_state*>(m_state);
    if (!state)
        return 0;

    if (pressed)
        xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
    else
        xkb_state_update_key(state, keycode, XKB_KEY_UP);

    const xkb_keysym_t* syms = nullptr;
    int count = xkb_state_key_get_syms(state, keycode, &syms);
    if (count > 0)
        return syms[0];
    return 0;
}

} /* namespace imwb */