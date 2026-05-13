#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>
#include "Utils.h"
#include "Joystick.h"

typedef struct {
    Position2D pos;
    int16_t    size;
    uint8_t    speed;
} Cursor_t;

void Cursor_Init(Cursor_t* cursor);
void Cursor_Update(Cursor_t* cursor, UserInput input);
void Cursor_Draw(Cursor_t* cursor);
AABB Cursor_GetAABB(Cursor_t* cursor);

#endif
