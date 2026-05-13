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

// =============================================
// GAME CONSTANTS
// =============================================
#define GAME2_FRAME_TIME_MS 16
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Player
#define PLAYER_RADIUS        6
#define HURTBOX_RADIUS       6
#define HITBOX_RADIUS        8
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
#define JUMP_VELOCITY_MAX  10.0f

// Dash
#define DASH_SPEED         (MOVE_SPEED * 2.0f)
#define DASH_DISTANCE      125.0f
#define DASH_COOLDOWN_MS   500

// Dash assist
#define DASH_ASSIST_SNAP_UP   6
#define DASH_ASSIST_CEILING   3

// Falling platform
#define FALLING_DELAY_MS   1000

// Moving platform
#define MOVING_FORWARD_MS  1500
#define MOVING_PAUSE_MS    1000
#define MOVING_RETURN_SPEED 0.5f

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
    uint32_t dash_cooldown_end;
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
static uint32_t game_score = 0;
static int current_level = 0;
static int total_strawberries = 0;
static int level_strawberries = 0;

// Jump state
static uint8_t jump_held = 0;
static uint32_t jump_hold_start = 0;
static uint8_t is_jumping = 0;
static float jump_dir_x = 1, jump_dir_y = -1;

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
    if (level_idx < 0 || level_idx >= 12) level_idx = 0;
    current_level = level_idx;
    
    const uint8_t* level = all_levels[level_idx];
    
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
    
    for (int row = 0; row < LEVEL_ROWS; row++) {
        for (int col = 0; col < LEVEL_COLS; col++) {
            uint8_t cell = level[row * LEVEL_COLS + col];
            int px = col * CELL_SIZE;
            int py = row * CELL_SIZE;
            
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
                    
                case CELL_MOVING_PLATFORM:
                    moving_platforms[moving_platform_count].current_x = (float)px;
                    moving_platforms[moving_platform_count].current_y = (float)py;
                    moving_platforms[moving_platform_count].width = CELL_SIZE;
                    moving_platforms[moving_platform_count].height = CELL_SIZE;
                    moving_platforms[moving_platform_count].active = 1;
                    moving_platforms[moving_platform_count].moving_forward = 1;
                    moving_platforms[moving_platform_count].paused = 0;
                    moving_platforms[moving_platform_count].phase_start_time = HAL_GetTick();
                    moving_platforms[moving_platform_count].start_x = px;
                    moving_platforms[moving_platform_count].start_y = py;
                    moving_platforms[moving_platform_count].end_x = px + CELL_SIZE * 3;
                    moving_platforms[moving_platform_count].end_y = py;
                    {
                        float dx = (float)(moving_platforms[moving_platform_count].end_x - moving_platforms[moving_platform_count].start_x);
                        float dy = (float)(moving_platforms[moving_platform_count].end_y - moving_platforms[moving_platform_count].start_y);
                        moving_platforms[moving_platform_count].total_dist = sqrtf(dx * dx + dy * dy);
                        if (moving_platforms[moving_platform_count].total_dist > 0.1f) {
                            moving_platforms[moving_platform_count].dir_x = dx / moving_platforms[moving_platform_count].total_dist;
                            moving_platforms[moving_platform_count].dir_y = dy / moving_platforms[moving_platform_count].total_dist;
                        } else {
                            moving_platforms[moving_platform_count].dir_x = 1.0f;
                            moving_platforms[moving_platform_count].dir_y = 0.0f;
                        }
                    }
                    moving_platform_count++;
                    break;
                    
                case CELL_MOVING_START:
                    for (int m = 0; m < moving_platform_count; m++) {
                        if ((int)moving_platforms[m].start_x == 0 && (int)moving_platforms[m].start_y == 0) {
                            moving_platforms[m].start_x = px;
                            moving_platforms[m].start_y = py;
                            moving_platforms[m].current_x = (float)px;
                            moving_platforms[m].current_y = (float)py;
                            float dx = (float)(moving_platforms[m].end_x - moving_platforms[m].start_x);
                            float dy = (float)(moving_platforms[m].end_y - moving_platforms[m].start_y);
                            moving_platforms[m].total_dist = sqrtf(dx * dx + dy * dy);
                            if (moving_platforms[m].total_dist > 0.1f) {
                                moving_platforms[m].dir_x = dx / moving_platforms[m].total_dist;
                                moving_platforms[m].dir_y = dy / moving_platforms[m].total_dist;
                            }
                            break;
                        }
                    }
                    break;
                    
                case CELL_MOVING_END:
                    for (int m = 0; m < moving_platform_count; m++) {
                        if ((int)moving_platforms[m].end_x == 0 && (int)moving_platforms[m].end_y == 0) {
                            moving_platforms[m].end_x = px;
                            moving_platforms[m].end_y = py;
                            float dx = (float)(moving_platforms[m].end_x - moving_platforms[m].start_x);
                            float dy = (float)(moving_platforms[m].end_y - moving_platforms[m].start_y);
                            moving_platforms[m].total_dist = sqrtf(dx * dx + dy * dy);
                            if (moving_platforms[m].total_dist > 0.1f) {
                                moving_platforms[m].dir_x = dx / moving_platforms[m].total_dist;
                                moving_platforms[m].dir_y = dy / moving_platforms[m].total_dist;
                            }
                            break;
                        }
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
                    break;
                    
                case CELL_PLAYER_SPAWN:
                    player.x = (float)(px + CELL_SIZE / 2);
                    player.y = (float)(py + CELL_SIZE / 2);
                    respawn_x = player.x;
                    respawn_y = player.y;
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
}

// =============================================
// PLAYER INITIALIZATION
// =============================================
static void init_player(void) {
    player.vx = 0;
    player.vy = 0;
    player.radius = PLAYER_RADIUS;
    player.color = PLAYER_COLOR_NORMAL;
    player.is_dashing = 0;
    player.dash_cooldown_end = 0;
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
            if (now - mp->phase_start_time >= MOVING_PAUSE_MS) {
                mp->paused = 0;
                mp->moving_forward = 0;
                mp->phase_start_time = now;
            }
        } else if (mp->moving_forward) {
            float progress = (float)(now - mp->phase_start_time) / (float)MOVING_FORWARD_MS;
            if (progress >= 1.0f) {
                mp->current_x = (float)mp->end_x;
                mp->current_y = (float)mp->end_y;
                mp->paused = 1;
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
        if (player.x - player.radius < 0) { player.x = player.radius; player.dash_distance_remaining = 0; }
        if (player.x + player.radius > SCREEN_WIDTH) { player.x = SCREEN_WIDTH - player.radius; player.dash_distance_remaining = 0; }
        if (player.y - player.radius < 0) { player.y = player.radius; player.dash_distance_remaining = 0; }
        if (player.y + player.radius > SCREEN_HEIGHT) { player.y = SCREEN_HEIGHT - player.radius; player.dash_distance_remaining = 0; }
        if (player.dash_distance_remaining <= 0) {
            player.is_dashing = 0;
            player.color = PLAYER_COLOR_NORMAL;
            player.dash_cooldown_end = now + DASH_COOLDOWN_MS;
            player.vx = 0; player.vy = 0;
        }
        return;
    }
    
    // ---- WALL SLIDE ----
    int8_t wall = check_wall_slide();
    if (wall != 0 && !is_jumping && player.vy >= 0) {
        player.is_wall_sliding = 1;
        player.wall_side = wall;
        player.color = PLAYER_COLOR_WALL;
        player.vy = WALL_SLIDE_SPEED;
    } else {
        player.is_wall_sliding = 0;
        player.wall_side = 0;
        if (!player.is_dashing) player.color = PLAYER_COLOR_NORMAL;
    }
    
    // ---- PHYSICS ----
    if (!player.is_wall_sliding) {
        float joy_x = (float)joystick_data.coord_mapped.x;
        if (joy_x > 0.2f || joy_x < -0.2f) {
            player.vx = joy_x * MOVE_SPEED;
        } else {
            if (is_jumping) player.vx *= 0.95f;
            else player.vx *= 0.8f;
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
        float joy_x = (float)joystick_data.coord_mapped.x;
        if (joy_x > 0.2f || joy_x < -0.2f) player.vx = joy_x * MOVE_SPEED;
        else { player.vx *= 0.8f; if (fabs(player.vx) < 0.1f) player.vx = 0; }
    }
    
    player.x += player.vx;
    player.y += player.vy;
    
    // ---- BOUNDARIES ----
    if (player.x - player.radius < 0) { player.x = player.radius; player.vx = 0; }
    if (player.x + player.radius > SCREEN_WIDTH) { player.x = SCREEN_WIDTH - player.radius; player.vx = 0; }
    if (player.y + player.radius > SCREEN_HEIGHT) {
        player.y = SCREEN_HEIGHT - player.radius;
        player.vy = 0;
        is_jumping = 0;
        player.is_wall_sliding = 0;
        player.wall_side = 0;
    }
    if (player.y - player.radius < 0) { player.y = player.radius; player.vy = 0; }
    
    // ---- PLATFORM COLLISION (hitbox) ----
    uint8_t on_ground = 0;
    for (int i = 0; i < platform_count; i++) {
        if (!platforms[i].active) continue;
        if (!check_rect_collision(player.x, player.y, HITBOX_RADIUS, platforms[i].x, platforms[i].y, platforms[i].width, platforms[i].height)) continue;
        
        float prev_y = player.y - player.vy;
        float prev_x = player.x - player.vx;
        
        // Landing on top
        if (prev_y + HITBOX_RADIUS <= platforms[i].y && player.vy >= 0) {
            player.y = platforms[i].y - HITBOX_RADIUS;
            player.vy = 0;
            is_jumping = 0;
            on_ground = 1;
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
                platforms[i].active = 0;
            }
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
            on_ground = 1;
            standing_on_moving = i;
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
            // Bounce up!
            player.vy = -JUMP_VELOCITY_MAX;
            is_jumping = 1;
            jump_held = 0;
            player.dash_cooldown_end = 0;  // Restore dash
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
    if (!player_dying && goal.active) {
        if (check_rect_collision(player.x, player.y, HITBOX_RADIUS, goal.x, goal.y, goal.width, goal.height)) {
            // Next level!
            game_score += 500;
            if (current_level < 11) {
                load_level(current_level + 1);
                init_player();
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
            player.dash_cooldown_end = 0;  // Restore dash
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
        uint8_t color = platforms[i].is_falling ? COLOR_BLACK : COLOR_GRAY;
        // If falling and player is on it, flash red when about to fall
        if (platforms[i].is_falling && platforms[i].player_on) {
            uint32_t elapsed = HAL_GetTick() - platforms[i].step_on_time;
            if (elapsed > FALLING_DELAY_MS - 300) {
                color = COLOR_WHITE;  // Flash white before falling
            }
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
    
    // Dash cooldown indicator
    if (HAL_GetTick() < player.dash_cooldown_end) {
        LCD_Print_String(cfg0, 2, 22, "DASH:CD", COLOR_RED_BROWN, 0);
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
}

// =============================================
// INPUT HANDLING
// =============================================
static void handle_input(void) {
    uint32_t now = HAL_GetTick();
    
    // Read BT4 (jump) - PA8
    curr_btn4 = (uint8_t)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);
    
    // ---- JUMP (BT4) ----
    if (curr_btn4 == 0 && prev_btn4 == 1) {  // Falling edge (pressed)
        if (!player_dying && !game_over) {
            if (!is_jumping) {
                // Wall jump
                if (player.is_wall_sliding) {
                    player.vy = -JUMP_VELOCITY_MAX * 0.8f;
                    player.vx = -player.wall_side * MOVE_SPEED * 1.5f;
                    is_jumping = 1;
                    jump_held = 1;
                    jump_hold_start = now;
                    player.is_wall_sliding = 0;
                    player.wall_side = 0;
                    player.color = PLAYER_COLOR_NORMAL;
                } else {
                    // Normal jump
                    player.vy = -JUMP_VELOCITY_MAX;
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
                // Charge jump: parabolic curve
                float charge_t = (float)(now - jump_hold_start) / (float)MAX_JUMP_HOLD_MS;
                float extra_vy = -JUMP_VELOCITY_MAX * charge_t * 0.5f;
                player.vy = -JUMP_VELOCITY_MAX + extra_vy;
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
    if (!player_dying && !game_over && !player.is_dashing && now >= player.dash_cooldown_end) {
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_RESET) {
            // Get direction from joystick
            float dx = (float)joystick_data.coord_mapped.x;
            float dy = (float)joystick_data.coord_mapped.y;
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
        
        // Exit condition
        if (game_over) {
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8) == GPIO_PIN_RESET) {
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
