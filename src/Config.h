#ifndef CONFIG_H
#define CONFIG_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct WinjumpState WinjumpState;

HFONT config_read_from_disk(WinjumpState *state);
void  config_write_to_disk(WinjumpState *state);

#endif
