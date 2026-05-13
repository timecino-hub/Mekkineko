#ifndef GAME_2_H
#define GAME_2_H

#include "Menu.h"

/**
 * @brief Game 2 - "Etselec" Platform Jumper Game
 * 
 * A platform jumping game with 5 custom levels defined by number matrices.
 * 
 * Controls:
 * - Joystick LEFT/RIGHT: Move character horizontally
 * - Joystick UP/DOWN: Set dash/jump direction
 * - BT2: Dash
 * - BT4: Jump (hold to charge)
 * 
 * @return MenuState - Where to go next
 */

MenuState Game2_Run(void);

#endif // GAME_2_H
