#ifndef CONFIG_H
#define CONFIG_H

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include "Winjump.h"

extern const char *const CONFIG_GLOBAL_STRING_INVALID_HOTKEY;

typedef struct WinjumpState WinjumpState;

HFONT Config_ReadFromDisk(WinjumpState *state);
void  Config_WriteToDisk (WinjumpState *state);

#endif
