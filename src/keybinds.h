#pragma once
#ifndef MANGOHUD_KEYBINDS_H
#define MANGOHUD_KEYBINDS_H

#include <windows.h>
#include <vector>

#ifndef KeySym
typedef unsigned long KeySym;
#endif

static inline bool keys_are_pressed(const std::vector<KeySym>& keys) {
    size_t pressed = 0;

    for (KeySym ks : keys) {
        if (GetAsyncKeyState(ks) & 0x8000)
            pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

    return false;
}

#endif //MANGOHUD_KEYBINDS_H
