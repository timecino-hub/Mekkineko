#ifndef MENU_H
#define MENU_H

#include <stdint.h>

typedef enum {
    MENU_STATE_HOME = 0,
    MENU_STATE_GAME_1,
    MENU_STATE_GAME_2,
    MENU_STATE_GAME_3,
} MenuState;

typedef struct {
    uint8_t selected_option;
} MenuSystem;

void Menu_Init(MenuSystem* menu);
MenuState Menu_Run(MenuSystem* menu);

#endif
