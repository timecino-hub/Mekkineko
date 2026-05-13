#include "Game_3.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

// ==============================================
// ZUMA GAME CONSTANTS
// ==============================================

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Ball settings
#define BALL_RADIUS   6
#define BALL_DIAMETER (BALL_RADIUS * 2)
#define MAX_BALLS     40
#define MAX_CHAIN_BALLS 60

// Path definition - a winding track from top to bottom
#define PATH_NUM_POINTS 12
#define BALL_SPEED_INIT 1
#define BALL_SPEED_MAX  2      // reduced from 4
#define SPEED_INCREASE_INTERVAL 500  // slower acceleration

// Shooter settings
#define SHOOTER_Y      215     // Y position of shooter
#define SHOOTER_MIN_X  20
#define SHOOTER_MAX_X  220
#define SHOOTER_SPEED  4       // Shooter movement speed

// Projectile settings
#define PROJECTILE_SPEED 14
#define MAX_PROJECTILES 3

// Match detection
#define MATCH_MIN 3  // Minimum balls in a row to eliminate

// Game states
typedef enum {
    GAME_PLAYING,
    GAME_OVER,
    GAME_WIN
} GameState;

// Level configuration
#define ZUMA_NUM_LEVELS 3
typedef struct {
    int total_balls;        // Balls to clear to win this level
    int ball_speed_start;   // Starting ball speed
} ZumaLevel;

static const ZumaLevel zuma_levels[ZUMA_NUM_LEVELS] = {
    { 15, 1 },  // Level 1: 15 balls
    { 20, 1 },  // Level 2: 20 balls
    { 25, 2 },  // Level 3: 25 balls, faster
};

// Ball colours (using LCD colour indices 1-6)
#define COLOUR_RED    2
#define COLOUR_GREEN  3
#define COLOUR_BLUE   4
#define COLOUR_ORANGE 5
#define COLOUR_YELLOW 6
#define COLOUR_PINK   7
#define NUM_COLOURS   6

// ==============================================
// PATH DEFINITION - Winding track
// ==============================================

// The path is defined as a series of waypoints that balls follow
static const int16_t path_points[PATH_NUM_POINTS][2] = {
    {120, 10},   // 0: Top center (start)
    {40,  35},   // 1: Curve left
    {30,  70},   // 2: Left side
    {80,  100},  // 3: Curve right
    {180, 90},   // 4: Right side
    {210, 130},  // 5: Bottom right
    {180, 165},  // 6: Curve left
    {120, 155},  // 7: Center
    {60,  175},  // 8: Left
    {40,  200},  // 9: Bottom left
    {80,  215},  // 10: Curve right toward bottom
    {120, 230},  // 11: End point (goal)
};

// ==============================================
// BALL STRUCTURES
// ==============================================

// A ball on the path
typedef struct {
    uint8_t active;         // 1 = active, 0 = inactive
    uint8_t colour;         // Colour index (2-7)
    uint8_t segment;        // Current path segment index
    float   progress;       // Progress along current segment (0.0 to 1.0)
    int16_t x, y;           // Current pixel position (computed)
} PathBall;

// A projectile ball fired by the player
typedef struct {
    uint8_t active;
    uint8_t colour;
    int16_t x, y;
    float   vx, vy;
    int16_t target_x, target_y;
    uint8_t life;           // frame counter, auto-expire at 60
} Projectile;

// ==============================================
// GAME STATE
// ==============================================

static GameState game_state;
static uint32_t frame_counter;
static int32_t score;
static int32_t lives;
static int32_t ball_speed;
static int current_zuma_level;

// Shooter
static int16_t shooter_x;
static uint8_t shooter_colour;
static uint8_t next_colour;

// Path balls
static PathBall path_balls[MAX_BALLS];
static int32_t num_path_balls;

// Projectiles
static Projectile projectiles[MAX_PROJECTILES];

// Spawn control
static int32_t spawn_timer;
static int32_t balls_spawned;      // Balls spawned so far this level
static int32_t balls_to_spawn;     // Total balls for this level
#define SPAWN_INTERVAL_INIT 60   // Frames between new ball spawns
#define SPAWN_INTERVAL_MIN 25
static int32_t spawn_interval;

// Chain reaction tracking
static int32_t chain_combo;
static int32_t chain_timer;
#define CHAIN_DISPLAY_TIME 30

// ==============================================
// HELPER: Get position on path at a given segment/progress
// ==============================================

static void get_path_position(uint8_t segment, float progress, int16_t* out_x, int16_t* out_y) {
    if (segment >= PATH_NUM_POINTS - 1) {
        *out_x = path_points[PATH_NUM_POINTS - 1][0];
        *out_y = path_points[PATH_NUM_POINTS - 1][1];
        return;
    }
    
    int16_t x0 = path_points[segment][0];
    int16_t y0 = path_points[segment][1];
    int16_t x1 = path_points[segment + 1][0];
    int16_t y1 = path_points[segment + 1][1];
    
    *out_x = x0 + (int16_t)((x1 - x0) * progress);
    *out_y = y0 + (int16_t)((y1 - y0) * progress);
}

// ==============================================
// HELPER: Get total path length (in segments)
// ==============================================

static float get_total_path_progress(uint8_t segment, float progress) {
    return (float)segment + progress;
}

// ==============================================
// HELPER: Get a random colour
// ==============================================

static uint8_t random_colour(void) {
    // 70% chance: pick a color already on the path (bias toward same-color chains)
    if (num_path_balls > 0 && (rand() % 100) < 70) {
        int idx = num_path_balls - 1 - (rand() % 3);
        if (idx < 0) idx = 0;
        for (int i = idx; i >= 0; i--) {
            if (path_balls[i].active) return path_balls[i].colour;
        }
    }
    return (rand() % NUM_COLOURS) + 2;
}

// Pure random for shooter balls (no path bias)
static uint8_t random_colour_pure(void) {
    return (rand() % NUM_COLOURS) + 2;
}

// ==============================================
// INITIALIZATION
// ==============================================

static void game_init(void) {
    game_state = GAME_PLAYING;
    frame_counter = 0;
    score = 0;
    lives = 3;
    current_zuma_level = 0;
    balls_to_spawn = zuma_levels[0].total_balls;
    balls_spawned = 0;
    ball_speed = zuma_levels[0].ball_speed_start;
    spawn_interval = SPAWN_INTERVAL_INIT;
    spawn_timer = 0;
    num_path_balls = 0;
    chain_combo = 0;
    chain_timer = 0;

    // Clear all path balls
    for (int i = 0; i < MAX_BALLS; i++) {
        path_balls[i].active = 0;
    }

    // Clear all projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = 0;
    }

    // Initialize shooter
    shooter_x = SCREEN_WIDTH / 2;
    shooter_colour = random_colour();
    next_colour = random_colour();

    // Spawn initial balls on the path
    for (int i = 0; i < 6; i++) {
        if (num_path_balls < MAX_BALLS) {
            PathBall* ball = &path_balls[num_path_balls];
            ball->active = 1;
            ball->colour = random_colour();
            ball->segment = 0;
            ball->progress = (float)i * 0.15f;
            if (ball->progress > 1.0f) {
                ball->segment = 1;
                ball->progress = ball->progress - 1.0f;
            }
            get_path_position(ball->segment, ball->progress, &ball->x, &ball->y);
            num_path_balls++;
            balls_spawned++;
        }
    }

    // Play startup sound
    buzzer_tone(&buzzer_cfg, 523, 20);  // C5
    HAL_Delay(40);
    buzzer_tone(&buzzer_cfg, 659, 20);  // E5
    HAL_Delay(40);
    buzzer_tone(&buzzer_cfg, 784, 20);  // G5
    HAL_Delay(40);
    buzzer_off(&buzzer_cfg);
}

static void zuma_next_level(void) {
    current_zuma_level++;
    if (current_zuma_level >= ZUMA_NUM_LEVELS) {
        game_state = GAME_WIN;
        return;
    }

    const ZumaLevel* lv = &zuma_levels[current_zuma_level];
    balls_to_spawn = lv->total_balls;
    balls_spawned = 0;
    ball_speed = lv->ball_speed_start;
    spawn_interval = SPAWN_INTERVAL_INIT;
    spawn_timer = 0;
    num_path_balls = 0;
    chain_combo = 0;
    chain_timer = 0;

    // Clear all path balls
    for (int i = 0; i < MAX_BALLS; i++) {
        path_balls[i].active = 0;
    }

    // Clear all projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = 0;
    }

    // Spawn initial balls for the new level
    for (int i = 0; i < 6; i++) {
        if (num_path_balls < MAX_BALLS) {
            PathBall* ball = &path_balls[num_path_balls];
            ball->active = 1;
            ball->colour = random_colour();
            ball->segment = 0;
            ball->progress = (float)i * 0.15f;
            if (ball->progress > 1.0f) {
                ball->segment = 1;
                ball->progress = ball->progress - 1.0f;
            }
            get_path_position(ball->segment, ball->progress, &ball->x, &ball->y);
            num_path_balls++;
            balls_spawned++;
        }
    }

    // Level transition fanfare
    buzzer_tone(&buzzer_cfg, 784, 20);  // G5
    HAL_Delay(50);
    buzzer_tone(&buzzer_cfg, 1047, 20); // C6
    HAL_Delay(50);
    buzzer_off(&buzzer_cfg);
}

// ==============================================
// SPAWN A NEW BALL ON THE PATH
// ==============================================

static void spawn_ball(void) {
    if (num_path_balls >= MAX_BALLS) return;
    if (balls_spawned >= balls_to_spawn) return;  // Level ball limit reached

    PathBall* ball = &path_balls[num_path_balls];
    ball->active = 1;
    ball->colour = random_colour();
    ball->segment = 0;
    ball->progress = 0.0f;
    get_path_position(0, 0.0f, &ball->x, &ball->y);
    num_path_balls++;
    balls_spawned++;
}

// ==============================================
// CHECK IF A BALL REACHED THE END
// ==============================================

static int check_ball_at_end(PathBall* ball) {
    return (ball->segment >= PATH_NUM_POINTS - 2 && ball->progress >= 1.0f);
}

// ==============================================
// FIND BALL AT A GIVEN POSITION (for collision detection)
// ==============================================

static int find_ball_at(int16_t x, int16_t y, int ignore_index) {
    for (int i = 0; i < num_path_balls; i++) {
        if (!path_balls[i].active || i == ignore_index) continue;
        int16_t dx = path_balls[i].x - x;
        int16_t dy = path_balls[i].y - y;
        int16_t dist_sq = dx * dx + dy * dy;
        if (dist_sq < (BALL_DIAMETER * BALL_DIAMETER)) {
            return i;
        }
    }
    return -1;
}

// ==============================================
// INSERT A BALL INTO THE PATH (after projectile hit)
// ==============================================

static int insert_ball_into_path(int insert_after, uint8_t colour) {
    if (num_path_balls >= MAX_BALLS) return -1;
    
    // Shift all balls after insert_after to make room
    for (int i = num_path_balls; i > insert_after + 1; i--) {
        path_balls[i] = path_balls[i - 1];
    }
    
    // Insert new ball
    PathBall* new_ball = &path_balls[insert_after + 1];
    PathBall* prev = &path_balls[insert_after];
    new_ball->active = 1;
    new_ball->colour = colour;
    new_ball->segment = prev->segment;
    new_ball->progress = prev->progress + 0.08f;
    
    // Handle segment overflow
    if (new_ball->progress >= 1.0f) {
        new_ball->segment++;
        new_ball->progress -= 1.0f;
        if (new_ball->segment >= PATH_NUM_POINTS - 1) {
            new_ball->segment = PATH_NUM_POINTS - 2;
            new_ball->progress = 0.99f;
        }
    }
    
    get_path_position(new_ball->segment, new_ball->progress, &new_ball->x, &new_ball->y);
    num_path_balls++;
    
    return insert_after + 1;
}

// ==============================================
// CHECK AND CLEAR MATCHES (3+ same colour in a row)
// ==============================================

static int check_matches(int start_index) {
    if (num_path_balls < 3) return 0;

    int cleared = 0;
    int found_match;
    int safety = 0;

    do {
        if (++safety > 100) break;  // Safety valve: prevent infinite loop
        found_match = 0;
        
        // Scan for matches
        for (int i = 0; i < num_path_balls; i++) {
            if (!path_balls[i].active) continue;
            
            uint8_t c = path_balls[i].colour;
            int count = 1;
            
            // Count forward
            for (int j = i + 1; j < num_path_balls; j++) {
                if (!path_balls[j].active) break;
                if (path_balls[j].colour == c) count++;
                else break;
            }
            
            // Count backward
            for (int j = i - 1; j >= 0; j--) {
                if (!path_balls[j].active) break;
                if (path_balls[j].colour == c) count++;
                else break;
            }
            
            if (count >= MATCH_MIN) {
                // Found a match! Clear these balls
                found_match = 1;
                
                // Mark all matching balls as inactive
                // First find the start of the match
                int match_start = i;
                while (match_start > 0 && path_balls[match_start - 1].active && 
                       path_balls[match_start - 1].colour == c) {
                    match_start--;
                }
                
                int match_end = i;
                while (match_end < num_path_balls - 1 && path_balls[match_end + 1].active && 
                       path_balls[match_end + 1].colour == c) {
                    match_end++;
                }
                
                // Deactivate matched balls
                for (int j = match_start; j <= match_end; j++) {
                    path_balls[j].active = 0;
                }
                
                // Calculate score with combo multiplier
                int cleared_count = match_end - match_start + 1;
                int points = cleared_count * 10 * (chain_combo + 1);
                score += points;
                cleared += cleared_count;
                
                // Chain combo
                chain_combo++;
                chain_timer = CHAIN_DISPLAY_TIME;
                
                // Play match sound
                buzzer_tone(&buzzer_cfg, 880 + (chain_combo * 100), 15);
                HAL_Delay(20);
                buzzer_off(&buzzer_cfg);
                
                // Compact the array (remove inactive balls)
                int write_idx = 0;
                for (int read_idx = 0; read_idx < num_path_balls; read_idx++) {
                    if (path_balls[read_idx].active) {
                        if (write_idx != read_idx) {
                            path_balls[write_idx] = path_balls[read_idx];
                        }
                        write_idx++;
                    }
                }
                num_path_balls = write_idx;
                
                // LED flash on match
                PWM_SetDuty(&pwm_cfg, 100);
                HAL_Delay(10);
                PWM_SetDuty(&pwm_cfg, 50);
                
                break;  // Restart scan after compaction
            }
        }
    } while (found_match);
    
    return cleared;
}

// ==============================================
// UPDATE PATH BALLS - Move them along the path
// ==============================================

static void update_path_balls(void) {
    uint8_t ball_reached_end = 0;

    for (int i = 0; i < num_path_balls; i++) {
        if (!path_balls[i].active) continue;

        PathBall* ball = &path_balls[i];

        // Move forward along the path
        ball->progress += (float)ball_speed * 0.01f;

        if (ball->progress >= 1.0f) {
            ball->progress -= 1.0f;
            ball->segment++;

            if (ball->segment >= PATH_NUM_POINTS - 1) {
                // Ball reached the end — mark and break out immediately
                ball->segment = PATH_NUM_POINTS - 2;
                ball->progress = 1.0f;
                lives--;
                ball_reached_end = 1;
                break;
            }
        }

        // Update pixel position
        get_path_position(ball->segment, ball->progress, &ball->x, &ball->y);
    }

    // Handle end-of-path outside the loop (safe, no loop-bound modification)
    if (ball_reached_end) {
        buzzer_tone(&buzzer_cfg, 200, 30);
        HAL_Delay(100);
        buzzer_off(&buzzer_cfg);

        num_path_balls = 0;

        if (lives <= 0) {
            game_state = GAME_OVER;
        }
    }
}

// ==============================================
// UPDATE PROJECTILES
// ==============================================

static void update_projectiles(void) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;

        Projectile* p = &projectiles[i];

        p->x += (int16_t)p->vx;
        p->y += (int16_t)p->vy;
        p->life++;

        // Kill if too old or off screen
        if (p->life > 60 || p->x < -20 || p->x > SCREEN_WIDTH + 20 ||
            p->y < -20 || p->y > SCREEN_HEIGHT + 20) {
            p->active = 0;
            continue;
        }

        // Collision with nearest path ball (tighter: BALL_RADIUS * 2)
        int hit_idx = -1;
        int16_t best = BALL_RADIUS * 2;
        for (int j = 0; j < num_path_balls; j++) {
            if (!path_balls[j].active) continue;
            int16_t dx = path_balls[j].x - p->x;
            int16_t dy = path_balls[j].y - p->y;
            int16_t dist = (int16_t)sqrtf((float)(dx * dx + dy * dy));
            if (dist < best) {
                hit_idx = j;
                best = dist;
            }
        }

        if (hit_idx >= 0) {
            int insert_idx = insert_ball_into_path(hit_idx, p->colour);
            p->active = 0;

            buzzer_tone(&buzzer_cfg, 600, 15);
            HAL_Delay(15);
            buzzer_off(&buzzer_cfg);

            if (insert_idx >= 0) {
                check_matches(insert_idx);
            }
        }
    }
}

// ==============================================
// FIRE PROJECTILE
// ==============================================

static void fire_projectile(void) {
    // Find an inactive projectile slot
    int slot = -1;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;  // No available slots

    // Find target ball and predict its future position
    int16_t target_x = SCREEN_WIDTH / 2;
    int16_t target_y = 50;

    for (int i = 0; i < num_path_balls; i++) {
        if (path_balls[i].active) {
            target_x = path_balls[i].x;
            target_y = path_balls[i].y;

            // Predict where ball will be when projectile arrives
            float dx0 = (float)(target_x - shooter_x);
            float dy0 = (float)(target_y - SHOOTER_Y);
            float dist0 = sqrtf(dx0 * dx0 + dy0 * dy0);
            float flight_frames = dist0 / PROJECTILE_SPEED;
            float advance = flight_frames * (float)ball_speed * 0.01f;

            // Advance target position along path
            uint8_t seg = path_balls[i].segment;
            float prog = path_balls[i].progress + advance;
            while (prog >= 1.0f && seg < PATH_NUM_POINTS - 2) {
                prog -= 1.0f;
                seg++;
            }
            get_path_position(seg, prog, &target_x, &target_y);
            break;
        }
    }

    // Calculate direction from shooter to predicted target
    float dx = (float)(target_x - shooter_x);
    float dy = (float)(target_y - SHOOTER_Y);
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 1.0f) dist = 1.0f;
    
    Projectile* p = &projectiles[slot];
    p->active = 1;
    p->colour = shooter_colour;
    p->x = shooter_x;
    p->y = SHOOTER_Y - 20;
    p->vx = (dx / dist) * PROJECTILE_SPEED;
    p->vy = (dy / dist) * PROJECTILE_SPEED;
    p->target_x = target_x;
    p->target_y = target_y;
    p->life = 0;
    
    // Get next colour for shooter
    shooter_colour = next_colour;
    next_colour = random_colour_pure();

    // Play fire sound
    buzzer_tone(&buzzer_cfg, 440, 10);
    HAL_Delay(10);
    buzzer_off(&buzzer_cfg);
}

// ==============================================
// UPDATE GAME LOGIC
// ==============================================

static void update_game(void) {
    if (game_state != GAME_PLAYING) return;
    
    frame_counter++;
    
    // Read input
    Input_Read();
    
    // Handle shooter movement (joystick left/right via buttons)
    // BT2 = move left, BT3 = fire (also used for menu return)
    // We'll use a different approach: auto-move or use available inputs
    
    // For simplicity, use BT2 to fire (since BT3 returns to menu)
    // Shooter auto-oscillates or we can use joystick
    
    // Move shooter with joystick
    Joystick_Read(&joystick_cfg, &joystick_data);
    
    Direction dir = joystick_data.direction;
    
    if (dir == E || dir == NE || dir == SE) {
        shooter_x -= SHOOTER_SPEED;
        if (shooter_x < SHOOTER_MIN_X) shooter_x = SHOOTER_MIN_X;
    } else if (dir == W || dir == NW || dir == SW) {
        shooter_x += SHOOTER_SPEED;
        if (shooter_x > SHOOTER_MAX_X) shooter_x = SHOOTER_MAX_X;
    }
    
    // Update projectiles first (move existing ones)
    update_projectiles();

    // Update path balls
    update_path_balls();

    // Joystick down: discard current shooter ball (edge-triggered)
    {
        static Direction last_dir_down = CENTRE;
        if (dir == S || dir == SE || dir == SW) {
            if (last_dir_down != S && last_dir_down != SE && last_dir_down != SW) {
                shooter_colour = next_colour;
                next_colour = random_colour_pure();
            }
        }
        if (dir != S && dir != SE && dir != SW) {
            last_dir_down = CENTRE;
        } else {
            last_dir_down = dir;
        }
    }

    // Fire with B1 (blue button) or BT2 (after updates, so new projectile shows for full frame)
    if (current_input.btn1_pressed || current_input.btn2_pressed) {
        fire_projectile();
    }

    // Return to menu with BT3
    if (current_input.btn3_pressed) {
        game_state = GAME_OVER;
        return;
    }
    
    // Spawn new balls periodically
    spawn_timer++;
    if (spawn_timer >= spawn_interval) {
        spawn_timer = 0;
        spawn_ball();
    }
    
    // Increase difficulty over time
    if (frame_counter % SPEED_INCREASE_INTERVAL == 0) {
        if (ball_speed < BALL_SPEED_MAX) {
            ball_speed++;
        }
        if (spawn_interval > SPAWN_INTERVAL_MIN) {
            spawn_interval -= 2;
        }
    }
    
    // Update chain combo timer
    if (chain_timer > 0) {
        chain_timer--;
        if (chain_timer == 0) {
            chain_combo = 0;
        }
    }
    
    // Check win condition: all balls spawned, none left on path
    if (balls_spawned >= balls_to_spawn && num_path_balls == 0) {
        score += 500;  // Level completion bonus (like Game 2)
        zuma_next_level();  // Advance to next level or set GAME_WIN
    }
}

// ==============================================
// DRAW A BALL ON THE LCD
// ==============================================

static void draw_ball(int16_t x, int16_t y, uint8_t colour, uint8_t size) {
    // Draw a filled circle using the LCD_Draw_Circle function
    // Since we have LCD_Draw_Circle with fill parameter
    LCD_Draw_Circle(x, y, size, colour, 1);  // 1 = filled
    // Draw outline
    LCD_Draw_Circle(x, y, size, 1, 0);  // 0 = outline, colour 1 = white
}

// ==============================================
// DRAW THE PATH
// ==============================================

static void draw_path(void) {
    // Draw lines connecting path waypoints
    for (int i = 0; i < PATH_NUM_POINTS - 1; i++) {
        LCD_Draw_Line(
            path_points[i][0], path_points[i][1],
            path_points[i + 1][0], path_points[i + 1][1],
            13  // Grey colour
        );
    }
}

// ==============================================
// RENDER GAME
// ==============================================

static void render_game(void) {
    LCD_Fill_Buffer(0);  // Clear screen (black)
    
    // Draw the path
    draw_path();
    
    // Draw path balls
    for (int i = 0; i < num_path_balls; i++) {
        if (!path_balls[i].active) continue;
        draw_ball(path_balls[i].x, path_balls[i].y, path_balls[i].colour, BALL_RADIUS);
    }
    
    // Draw projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        draw_ball(projectiles[i].x, projectiles[i].y, projectiles[i].colour, 4);
    }
    
    // Draw shooter
    LCD_Draw_Rect(shooter_x - 12, SHOOTER_Y - 6, 24, 12, 1, 0);  // White outline
    LCD_Draw_Rect(shooter_x - 10, SHOOTER_Y - 4, 20, 8, shooter_colour, 1);  // Filled with current colour
    
    // Draw next colour indicator (top-right corner)
    LCD_printString("Next:", 185, 20, 1, 1);
    draw_ball(210, 26, next_colour, 5);
    
    // Draw level and score
    char info_str[32];
    sprintf(info_str, "L%d S:%ld", current_zuma_level + 1, score);
    LCD_printString(info_str, 5, 5, 1, 1);

    // Draw remaining balls
    int remaining = balls_to_spawn - balls_spawned + num_path_balls;
    char remain_str[16];
    sprintf(remain_str, "Left:%d", remaining > 0 ? remaining : 0);
    LCD_printString(remain_str, 5, 17, 1, 1);

    // Draw lives
    char lives_str[12];
    sprintf(lives_str, "Lives: %ld", lives);
    LCD_printString(lives_str, 170, 5, 1, 1);

    // Draw chain combo if active
    if (chain_combo > 0) {
        char combo_str[16];
        sprintf(combo_str, "x%d Combo!", chain_combo);
        LCD_printString(combo_str, 80, 30, 2, 2);
    }
    
    // Draw instructions
    LCD_printString("Joy:L/R B1:Fire BT3:Exit", 25, 232, 1, 1);
    
    LCD_Refresh(&cfg0);
}

// ==============================================
// RENDER GAME OVER SCREEN
// ==============================================

static void render_game_over(void) {
    LCD_Fill_Buffer(0);

    LCD_printString("GAME OVER", 60, 80, 2, 3);

    char score_str[24];
    sprintf(score_str, "Final Score: %ld", score);
    LCD_printString(score_str, 50, 130, 1, 2);

    LCD_printString("Press BT3", 70, 190, 1, 2);
    LCD_printString("to continue", 60, 210, 1, 2);

    LCD_Refresh(&cfg0);
}

// ==============================================
// RENDER GAME WIN SCREEN (like Game 2's "YOU WIN!")
// ==============================================

static void render_game_win(void) {
    LCD_Fill_Buffer(0);

    LCD_printString("YOU WIN!", 70, 80, 2, 3);

    char score_str[32];
    sprintf(score_str, "Score: %ld", score);
    LCD_printString(score_str, 70, 130, 1, 2);

    char level_str[24];
    sprintf(level_str, "All %d levels", ZUMA_NUM_LEVELS);
    LCD_printString(level_str, 60, 155, 1, 2);
    LCD_printString("cleared!", 80, 175, 1, 2);

    LCD_printString("Press BT3", 70, 210, 1, 2);
    LCD_printString("to exit", 80, 230, 1, 2);

    LCD_Refresh(&cfg0);
}

// ==============================================
// MAIN GAME LOOP
// ==============================================

MenuState Game3_Run(void) {
    // Initialize game
    game_init();
    
    MenuState exit_state = MENU_STATE_HOME;
    
    // Game loop
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        if (game_state == GAME_PLAYING) {
            // Update game logic
            update_game();

            // Render
            render_game();
        } else if (game_state == GAME_OVER) {
            // Show game over screen
            render_game_over();

            // Wait for button press to exit
            Input_Read();
            if (current_input.btn3_pressed) {
                PWM_SetDuty(&pwm_cfg, 50);
                exit_state = MENU_STATE_HOME;
                break;
            }
        } else if (game_state == GAME_WIN) {
            // Show win screen (like Game 2's "YOU WIN!")
            render_game_win();

            // Wait for button press to exit
            Input_Read();
            if (current_input.btn3_pressed) {
                PWM_SetDuty(&pwm_cfg, 50);
                exit_state = MENU_STATE_HOME;
                break;
            }
        }
        
        // Frame timing
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < 30) {
            HAL_Delay(30 - frame_time);
        }
    }
    
    return exit_state;
}
