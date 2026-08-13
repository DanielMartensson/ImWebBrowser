/* ImWebBrowser - SDL scancode to Linux evdev keycode mapping. */

#include "input/keymap.h"

#include <array>

#include <linux/input-event-codes.h>

namespace imwb {

namespace {

/* Indexed by SDL_Scancode; value is the Linux evdev key code. Built through
 * an initializer function: GCC 13 has no non-trivial designated array
 * initializers, so each slot is assigned explicitly. */
constexpr std::size_t kScancodeCount = 512;

std::array<uint32_t, kScancodeCount> build_keymap()
{
    std::array<uint32_t, kScancodeCount> map{};

    map[SDL_SCANCODE_A] = KEY_A;          /* 30 */
    map[SDL_SCANCODE_B] = KEY_B;          /* 48 */
    map[SDL_SCANCODE_C] = KEY_C;          /* 46 */
    map[SDL_SCANCODE_D] = KEY_D;          /* 32 */
    map[SDL_SCANCODE_E] = KEY_E;          /* 18 */
    map[SDL_SCANCODE_F] = KEY_F;          /* 33 */
    map[SDL_SCANCODE_G] = KEY_G;          /* 34 */
    map[SDL_SCANCODE_H] = KEY_H;          /* 35 */
    map[SDL_SCANCODE_I] = KEY_I;          /* 23 */
    map[SDL_SCANCODE_J] = KEY_J;          /* 36 */
    map[SDL_SCANCODE_K] = KEY_K;          /* 37 */
    map[SDL_SCANCODE_L] = KEY_L;          /* 38 */
    map[SDL_SCANCODE_M] = KEY_M;          /* 50 */
    map[SDL_SCANCODE_N] = KEY_N;          /* 49 */
    map[SDL_SCANCODE_O] = KEY_O;          /* 24 */
    map[SDL_SCANCODE_P] = KEY_P;          /* 25 */
    map[SDL_SCANCODE_Q] = KEY_Q;          /* 16 */
    map[SDL_SCANCODE_R] = KEY_R;          /* 19 */
    map[SDL_SCANCODE_S] = KEY_S;          /* 31 */
    map[SDL_SCANCODE_T] = KEY_T;          /* 20 */
    map[SDL_SCANCODE_U] = KEY_U;          /* 22 */
    map[SDL_SCANCODE_V] = KEY_V;          /* 47 */
    map[SDL_SCANCODE_W] = KEY_W;          /* 17 */
    map[SDL_SCANCODE_X] = KEY_X;          /* 45 */
    map[SDL_SCANCODE_Y] = KEY_Y;          /* 21 */
    map[SDL_SCANCODE_Z] = KEY_Z;          /* 44 */

    map[SDL_SCANCODE_1] = KEY_1;
    map[SDL_SCANCODE_2] = KEY_2;
    map[SDL_SCANCODE_3] = KEY_3;
    map[SDL_SCANCODE_4] = KEY_4;
    map[SDL_SCANCODE_5] = KEY_5;
    map[SDL_SCANCODE_6] = KEY_6;
    map[SDL_SCANCODE_7] = KEY_7;
    map[SDL_SCANCODE_8] = KEY_8;
    map[SDL_SCANCODE_9] = KEY_9;
    map[SDL_SCANCODE_0] = KEY_0;

    map[SDL_SCANCODE_RETURN] = KEY_ENTER;        /* 28 */
    map[SDL_SCANCODE_ESCAPE] = KEY_ESC;          /* 1  */
    map[SDL_SCANCODE_BACKSPACE] = KEY_BACKSPACE; /* 14 */
    map[SDL_SCANCODE_TAB] = KEY_TAB;             /* 15 */
    map[SDL_SCANCODE_SPACE] = KEY_SPACE;         /* 57 */
    map[SDL_SCANCODE_MINUS] = KEY_MINUS;         /* 12 */
    map[SDL_SCANCODE_EQUALS] = KEY_EQUAL;        /* 13 */
    map[SDL_SCANCODE_LEFTBRACKET] = KEY_LEFTBRACE;   /* 26 */
    map[SDL_SCANCODE_RIGHTBRACKET] = KEY_RIGHTBRACE; /* 27 */
    map[SDL_SCANCODE_BACKSLASH] = KEY_BACKSLASH;     /* 43 */
    map[SDL_SCANCODE_SEMICOLON] = KEY_SEMICOLON;     /* 39 */
    map[SDL_SCANCODE_APOSTROPHE] = KEY_APOSTROPHE;   /* 40 */
    map[SDL_SCANCODE_GRAVE] = KEY_GRAVE;             /* 41 */
    map[SDL_SCANCODE_COMMA] = KEY_COMMA;             /* 51 */
    map[SDL_SCANCODE_PERIOD] = KEY_DOT;              /* 52 */
    map[SDL_SCANCODE_SLASH] = KEY_SLASH;             /* 53 */
    map[SDL_SCANCODE_CAPSLOCK] = KEY_CAPSLOCK;       /* 58 */

    map[SDL_SCANCODE_F1] = KEY_F1;
    map[SDL_SCANCODE_F2] = KEY_F2;
    map[SDL_SCANCODE_F3] = KEY_F3;
    map[SDL_SCANCODE_F4] = KEY_F4;
    map[SDL_SCANCODE_F5] = KEY_F5;
    map[SDL_SCANCODE_F6] = KEY_F6;
    map[SDL_SCANCODE_F7] = KEY_F7;
    map[SDL_SCANCODE_F8] = KEY_F8;
    map[SDL_SCANCODE_F9] = KEY_F9;
    map[SDL_SCANCODE_F10] = KEY_F10;
    map[SDL_SCANCODE_F11] = KEY_F11;               /* 87 */
    map[SDL_SCANCODE_F12] = KEY_F12;               /* 88 */

    map[SDL_SCANCODE_PRINTSCREEN] = KEY_SYSRQ;     /* 99 */
    map[SDL_SCANCODE_SCROLLLOCK] = KEY_SCROLLLOCK; /* 70 */
    map[SDL_SCANCODE_PAUSE] = KEY_PAUSE;           /* 119 */
    map[SDL_SCANCODE_INSERT] = KEY_INSERT;         /* 110 */
    map[SDL_SCANCODE_HOME] = KEY_HOME;             /* 102 */
    map[SDL_SCANCODE_PAGEUP] = KEY_PAGEUP;         /* 104 */
    map[SDL_SCANCODE_DELETE] = KEY_DELETE;         /* 111 */
    map[SDL_SCANCODE_END] = KEY_END;               /* 107 */
    map[SDL_SCANCODE_PAGEDOWN] = KEY_PAGEDOWN;     /* 109 */
    map[SDL_SCANCODE_RIGHT] = KEY_RIGHT;           /* 106 */
    map[SDL_SCANCODE_LEFT] = KEY_LEFT;             /* 105 */
    map[SDL_SCANCODE_DOWN] = KEY_DOWN;             /* 108 */
    map[SDL_SCANCODE_UP] = KEY_UP;                 /* 103 */

    map[SDL_SCANCODE_NUMLOCKCLEAR] = KEY_NUMLOCK;  /* 69 */
    map[SDL_SCANCODE_KP_DIVIDE] = KEY_KPSLASH;     /* 98 */
    map[SDL_SCANCODE_KP_MULTIPLY] = KEY_KPASTERISK;/* 55 */
    map[SDL_SCANCODE_KP_MINUS] = KEY_KPMINUS;      /* 74 */
    map[SDL_SCANCODE_KP_PLUS] = KEY_KPPLUS;        /* 78 */
    map[SDL_SCANCODE_KP_ENTER] = KEY_KPENTER;      /* 96 */
    map[SDL_SCANCODE_KP_1] = KEY_KP1;
    map[SDL_SCANCODE_KP_2] = KEY_KP2;
    map[SDL_SCANCODE_KP_3] = KEY_KP3;
    map[SDL_SCANCODE_KP_4] = KEY_KP4;
    map[SDL_SCANCODE_KP_5] = KEY_KP5;
    map[SDL_SCANCODE_KP_6] = KEY_KP6;
    map[SDL_SCANCODE_KP_7] = KEY_KP7;
    map[SDL_SCANCODE_KP_8] = KEY_KP8;
    map[SDL_SCANCODE_KP_9] = KEY_KP9;
    map[SDL_SCANCODE_KP_0] = KEY_KP0;
    map[SDL_SCANCODE_KP_PERIOD] = KEY_KPDOT;       /* 83 */

    map[SDL_SCANCODE_LCTRL] = KEY_LEFTCTRL;        /* 29 */
    map[SDL_SCANCODE_LSHIFT] = KEY_LEFTSHIFT;      /* 42 */
    map[SDL_SCANCODE_LALT] = KEY_LEFTALT;          /* 56 */
    map[SDL_SCANCODE_LGUI] = KEY_LEFTMETA;         /* 125 */
    map[SDL_SCANCODE_RCTRL] = KEY_RIGHTCTRL;       /* 97 */
    map[SDL_SCANCODE_RSHIFT] = KEY_RIGHTSHIFT;     /* 54 */
    map[SDL_SCANCODE_RALT] = KEY_RIGHTALT;         /* 100 */
    map[SDL_SCANCODE_RGUI] = KEY_RIGHTMETA;        /* 126 */

    return map;
}

const std::array<uint32_t, kScancodeCount> kScancodeToEvdev = build_keymap();

} /* namespace */

uint32_t keymap_evdev_from_scancode(SDL_Scancode scancode)
{
    if (scancode < 0 || scancode >= static_cast<int>(kScancodeToEvdev.size()))
        return 0;
    return kScancodeToEvdev[static_cast<std::size_t>(scancode)];
}

} /* namespace imwb */
