#ifndef CATSPRITES_H
#define CATSPRITES_H

#include <stdint.h>

// Sprite dimensions
#define CAT_SPRITE_W 240
#define CAT_SPRITE_H 240

// Unified 16-color palette (RGB565, byte-swapped for ST7789V2)
static const uint16_t cat_palette[16] = {
    0x16F7, 0x71EE, 0x12E6, 0x03F6, 0xAAE5, 0xA0F5, 0x22E5, 0xAAB4, 0x6A7B, 0x875A, 0xC539, 0x2421, 0x0319, 0xC310, 0xA210, 0x2000
};

extern const uint8_t sprite_cat_open[57600];
extern const uint8_t sprite_cat_half[57600];
extern const uint8_t sprite_cat_closed[57600];
extern const uint8_t sprite_cat_bite[57600];
extern const uint8_t sprite_cat_belly[57600];
extern const uint8_t sprite_cat_bad[57600];
extern const uint8_t sprite_cat_good[57600];
extern const uint8_t sprite_cat_hidden[57600];
extern const uint8_t sprite_cat_intro[57600];
extern const uint8_t sprite_cg_12[57600];
extern const uint8_t sprite_cg_13[57600];
extern const uint8_t sprite_cg_14[57600];
extern const uint8_t sprite_cg_15[57600];
extern const uint8_t sprite_cg_16[57600];
extern const uint8_t sprite_cg_17[57600];

#endif