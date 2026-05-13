#include "Game_2.h"
#include "Levels.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Joystick.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern Buzzer_cfg_t buzzer_cfg;

// LCD API compatibility (our lib uses framebuffer, game uses direct-write)
#define LCD_Clear(cfg, c)      LCD_Fill_Buffer(0)
#define LCD_Fill_Rect(cfg, x, y, w, h, c) LCD_Draw_Rect(x, y, w, h, c, 1)
#define LCD_Print_String(cfg, x, y, str, c, bg) LCD_printString(str, x, y, c, 1)
#define MENU_STATE_MAIN_MENU   MENU_STATE_HOME

// =============================================
// GAME CONSTANTS
// =============================================
#define GAME2_FRAME_TIME_MS 16
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
// Legacy cell aliases
#define CELL_MOVING_START 10
#define CELL_MOVING_END   11

#define GRID_OFFSET   4
#define GRID_W        (LEVEL_COLS * CELL_SIZE)
#define GRID_H        (LEVEL_ROWS * CELL_SIZE)
#define GRID_RIGHT    (GRID_OFFSET + GRID_W)
#define GRID_BOTTOM   (GRID_OFFSET + GRID_H)

// Player
#define PLAYER_RADIUS        6
#define HURTBOX_RADIUS       6
#define HITBOX_RADIUS        HURTBOX_RADIUS
#define PLAYER_COLOR_NORMAL  2
#define PLAYER_COLOR_DASH    4
#define PLAYER_COLOR_WALL    5

// Physics
#define GRAVITY_ACCEL      0.6f
#define MOVE_SPEED         3.0f
#define FALL_MULTIPLIER    1.5f
#define WALL_SLIDE_SPEED   ((GRAVITY_ACCEL * FALL_MULTIPLIER) / 2.0f)

// Jump
#define MAX_JUMP_HOLD_MS   450
#define JUMP_VELOCITY_MIN   4.0f    // tap jump
#define JUMP_VELOCITY_MAX   7.0f    // max hold jump (2.5x character height)

// Dash
#define DASH_SPEED         6.0f        // double speed
#define DASH_DISTANCE      64.0f       // 4x character height (16px)

// Dash assist
#define DASH_ASSIST_SNAP_UP   6
#define DASH_ASSIST_CEILING   3

// Falling platform
#define FALLING_DELAY_MS   1000

// Moving platform
#define MOVING_FORWARD_MS  750
#define MOVING_PAUSE_MS    1000
#define MOVING_RETURN_SPEED 1.0f

// =============================================
// COLOR DEFINITIONS (16-color palette indices)
// =============================================
// 0=Black, 1=White, 2=Red, 3=Green, 4=Blue, 5=Purple, 6=Yellow, 7=Cyan
#define COLOR_GRAY       1    // White as gray
#define COLOR_BLACK      0    // Black
#define COLOR_WHITE      1    // White
#define COLOR_RED_BROWN  2    // Red-brown (moving idle)
#define COLOR_GREEN      3    // Green (moving active)
#define COLOR_YELLOW     6    // Yellow (moving return)
#define COLOR_LIGHT_BROWN 7   // Cyan as light brown
#define COLOR_DARK_BROWN  5   // Purple as dark brown
#define COLOR_PINK       4    // Blue as pink
#define COLOR_LIME       3    // Green as lime
#define COLOR_PURPLE     5    // Purple (goal)
#define COLOR_SOFT_BLUE   4    // Blue (falling platform)
#define COLOR_SKIN       7    // Cyan as skin (spring)
#define COLOR_STRAWBERRY 2    // Red (strawberry)

// =============================================
// GAME STATE STRUCTURES
// =============================================
typedef struct {
    float x, y;
    float vx, vy;
    int radius;
    uint8_t color;
    uint8_t is_dashing;
    uint8_t dash_available;
    float dash_dir_x, dash_dir_y;
    float dash_distance_remaining;
    uint8_t is_wall_sliding;
    int8_t wall_side;
} Player;

typedef struct {
    int x, y;
    int width, height;
    uint8_t active;
    uint8_t is_falling;
    uint32_t step_on_time;
    uint8_t player_on;
    uint8_t falling_dead;
    uint32_t death_time;
} Platform;

typedef struct {
    int x, y;
    int radius;
    uint8_t active;
    uint32_t respawn_time;
} Crystal;

typedef struct {
    int start_x, start_y;
    int end_x, end_y;
    float current_x, current_y;
    int width, height;
    uint8_t active;
    uint8_t moving_forward;
    uint8_t paused;
    uint8_t triggered;
    uint32_t phase_start_time;
    float total_dist;
    float dir_x, dir_y;
} MovingPlatform;

typedef struct {
    int x, y;
    int width, height;
    uint8_t active;
} Spike;

typedef struct {
    int x, y;
    int radius;
    uint8_t active;
    uint8_t activated;
} Checkpoint;

typedef struct {
    int x, y;
    int width, height;
    uint8_t active;
} Spring;

typedef struct {
    int x, y;
    int radius;
    uint8_t collected;
} Strawberry;

typedef struct {
    int x, y;
    int width, height;
    uint8_t active;
} Goal;

// =============================================
// GAME STATE
// =============================================
static Player player;
// Trail effect
#define TRAIL_LENGTH 5
static float trail_x[TRAIL_LENGTH], trail_y[TRAIL_LENGTH];
static uint8_t trail_age[TRAIL_LENGTH];
static int trail_head;
static Platform platforms[LEVEL_COLS * LEVEL_ROWS];
static int platform_count;
static Crystal crystals[10];
static int crystal_count;
static MovingPlatform moving_platforms[5];
static int moving_platform_count;
static Spike spikes[20];
static int spike_count;
static Checkpoint checkpoints[5];
static int checkpoint_count;
static Spring springs[10];
static int spring_count;
static Strawberry strawberries[6];
static int strawberry_count;
static Goal goal;
static float respawn_x, respawn_y;
static uint8_t player_dying = 0;
static uint32_t death_start_time = 0;
static uint8_t game_over = 0;
static uint8_t pending_transition = 0;
static uint32_t game_score = 0;
static int current_level = 0;
static int total_strawberries = 0;
static int level_strawberries = 0;

// Jump state
static uint8_t jump_held = 0;
static uint32_t jump_hold_start = 0;
static uint8_t is_jumping = 0;

// Button state
static uint8_t prev_btn4 = 0;
static uint8_t curr_btn4 = 0;

// Moving platform carry
static int8_t standing_on_moving = -1;
static float prev_mp_x[5] = {0};
static float prev_mp_y[5] = {0};
static uint8_t mp_positions_initialized = 0;

// =============================================
// HELPER: Snap direction to 8-direction
// =============================================
static void snap_to_8dir(float* dx, float* dy) {
    if (fabs(*dx) < 0.01f && fabs(*dy) < 0.01f) { *dx = 1.0f; *dy = 0.0f; return; }
    float angle = atan2f(*dy, *dx) * 180.0f / 3.14159f;
    if (angle < 0) angle += 360.0f;
    int octant = (int)((angle + 22.5f) / 45.0f) % 8;
    switch (octant) {
        case 0: *dx = 1.0f;  *dy = 0.0f;   break;
        case 1: *dx = 0.707f; *dy = 0.707f; break;
        case 2: *dx = 0.0f;  *dy = 1.0f;   break;
        case 3: *dx = -0.707f; *dy = 0.707f; break;
        case 4: *dx = -1.0f; *dy = 0.0f;   break;
        case 5: *dx = -0.707f; *dy = -0.707f; break;
        case 6: *dx = 0.0f;  *dy = -1.0f;  break;
        case 7: *dx = 0.707f; *dy = -0.707f; break;
    }
}

// =============================================
// LEVEL LOADER
// =============================================
static void load_level(int level_idx) {
    if (level_idx < 0 || level_idx >= NUM_LEVELS) level_idx = 0;
    current_level = level_idx;

    const uint8_t (*level)[LEVEL_COLS] = level_list[level_idx];

    platform_count = 0;
    crystal_count = 0;
    moving_platform_count = 0;
    spike_count = 0;
    checkpoint_count = 0;
    spring_count = 0;
    strawberry_count = 0;
    level_strawberries = 0;
    goal.active = 0;
    mp_positions_initialized = 0;
    respawn_x = 0;
    respawn_y = 0;

    // Pending moving platform starts (stack for pairing with END)
    int mp_pending_start_x[5] = {0};
    int mp_pending_start_y[5] = {0};
    int mp_pending_count = 0;

    for (int row = 0; row < LEVEL_ROWS; row++) {
        for (int col = 0; col < LEVEL_COLS; col++) {
            uint8_t cell = level[row][col];
            int px = col * CELL_SIZE + 4;
            int py = row * CELL_SIZE + 4;

            switch (cell) {
                case CELL_PLATFORM:
                    platforms[platform_count].x = px;
                    platforms[platform_count].y = py;
                    platforms[platform_count].width = CELL_SIZE;
                    platforms[platform_count].height = CELL_SIZE;
                    platforms[platform_count].active = 1;
                    platforms[platform_count].is_falling = 0;
                    platforms[platform_count].player_on = 0;
                    platforms[platform_count].step_on_time = 0;
                    platform_count++;
                    break;

                case CELL_FALLING:
                    platforms[platform_count].x = px;
                    platforms[platform_count].y = py;
                    platforms[platform_count].width = CELL_SIZE;
                    platforms[platform_count].height = CELL_SIZE;
                    platforms[platform_count].active = 1;
                    platforms[platform_count].is_falling = 1;
                    platforms[platform_count].player_on = 0;
                    platforms[platform_count].step_on_time = 0;
                    platforms[platform_count].falling_dead = 0;
                    platform_count++;
                    break;

                case CELL_SPIKE:
                    spikes[spike_count].x = px;
                    spikes[spike_count].y = py;
                    spikes[spike_count].width = CELL_SIZE;
                    spikes[spike_count].height = CELL_SIZE;
                    spikes[spike_count].active = 1;
                    spike_count++;
                    break;

                case CELL_MOVING_START:
                    mp_pending_start_x[mp_pending_count] = px;
                    mp_pending_start_y[mp_pending_count] = py;
                    mp_pending_count++;
                    break;

                case CELL_MOVING_END:
                    if (mp_pending_count > 0) {
                        mp_pending_count--;
                        int sx = mp_pending_start_x[mp_pending_count];
                        int sy = mp_pending_start_y[mp_pending_count];
                        MovingPlatform* mp = &moving_platforms[moving_platform_count];
                        mp->start_x = sx;
                        mp->start_y = sy;
                        mp->end_x = px;
                        mp->end_y = py;
                        mp->current_x = (float)sx;
                        mp->current_y = (float)sy;
                        mp->width = CELL_SIZE;
                        mp->height = CELL_SIZE;
                        mp->active = 1;
                        mp->moving_forward = 1;
                        mp->paused = 1;
                        mp->triggered = 0;
                        mp->phase_start_time = HAL_GetTick();
                        float dx = (float)(px - sx);
                        float dy = (float)(py - sy);
                        mp->total_dist = sqrtf(dx * dx + dy * dy);
                        if (mp->total_dist > 0.1f) {
                            mp->dir_x = dx / mp->total_dist;
                            mp->dir_y = dy / mp->total_dist;
                        } else {
                            mp->dir_x = 1.0f;
                            mp->dir_y = 0.0f;
                        }
                        moving_platform_count++;
                    }
                    break;
                    
                case CELL_CRYSTAL:
                    crystals[crystal_count].x = px + CELL_SIZE / 2;
                    crystals[crystal_count].y = py + CELL_SIZE / 2;
                    crystals[crystal_count].radius = CELL_SIZE / 4;
                    crystals[crystal_count].active = 1;
                    crystals[crystal_count].respawn_time = 0;
                    crystal_count++;
                    break;
                    
                case CELL_CHECKPOINT:
                    checkpoints[checkpoint_count].x = px + CELL_SIZE / 2;
                    checkpoints[checkpoint_count].y = py + CELL_SIZE / 2;
                    checkpoints[checkpoint_count].radius = CELL_SIZE / 4;
                    checkpoints[checkpoint_count].active = 1;
                    checkpoints[checkpoint_count].activated = 0;
                    checkpoint_count++;
                    if (respawn_x == 0 && respawn_y == 0) {
                        respawn_x = (float)(px + CELL_SIZE / 2);
                        respawn_y = (float)(py + CELL_SIZE / 2);
                    }
                    break;
                    
                    
                case CELL_GOAL:
                    goal.x = px;
                    goal.y = py;
                    goal.width = CELL_SIZE;
                    goal.height = CELL_SIZE;
                    goal.active = 1;
                    break;
                    
                case CELL_SPRING:
                    springs[spring_count].x = px;
                    springs[spring_count].y = py;
                    springs[spring_count].width = CELL_SIZE;
                    springs[spring_count].height = CELL_SIZE;
                    springs[spring_count].active = 1;
                    spring_count++;
                    break;
                    
                case CELL_STRAWBERRY:
                    strawberries[strawberry_count].x = px + CELL_SIZE / 2;
                    strawberries[strawberry_count].y = py + CELL_SIZE / 2;
                    strawberries[strawberry_count].radius = CELL_SIZE / 3;
                    strawberries[strawberry_count].collected = 0;
                    strawberry_count++;
                    level_strawberries++;
                    break;
                    
                default:
                    break;
            }
        }
    }

    // Fallback spawn point if no checkpoint in level
    if (respawn_x == 0 && respawn_y == 0) {
        for (int i = 0; i < platform_count; i++) {
            if (platforms[i].active && !platforms[i].is_falling) {
                respawn_x = (float)(platforms[i].x + platforms[i].width / 2);
                respawn_y = (float)(platforms[i].y - PLAYER_RADIUS - 1);
                break;
            }
        }
        if (respawn_x == 0 && respawn_y == 0) {
            respawn_x = GRID_OFFSET + PLAYER_RADIUS + 1;
            respawn_y = GRID_OFFSET + PLAYER_RADIUS + 1;
        }
    }
}

// =============================================
// PLAYER INITIALIZATION
// =============================================
static void init_player(void) {
    player.x = respawn_x;
    player.y = respawn_y;
    if (player.x - PLAYER_RADIUS < GRID_OFFSET) player.x = GRID_OFFSET + PLAYER_RADIUS;
    if (player.x + PLAYER_RADIUS > GRID_RIGHT) player.x = GRID_RIGHT - PLAYER_RADIUS;
    if (player.y - PLAYER_RADIUS < GRID_OFFSET) player.y = GRID_OFFSET + PLAYER_RADIUS;
    if (player.y + PLAYER_RADIUS > GRID_BOTTOM) player.y = GRID_BOTTOM - PLAYER_RADIUS;
    player.vx = 0;
    player.vy = 0;
    player.radius = PLAYER_RADIUS;
    player.color = PLAYER_COLOR_NORMAL;
    player.is_dashing = 0;
    player.dash_available = 1;
    player.dash_dir_x = 0;
    player.dash_dir_y = 0;
    player.dash_distance_remaining = 0;
    player.is_wall_sliding = 0;
    player.wall_side = 0;
    player_dying = 0;
    death_start_time = 0;
    jump_held = 0;
    jump_hold_start = 0;
    is_jumping = 0;
    prev_btn4 = 0;
    curr_btn4 = 0;
    standing_on_moving = -1;
    mp_positions_initialized = 0;
}

// =============================================
// COLLISION DETECTION
// =============================================
static uint8_t check_rect_collision(float px, float py, int r, int rx, int ry, int rw, int rh) {
    float closest_x = (px < rx) ? rx : (px > rx + rw) ? rx + rw : px;
    float closest_y = (py < ry) ? ry : (py > ry + rh) ? ry + rh : py;
    float dx = px - closest_x;
    float dy = py - closest_y;
    return (dx * dx + dy * dy <= (float)(r * r));
}

static uint8_t check_moving_collision(MovingPlatform* mp, float px, float py, int r) {
    if (!mp->active) return 0;
    float closest_x = (px < mp->current_x) ? mp->current_x : (px > mp->current_x + mp->width) ? mp->current_x + mp->width : px;
    float closest_y = (py < mp->current_y) ? mp->current_y : (py > mp->current_y + mp->height) ? mp->current_y + mp->height : py;
    float dx = px - closest_x;
    float dy = py - closest_y;
    return (dx * dx + dy * dy <= (float)(r * r));
}

// =============================================
// CHECK WALL SLIDE
// =============================================
static int8_t check_wall_slide(void) {
    float check_offset = (float)HURTBOX_RADIUS + 1.0f;
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active) continue;
        float test_x_right = player.x + check_offset;
        if (test_x_right >= platforms[i].x && test_x_right <= platforms[i].x + platforms[i].width) {
            float hurt_top = player.y - HURTBOX_RADIUS;
            float hurt_bot = player.y + HURTBOX_RADIUS;
            if (hurt_bot > platforms[i].y && hurt_top < platforms[i].y + platforms[i].height) {
                if (player.y + HURTBOX_RADIUS > platforms[i].y + 2) return 1;
            }
        }
        float test_x_left = player.x - check_offset;
        if (test_x_left >= platforms[i].x && test_x_left <= platforms[i].x + platforms[i].width) {
            float hurt_top = player.y - HURTBOX_RADIUS;
            float hurt_bot = player.y + HURTBOX_RADIUS;
            if (hurt_bot > platforms[i].y && hurt_top < platforms[i].y + platforms[i].height) {
                if (player.y + HURTBOX_RADIUS > platforms[i].y + 2) return -1;
            }
        }
    }
    return 0;
}

// =============================================
// DASH ASSIST
// =============================================
static uint8_t dash_assist_horizontal_snap(void) {
    float hitbox_bottom = player.y + HITBOX_RADIUS;
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active) continue;
        float p_left = player.x - HITBOX_RADIUS;
        float p_right = player.x + HITBOX_RADIUS;
        if (p_right > platforms[i].x && p_left < platforms[i].x + platforms[i].width) {
            float dist_below = hitbox_bottom - platforms[i].y;
            if (dist_below > 0 && dist_below <= DASH_ASSIST_SNAP_UP) {
                player.y = platforms[i].y - HITBOX_RADIUS;
                player.vy = 0;
                is_jumping = 0;
                player.is_wall_sliding = 0;
                player.wall_side = 0;
                return 1;
            }
        }
    }
    return 0;
}

// =============================================
// UPDATE MOVING PLATFORMS
// =============================================
static void update_moving_platforms(void) {
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < moving_platform_count; i++) {
        if (!moving_platforms[i].active) continue;
        MovingPlatform* mp = &moving_platforms[i];
        if (mp->paused) {
            // Stay paused until triggered by player stepping on it
        } else if (mp->moving_forward) {
            float progress = (float)(now - mp->phase_start_time) / (float)MOVING_FORWARD_MS;
            if (progress >= 1.0f) {
                mp->current_x = (float)mp->end_x;
                mp->current_y = (float)mp->end_y;
                mp->moving_forward = 0;
                mp->phase_start_time = now;
            } else {
                mp->current_x = mp->start_x + mp->dir_x * mp->total_dist * progress;
                mp->current_y = mp->start_y + mp->dir_y * mp->total_dist * progress;
            }
        } else {
            float return_time = (float)MOVING_FORWARD_MS / MOVING_RETURN_SPEED;
            float progress = (float)(now - mp->phase_start_time) / return_time;
            if (progress >= 1.0f) {
                mp->current_x = (float)mp->start_x;
                mp->current_y = (float)mp->start_y;
                mp->moving_forward = 1;
                mp->paused = 1;   // Stay still after returning
                mp->triggered = 0; // Re-triggerable
                mp->phase_start_time = now;
                mp->phase_start_time = now;
            } else {
                mp->current_x = mp->end_x - mp->dir_x * mp->total_dist * progress;
                mp->current_y = mp->end_y - mp->dir_y * mp->total_dist * progress;
            }
        }
    }
}

// =============================================
// PHYSICS UPDATE
// =============================================
static void update_physics(void) {
    uint32_t now = HAL_GetTick();
    
    update_moving_platforms();
    
    // ---- DASH ----
    if (player.is_dashing) {
        float step = DASH_SPEED;
        if (step > player.dash_distance_remaining) step = player.dash_distance_remaining;
        player.x += player.dash_dir_x * step;
        player.y += player.dash_dir_y * step;
        player.dash_distance_remaining -= step;
        if (fabs(player.dash_dir_y) < 0.1f) dash_assist_horizontal_snap();
        if (player.x - player.radius < GRID_OFFSET) { player.x = GRID_OFFSET + player.radius; player.dash_distance_remaining = 0; }
        if (player.x + player.radius > GRID_RIGHT) { player.x = GRID_RIGHT - player.radius; player.dash_distance_remaining = 0; }
        if (player.y - player.radius < GRID_OFFSET) { player.y = GRID_OFFSET + player.radius; player.dash_distance_remaining = 0; }
        if (player.y + player.radius > GRID_BOTTOM) { player.y = GRID_BOTTOM - player.radius; player.dash_distance_remaining = 0; }
        if (player.dash_distance_remaining <= 0) {
            player.is_dashing = 0;
            player.color = PLAYER_COLOR_NORMAL;
            player.dash_available = 0;
            player.vx = 0; player.vy = 0;
        }
        return;
    }
    
    // ---- WALL SLIDE ----
    int8_t wall = check_wall_slide();
    // Wall-jump only: detect wall contact but no sticking/sliding
    if (wall != 0 && !is_jumping) {
        player.wall_side = wall;   // remember wall direction for wall-jump
    } else if (is_jumping || player.vy <= 0) {
        player.wall_side = 0;
    }
    if (wall == 0) player.wall_side = 0;
    player.is_wall_sliding = 0;  // sliding disabled
    if (!player.is_dashing) player.color = PLAYER_COLOR_NORMAL;
    
    // ---- PHYSICS ----
    if (!player.is_wall_sliding) {
        float joy_x = -(float)joystick_data.coord_mapped.x;
        if (joy_x > 0.2f || joy_x < -0.2f) {
            player.vx = joy_x * MOVE_SPEED;
        } else {
            if (is_jumping) player.vx *= 0.5f;
            else player.vx *= 0.2f;
            if (fabs(player.vx) < 0.1f) player.vx = 0;
        }
        if (is_jumping) {
            if (player.vy > 0) player.vy += GRAVITY_ACCEL * FALL_MULTIPLIER;
            else player.vy += GRAVITY_ACCEL;
        } else {
            player.vy += GRAVITY_ACCEL * FALL_MULTIPLIER;
            if (player.vy > 15.0f) player.vy = 15.0f;
        }
    } else {
        float joy_x = -(float)joystick_data.coord_mapped.x;
        if (joy_x > 0.2f || joy_x < -0.2f) player.vx = joy_x * MOVE_SPEED;
        else { player.vx *= 0.3f; if (fabs(player.vx) < 0.1f) player.vx = 0; }
    }
    
    // Trail: record position during dash or fast falling
    if (player.is_dashing || (!is_jumping && player.vy > 4.0f)) {
        trail_x[trail_head] = player.x;
        trail_y[trail_head] = player.y;
        trail_age[trail_head] = TRAIL_LENGTH;
        trail_head = (trail_head + 1) % TRAIL_LENGTH;
    }
    // Age trail
    for (int t = 0; t < TRAIL_LENGTH; t++) {
        if (trail_age[t] > 0) trail_age[t]--;
    }

    player.x += player.vx;
    player.y += player.vy;
    
    // ---- BOUNDARIES ----
    if (player.x - player.radius < GRID_OFFSET) { player.x = GRID_OFFSET + player.radius; player.vx = 0; }
    if (player.x + player.radius > GRID_RIGHT) { player.x = GRID_RIGHT - player.radius; player.vx = 0; }
    if (player.y + player.radius > SCREEN_HEIGHT) {
        player.y = SCREEN_HEIGHT - player.radius;
        player.vy = 0;
        is_jumping = 0;
        player.is_wall_sliding = 0;
        player.wall_side = 0;
    }
    if (player.y - player.radius < GRID_OFFSET) { player.y = GRID_OFFSET + player.radius; player.vy = 0; }
    
    // ---- PLATFORM COLLISION (hitbox) ----
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active || platforms[i].falling_dead) continue;
        if (!check_rect_collision(player.x, player.y, HITBOX_RADIUS, platforms[i].x, platforms[i].y, platforms[i].width, platforms[i].height)) continue;
        
        float prev_y = player.y - player.vy;
        float prev_x = player.x - player.vx;
        
        // Landing on top
        if (prev_y + HITBOX_RADIUS <= platforms[i].y && player.vy >= 0) {
            player.y = platforms[i].y - HITBOX_RADIUS;
            player.vy = 0;
            is_jumping = 0;
            player.dash_available = 1;
            player.is_wall_sliding = 0;
            player.wall_side = 0;

            // Falling platform check
            if (platforms[i].is_falling && !platforms[i].player_on) {
                platforms[i].player_on = 1;
                platforms[i].step_on_time = now;
            }
        }
        // Hit head
        else if (prev_y - HITBOX_RADIUS >= platforms[i].y + platforms[i].height - DASH_ASSIST_CEILING && player.vy < 0) {
            player.y = platforms[i].y + platforms[i].height + HITBOX_RADIUS;
            player.vy = 0;
        }
        // Side collision
        else {
            if (prev_x + HITBOX_RADIUS <= platforms[i].x) {
                player.x = platforms[i].x - HITBOX_RADIUS;
            } else if (prev_x - HITBOX_RADIUS >= platforms[i].x + platforms[i].width) {
                player.x = platforms[i].x + platforms[i].width + HITBOX_RADIUS;
            }
        }
    }
    
    // ---- FALLING PLATFORM TIMER ----
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active || !platforms[i].is_falling) continue;
        if (platforms[i].player_on) {
            if (now - platforms[i].step_on_time >= FALLING_DELAY_MS) {
                platforms[i].falling_dead = 1;
                platforms[i].death_time = now;
            }
        }
        // Respawn after 5 seconds
        if (platforms[i].falling_dead && now - platforms[i].death_time >= 5000) {
            platforms[i].falling_dead = 0;
            platforms[i].player_on = 0;
            platforms[i].step_on_time = 0;
        }
        // Reset player_on if player leaves
        if (!check_rect_collision(player.x, player.y, HITBOX_RADIUS, platforms[i].x, platforms[i].y, platforms[i].width, platforms[i].height)) {
            platforms[i].player_on = 0;
        }
    }
    
    // ---- MOVING PLATFORM COLLISION ----
    standing_on_moving = -1;
    for (int i = 0; i < moving_platform_count; i++) {
        if (!moving_platforms[i].active) continue;
        if (!check_moving_collision(&moving_platforms[i], player.x, player.y, HITBOX_RADIUS)) continue;
        
        float prev_y = player.y - player.vy;
        if (prev_y + HITBOX_RADIUS <= moving_platforms[i].current_y && player.vy >= 0) {
            player.y = moving_platforms[i].current_y - HITBOX_RADIUS;
            player.vy = 0;
            is_jumping = 0;
            player.dash_available = 1;
            standing_on_moving = i;
            // Trigger moving platform on first step
            if (!moving_platforms[i].triggered) {
                moving_platforms[i].triggered = 1;
                moving_platforms[i].paused = 0;
            }
            player.is_wall_sliding = 0;
            player.wall_side = 0;
        }
    }
    
    // ---- CARRY BY MOVING PLATFORM ----
    if (!mp_positions_initialized) {
        for (int i = 0; i < moving_platform_count; i++) {
            prev_mp_x[i] = moving_platforms[i].current_x;
            prev_mp_y[i] = moving_platforms[i].current_y;
        }
        mp_positions_initialized = 1;
    }
    if (standing_on_moving >= 0) {
        int idx = standing_on_moving;
        float dx = moving_platforms[idx].current_x - prev_mp_x[idx];
        float dy = moving_platforms[idx].current_y - prev_mp_y[idx];
        player.x += dx;
        player.y += dy;
    }
    for (int i = 0; i < moving_platform_count; i++) {
        prev_mp_x[i] = moving_platforms[i].current_x;
        prev_mp_y[i] = moving_platforms[i].current_y;
    }
    
    // ---- SPRING COLLISION ----
    for (int i = 0; i < spring_count; i++) {
        if (!springs[i].active) continue;
        // Check with half hitbox (player's bottom half on spring)
        float half_hitbox = HITBOX_RADIUS / 2.0f;
        float check_y = player.y + half_hitbox;
        if (check_rect_collision(player.x, check_y, (int)half_hitbox, springs[i].x, springs[i].y, springs[i].width, springs[i].height)) {
            // Bounce up! (3.5x character height = 56px)
            player.vy = -8.0f;
            is_jumping = 1;
            jump_held = 0;
            player.dash_available = 1;  // Restore dash
            buzzer_tone(&buzzer_cfg, 2000, 30);
            HAL_Delay(5);
            buzzer_off(&buzzer_cfg);
        }
    }
    
    // ---- SPIKE COLLISION (hurtbox) ----
    if (!player_dying && !game_over) {
        for (int i = 0; i < spike_count; i++) {
            if (!spikes[i].active) continue;
            float hurt_left = player.x - HURTBOX_RADIUS;
            float hurt_right = player.x + HURTBOX_RADIUS;
            float hurt_top = player.y - HURTBOX_RADIUS;
            float hurt_bot = player.y + HURTBOX_RADIUS;
            if (hurt_right > spikes[i].x && hurt_left < spikes[i].x + spikes[i].width &&
                hurt_bot > spikes[i].y && hurt_top < spikes[i].y + spikes[i].height) {
                player_dying = 1;
                death_start_time = now;
                player.color = 1;
                buzzer_tone(&buzzer_cfg, 100, 200);
                break;
            }
        }
    }
    
    // ---- DEATH RESPAWN ----
    if (player_dying) {
        if (now - death_start_time >= 500) {
            player.x = respawn_x;
            player.y = respawn_y;
            player.vx = 0;
            player.vy = 0;
            player.color = PLAYER_COLOR_NORMAL;
            player.is_dashing = 0;
            player.dash_available = 1;
            player.is_wall_sliding = 0;
            player.wall_side = 0;
            player_dying = 0;
            is_jumping = 0;
            standing_on_moving = -1;
            buzzer_off(&buzzer_cfg);
        }
    }
    
    // ---- CHECKPOINT COLLISION ----
    if (!player_dying) {
        for (int i = 0; i < checkpoint_count; i++) {
            if (!checkpoints[i].active) continue;
            float dx = player.x - checkpoints[i].x;
            float dy = player.y - checkpoints[i].y;
            float dist_sq = dx * dx + dy * dy;
            float collide_dist = (float)(HITBOX_RADIUS + checkpoints[i].radius);
            if (dist_sq <= collide_dist * collide_dist) {
                if (!checkpoints[i].activated) {
                    checkpoints[i].activated = 1;
                    respawn_x = (float)checkpoints[i].x;
                    respawn_y = (float)checkpoints[i].y;
                    buzzer_tone(&buzzer_cfg, 1500, 20);
                    HAL_Delay(10);
                    buzzer_off(&buzzer_cfg);
                }
            }
        }
    }
    
    // ---- STRAWBERRY COLLISION ----
    if (!player_dying) {
        for (int i = 0; i < strawberry_count; i++) {
            if (strawberries[i].collected) continue;
            float dx = player.x - strawberries[i].x;
            float dy = player.y - strawberries[i].y;
            float dist_sq = dx * dx + dy * dy;
            float collide_dist = (float)(HITBOX_RADIUS + strawberries[i].radius);
            if (dist_sq <= collide_dist * collide_dist) {
                strawberries[i].collected = 1;
                total_strawberries++;
                game_score += 100;
                buzzer_tone(&buzzer_cfg, 3000, 15);
                HAL_Delay(5);
                buzzer_off(&buzzer_cfg);
            }
        }
    }
    
    // ---- GOAL COLLISION ----
    if (!player_dying && !pending_transition && goal.active) {
        if (check_rect_collision(player.x, player.y, HITBOX_RADIUS, goal.x, goal.y, goal.width, goal.height)) {
            game_score += 500;
            if (current_level < NUM_LEVELS - 1) {
                pending_transition = 1;
            } else {
                game_over = 1;
            }
        }
    }
    
    // ---- CRYSTAL COLLISION ----
    for (int i = 0; i < crystal_count; i++) {
        if (!crystals[i].active) continue;
        float dx = player.x - crystals[i].x;
        float dy = player.y - crystals[i].y;
        float dist_sq = dx * dx + dy * dy;
        float collide_dist = (float)(HITBOX_RADIUS + crystals[i].radius);
        if (dist_sq <= collide_dist * collide_dist) {
            crystals[i].active = 0;
            crystals[i].respawn_time = now + 5000;
            player.dash_available = 1;  // Restore dash
            buzzer_tone(&buzzer_cfg, 2500, 20);
            HAL_Delay(5);
            buzzer_off(&buzzer_cfg);
        }
    }
    
    // ---- RESPAWN CRYSTALS ----
    for (int i = 0; i < crystal_count; i++) {
        if (!crystals[i].active && now >= crystals[i].respawn_time) {
            crystals[i].active = 1;
        }
    }
}

// =============================================
// RENDER
// =============================================
static void render(void) {
    LCD_Clear(cfg0, 0);
    
    // ---- DRAW PLATFORMS ----
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active) continue;
        uint8_t color;
        if (platforms[i].falling_dead) {
            color = COLOR_BLACK;
        } else if (platforms[i].is_falling) {
            color = COLOR_SOFT_BLUE;
        } else {
            color = COLOR_GRAY;
        }
        LCD_Fill_Rect(cfg0, platforms[i].x, platforms[i].y, platforms[i].width, platforms[i].height, color);
    }
    
    // ---- DRAW SPIKES ----
    for (int i = 0; i < spike_count; i++) {
        if (!spikes[i].active) continue;
        int cx = spikes[i].x + spikes[i].width / 2;
        int by = spikes[i].y + spikes[i].height;
        for (int py = spikes[i].y; py < by; py++) {
            int half_w = (spikes[i].width * (py - spikes[i].y)) / (2 * spikes[i].height);
            for (int px = cx - half_w; px <= cx + half_w; px++) {
                LCD_Set_Pixel(px, py, COLOR_WHITE);
            }
        }
    }
    
    // ---- DRAW MOVING PLATFORMS ----
    for (int i = 0; i < moving_platform_count; i++) {
        if (!moving_platforms[i].active) continue;
        uint8_t mp_color;
        if (moving_platforms[i].paused) mp_color = COLOR_YELLOW;
        else if (moving_platforms[i].moving_forward) mp_color = COLOR_GREEN;
        else mp_color = COLOR_RED_BROWN;
        LCD_Fill_Rect(cfg0, (int)moving_platforms[i].current_x, (int)moving_platforms[i].current_y,
                      moving_platforms[i].width, moving_platforms[i].height, mp_color);
    }
    
    // ---- DRAW CRYSTALS ----
    for (int i = 0; i < crystal_count; i++) {
        if (!crystals[i].active) continue;
        LCD_Draw_Circle((uint16_t)crystals[i].x, (uint16_t)crystals[i].y, crystals[i].radius, COLOR_PINK, 1);
    }
    
    // ---- DRAW CHECKPOINTS ----
    for (int i = 0; i < checkpoint_count; i++) {
        if (!checkpoints[i].active) continue;
        uint8_t cp_color = checkpoints[i].activated ? COLOR_GREEN : COLOR_LIME;
        LCD_Draw_Circle((uint16_t)checkpoints[i].x, (uint16_t)checkpoints[i].y, checkpoints[i].radius, cp_color, 1);
        for (int py = checkpoints[i].y - checkpoints[i].radius; py > checkpoints[i].y - checkpoints[i].radius - 8; py--) {
            LCD_Set_Pixel(checkpoints[i].x, py, cp_color);
        }
        for (int px = checkpoints[i].x; px < checkpoints[i].x + 6; px++) {
            LCD_Set_Pixel(px, checkpoints[i].y - checkpoints[i].radius - 8, cp_color);
        }
    }
    
    // ---- DRAW SPRINGS ----
    for (int i = 0; i < spring_count; i++) {
        if (!springs[i].active) continue;
        int sx = springs[i].x;
        int sy = springs[i].y;
        int sw = springs[i].width;
        int sh = springs[i].height;
        int cx = sx + sw / 2;
        
        // Fill background with skin color
        LCD_Fill_Rect(cfg0, sx, sy, sw, sh, COLOR_SKIN);
        
        // Draw spring as a zigzag coil pattern
        // Top and bottom horizontal bars
        for (int px = sx + 2; px < sx + sw - 2; px++) {
            LCD_Set_Pixel(px, sy + 1, COLOR_WHITE);       // top bar
            LCD_Set_Pixel(px, sy + sh - 2, COLOR_WHITE);  // bottom bar
        }
        
        // Zigzag coil lines
        int coil_count = 4;
        int coil_spacing = (sh - 4) / coil_count;
        for (int c = 0; c < coil_count; c++) {
            int base_y = sy + 2 + c * coil_spacing;
            // Left side of coil
            LCD_Set_Pixel(cx - 4, base_y, COLOR_WHITE);
            LCD_Set_Pixel(cx - 3, base_y + 1, COLOR_WHITE);
            LCD_Set_Pixel(cx - 2, base_y + 2, COLOR_WHITE);
            // Right side of coil  
            LCD_Set_Pixel(cx + 2, base_y + 2, COLOR_WHITE);
            LCD_Set_Pixel(cx + 3, base_y + 1, COLOR_WHITE);
            LCD_Set_Pixel(cx + 4, base_y, COLOR_WHITE);
            // Connect left to right (bottom of coil)
            LCD_Set_Pixel(cx - 1, base_y + 3, COLOR_WHITE);
            LCD_Set_Pixel(cx, base_y + 3, COLOR_WHITE);
            LCD_Set_Pixel(cx + 1, base_y + 3, COLOR_WHITE);
        }
        
        // Vertical center line (spring core)
        for (int py = sy + 2; py < sy + sh - 2; py += 2) {
            LCD_Set_Pixel(cx, py, COLOR_WHITE);
        }
    }
    
    // ---- DRAW STRAWBERRIES ----
    for (int i = 0; i < strawberry_count; i++) {
        if (strawberries[i].collected) continue;
        // Draw strawberry as a red circle with green top
        LCD_Draw_Circle((uint16_t)strawberries[i].x, (uint16_t)strawberries[i].y, strawberries[i].radius, COLOR_STRAWBERRY, 1);
        // Green stem
        LCD_Set_Pixel(strawberries[i].x, strawberries[i].y - strawberries[i].radius - 2, COLOR_GREEN);
        LCD_Set_Pixel(strawberries[i].x - 1, strawberries[i].y - strawberries[i].radius - 1, COLOR_GREEN);
        LCD_Set_Pixel(strawberries[i].x + 1, strawberries[i].y - strawberries[i].radius - 1, COLOR_GREEN);
    }
    
    // ---- DRAW GOAL ----
    if (goal.active) {
        LCD_Fill_Rect(cfg0, goal.x, goal.y, goal.width, goal.height, COLOR_PURPLE);
        // Draw "G" letter
        int gcx = goal.x + goal.width / 2;
        int gcy = goal.y + goal.height / 2;
        LCD_Set_Pixel(gcx - 2, gcy - 3, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy - 2, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy - 1, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy + 1, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy + 2, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 2, gcy + 3, COLOR_WHITE);
        LCD_Set_Pixel(gcx - 1, gcy - 3, COLOR_WHITE);
        LCD_Set_Pixel(gcx, gcy - 3, COLOR_WHITE);
        LCD_Set_Pixel(gcx + 1, gcy - 3, COLOR_WHITE);
        LCD_Set_Pixel(gcx + 1, gcy, COLOR_WHITE);
        LCD_Set_Pixel(gcx, gcy, COLOR_WHITE);
        LCD_Set_Pixel(gcx + 1, gcy + 1, COLOR_WHITE);
        LCD_Set_Pixel(gcx + 1, gcy + 2, COLOR_WHITE);
        LCD_Set_Pixel(gcx + 1, gcy + 3, COLOR_WHITE);
    }
    
    // ---- DRAW TRAIL ----
    for (int t = 0; t < TRAIL_LENGTH; t++) {
        if (trail_age[t] > 0) {
            int r = (player.radius * trail_age[t]) / TRAIL_LENGTH;
            if (r > 0) LCD_Draw_Circle((uint16_t)trail_x[t], (uint16_t)trail_y[t], r, player.color, 1);
        }
    }

    // ---- DRAW PLAYER ----
    if (!player_dying) {
        LCD_Draw_Circle((uint16_t)player.x, (uint16_t)player.y, player.radius, player.color, 1);
    }
    
    // ---- DRAW HUD ----
    char buf[32];
    sprintf(buf, "L%d S:%d", current_level + 1, total_strawberries);
    LCD_Print_String(cfg0, 2, 2, buf, COLOR_WHITE, 0);
    
    sprintf(buf, "SCORE:%lu", game_score);
    LCD_Print_String(cfg0, 2, 12, buf, COLOR_WHITE, 0);
    
    // Dash availability indicator
    if (!player.dash_available) {
        LCD_Print_String(cfg0, 2, 22, "DASH:NO", COLOR_RED_BROWN, 0);
    } else {
        LCD_Print_String(cfg0, 2, 22, "DASH:OK", COLOR_GREEN, 0);
    }
    
    // Game over screen
    if (game_over) {
        LCD_Print_String(cfg0, 60, 140, "YOU WIN!", COLOR_GREEN, 0);
        char score_buf[32];
        sprintf(score_buf, "Score: %lu", game_score);
        LCD_Print_String(cfg0, 60, 160, score_buf, COLOR_WHITE, 0);
        LCD_Print_String(cfg0, 40, 200, "Press BT4 to exit", COLOR_WHITE, 0);
    }
    LCD_Refresh(&cfg0);
}

// =============================================
// INPUT HANDLING
// =============================================
static void handle_input(void) {
    uint32_t now = HAL_GetTick();
    
    // Read BT4 (jump) - PA8
    curr_btn4 = (uint8_t)HAL_GPIO_ReadPin(BTN4_GPIO_Port, BTN4_Pin);
    
    // ---- JUMP (BT4) ----
    if (curr_btn4 == 0 && prev_btn4 == 1) {  // Falling edge (pressed)
        if (!player_dying && !game_over) {
            if (!is_jumping) {
                // Wall jump: press BT4 while touching wall in air
                if (player.wall_side != 0) {
                    player.vy = -JUMP_VELOCITY_MAX * 0.8f;
                    player.vx = -player.wall_side * MOVE_SPEED * 1.5f;
                    is_jumping = 1;
                    jump_held = 1;
                    jump_hold_start = now;
                    player.wall_side = 0;
                    player.color = PLAYER_COLOR_NORMAL;
                } else {
                    player.vy = -JUMP_VELOCITY_MIN;
                    is_jumping = 1;
                    jump_held = 1;
                    jump_hold_start = now;
                }
            }
        }
    }
    if (curr_btn4 == 0 && prev_btn4 == 0) {  // Held
        if (jump_held && is_jumping && !player_dying && !game_over) {
            if (now - jump_hold_start < MAX_JUMP_HOLD_MS) {
                float charge_t = (float)(now - jump_hold_start) / (float)MAX_JUMP_HOLD_MS;
                player.vy = -(JUMP_VELOCITY_MIN + (JUMP_VELOCITY_MAX - JUMP_VELOCITY_MIN) * charge_t);
            } else {
                jump_held = 0;
            }
        }
    }
    if (curr_btn4 == 1) {  // Released
        jump_held = 0;
    }
    prev_btn4 = curr_btn4;
    
    // ---- DASH (BT2) ----
    if (!player_dying && !game_over && !player.is_dashing && player.dash_available) {
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_RESET) {
            // Get direction from joystick
            float dx = -(float)joystick_data.coord_mapped.x;
            float dy = -(float)joystick_data.coord_mapped.y;
            snap_to_8dir(&dx, &dy);
            player.dash_dir_x = dx;
            player.dash_dir_y = dy;
            player.is_dashing = 1;
            player.dash_distance_remaining = DASH_DISTANCE;
            player.color = PLAYER_COLOR_DASH;
            buzzer_tone(&buzzer_cfg, 1000, 30);
            HAL_Delay(5);
            buzzer_off(&buzzer_cfg);
        }
    }
    
    // ---- EXIT ON GAME OVER ----
    if (game_over && curr_btn4 == 0 && prev_btn4 == 1) {
        // Will exit in main loop
    }
}

// =============================================
// MAIN GAME LOOP
// =============================================
MenuState Game2_Run(void) {
    // Initialize
    total_strawberries = 0;
    game_score = 0;
    game_over = 0;
    pending_transition = 0;
    current_level = 0;

    load_level(0);
    init_player();

    uint32_t last_frame = HAL_GetTick();

    while (1) {
        uint32_t now = HAL_GetTick();
        if (now - last_frame < GAME2_FRAME_TIME_MS) continue;
        last_frame = now;

        // Read joystick
        Joystick_Read(&joystick_cfg, &joystick_data);

        // Handle input
        handle_input();

        // Update physics
        update_physics();

        // Render
        render();

        // Deferred level transition (safe point, outside physics)
        if (pending_transition) {
            pending_transition = 0;
            load_level(current_level + 1);
            init_player();
        }

        // Exit condition
        if (game_over) {
            if (HAL_GPIO_ReadPin(BTN4_GPIO_Port, BTN4_Pin) == GPIO_PIN_RESET) {
                HAL_Delay(300);
                break;
            }
        }

        // Check if fallen off screen
        if (player.y > SCREEN_HEIGHT + 50 && !player_dying && !game_over) {
            player_dying = 1;
            death_start_time = now;
        }
    }
    
    return MENU_STATE_MAIN_MENU;
}
