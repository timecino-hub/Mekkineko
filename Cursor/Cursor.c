#include "Cursor.h"
#include "LCD.h"

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

void Cursor_Init(Cursor_t* cursor)
{
    cursor->pos.x = SCREEN_WIDTH / 2;
    cursor->pos.y = SCREEN_HEIGHT / 2;
    cursor->size  = 18;
    cursor->speed = 5;
}

void Cursor_Update(Cursor_t* cursor, UserInput input)
{
    if (input.direction == CENTRE) return;

    // Map direction to movement
    float dx = 0, dy = 0;
    float mag = input.magnitude;
    if (mag < 0.1f) return;

    switch (input.direction) {
        case N:  dx = 0;        dy = -1.0f;  break;
        case NE: dx = 0.707f;   dy = -0.707f; break;
        case E:  dx = 1.0f;     dy = 0;       break;
        case SE: dx = 0.707f;   dy = 0.707f;  break;
        case S:  dx = 0;        dy = 1.0f;    break;
        case SW: dx = -0.707f;  dy = 0.707f;  break;
        case W:  dx = -1.0f;    dy = 0;       break;
        case NW: dx = -0.707f;  dy = -0.707f; break;
        default: return;
    }

    int16_t move = (int16_t)(cursor->speed * mag);
    cursor->pos.x -= (int16_t)(dx * move);
    cursor->pos.y += (int16_t)(dy * move);

    // Clamp to screen
    if (cursor->pos.x < 0) cursor->pos.x = 0;
    if (cursor->pos.y < 0) cursor->pos.y = 0;
    if (cursor->pos.x > SCREEN_WIDTH - cursor->size)  cursor->pos.x = SCREEN_WIDTH - cursor->size;
    if (cursor->pos.y > SCREEN_HEIGHT - cursor->size) cursor->pos.y = SCREEN_HEIGHT - cursor->size;
}

// BLACK hand on transparent background (value 0=BLACK, 255=transparent)
static const uint8_t hand_sprite[324] = {
    255,255,255,255,255,255,255,0,0,0,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,0,255,255,255,0,255,255,255,255,255,255,255,
    255,0,0,0,0,255,0,255,255,255,0,0,0,0,255,255,255,255,
    0,0,255,255,0,0,0,255,255,255,0,0,255,255,0,255,255,255,
    0,255,255,255,255,0,255,255,255,255,0,255,255,255,0,255,255,255,
    0,255,255,255,255,0,255,255,255,255,0,255,255,255,0,255,255,255,
    0,0,255,255,255,0,255,255,255,0,0,255,255,255,0,255,255,255,
    255,0,255,255,255,0,255,255,255,0,255,255,255,0,255,255,255,255,
    255,0,255,255,255,255,255,255,255,255,255,255,0,255,255,255,255,255,
    255,255,0,255,255,255,255,255,255,255,255,255,0,255,0,0,0,255,
    255,0,0,255,255,255,255,0,255,255,255,0,0,0,255,255,255,0,
    255,0,255,255,0,255,255,0,255,255,0,255,255,255,255,255,255,0,
    255,0,255,255,0,255,255,0,255,255,0,255,255,255,255,255,255,0,
    255,0,255,255,255,0,255,0,255,0,255,255,255,255,255,0,0,255,
    255,255,0,255,255,0,255,255,255,0,255,255,255,255,0,255,255,255,
    255,255,0,255,255,255,255,255,255,255,255,255,0,0,255,255,255,255,
    255,255,255,0,255,255,255,255,255,255,255,0,255,255,255,255,255,255,
    255,255,255,0,0,0,0,0,0,0,0,0,255,255,255,255,255,255
};

void Cursor_Draw(Cursor_t* cursor)
{
    // Black outline (draw 4 offsets in black)
    LCD_Draw_Sprite_Colour(cursor->pos.x-1, cursor->pos.y,   18, 18, hand_sprite, 15);
    LCD_Draw_Sprite_Colour(cursor->pos.x+1, cursor->pos.y,   18, 18, hand_sprite, 15);
    LCD_Draw_Sprite_Colour(cursor->pos.x,   cursor->pos.y-1, 18, 18, hand_sprite, 15);
    LCD_Draw_Sprite_Colour(cursor->pos.x,   cursor->pos.y+1, 18, 18, hand_sprite, 15);
    // White fill (center)
    LCD_Draw_Sprite_Colour(cursor->pos.x,   cursor->pos.y,   18, 18, hand_sprite, 1);
}

AABB Cursor_GetAABB(Cursor_t* cursor)
{
    AABB box;
    box.x      = cursor->pos.x;
    box.y      = cursor->pos.y;
    box.width  = cursor->size;
    box.height = cursor->size;
    return box;
}
