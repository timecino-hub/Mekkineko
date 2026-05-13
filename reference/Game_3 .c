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

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;      // LED PWM control
extern Buzzer_cfg_t buzzer_cfg; // Buzzer control

// ==============================================
// ZUMA GAME CONSTANTS
// ==============================================

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 280

// Ball settings
#define BALL_RADIUS   6
#define BALL_DIAMETER (BALL_RADIUS * 2)
#define MAX_BALLS     40
#define MAX_CHAIN_BALLS 60

// Path definition - a winding track from top to bottom
#define PATH_NUM_POINTS 12
#define BALL_SPEED_INIT 1      // Initial ball movement speed (pixels per frame)
#define BALL_SPEED_MAX  4      // Maximum speed
#define SPEED_INCREASE_INTERVAL 300  // Frames between speed increases

// Shooter settings
#define SHOOTER_Y      250     // Y position of shooter
#define SHOOTER_MIN_X  20
#define SHOOTER_MAX_X  220
#define SHOOTER_SPEED  4       // Shooter movement speed

// Projectile settings
#define PROJECTILE_SPEED 8
#define MAX_PROJECTILES 3

// Match detection
#define MATCH_MIN 3  // Minimum balls in a row to eliminate

// Game states
typedef enum {
    GAME_PLAYING,
    GAME_OVER,
    GAME_WIN
} GameState;

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
    {40,  40},   // 1: Curve left
    {30,  80},   // 2: Left side
    {80,  110},  // 3: Curve right
    {180, 100},  // 4: Right side
    {210, 140},  // 5: Bottom right
    {180, 180},  // 6: Curve left
    {120, 170},  // 7: Center
    {60,  190},  // 8: Left
    {40,  220},  // 9: Bottom left
    {80,  240},  // 10: Curve right toward bottom
    {120, 260},  // 11: End point (goal)
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
    float   vx, vy;         // Velocity components
    int16_t target_x, target_y; // Where it was aimed
} Projectile;

// ==============================================
// GAME STATE
// ==============================================

static GameState game_state;
static uint32_t frame_counter;
static int32_t score;
static int32_t lives;
static int32_t ball_speed;

// Shooter
static int16_t shooter_x;
static uint8_t shooter_colour;
static uint8_t next_colour;

// Path balls
static PathBall path_balls[MAX_BALLS];
static int32_t num_path_balls;

// Projectiles
static Projectile projectiles[MAX_PROJECTILES];

// Spawn timer
static int32_t spawn_timer;
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
    return (rand() % NUM_COLOURS) + 2;  // Returns 2-7
}

// ==============================================
// INITIALIZATION
// ==============================================

static void game_init(void) {
    game_state = GAME_PLAYING;
    frame_counter = 0;
    score = 0;
    lives = 3;
    ball_speed = BALL_SPEED_INIT;
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

// ==============================================
// SPAWN A NEW BALL ON THE PATH
// ==============================================

static void spawn_ball(void) {
    if (num_path_balls >= MAX_BALLS) return;
    
    PathBall* ball = &path_balls[num_path_balls];
    ball->active = 1;
    ball->colour = random_colour();
    ball->segment = 0;
    ball->progress = 0.0f;
    get_path_position(0, 0.0f, &ball->x, &ball->y);
    num_path_balls++;
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
    
    do {
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
    for (int i = 0; i < num_path_balls; i++) {
        if (!path_balls[i].active) continue;
        
        PathBall* ball = &path_balls[i];
        
        // Move forward along the path
        ball->progress += (float)ball_speed * 0.01f;
        
        if (ball->progress >= 1.0f) {
            ball->progress -= 1.0f;
            ball->segment++;
            
            if (ball->segment >= PATH_NUM_POINTS - 1) {
                // Ball reached the end!
                ball->segment = PATH_NUM_POINTS - 2;
                ball->progress = 1.0f;
                
                // Lose a life
                lives--;
                buzzer_tone(&buzzer_cfg, 200, 30);
                HAL_Delay(100);
                buzzer_off(&buzzer_cfg);
                
                // Remove this ball
                ball->active = 0;
                
                // Compact array
                for (int j = i; j < num_path_balls - 1; j++) {
                    path_balls[j] = path_balls[j + 1];
                }
                num_path_balls--;
                i--;  // Re-check this index
                
                if (lives <= 0) {
                    game_state = GAME_OVER;
                }
                continue;
            }
        }
        
        // Update pixel position
        get_path_position(ball->segment, ball->progress, &ball->x, &ball->y);
    }
}

// ==============================================
// UPDATE PROJECTILES
// ==============================================

static void update_projectiles(void) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        
        Projectile* p = &projectiles[i];
        
        // Move projectile
        p->x += (int16_t)p->vx;
        p->y += (int16_t)p->vy;
        
        // Check if projectile is off screen
        if (p->x < 0 || p->x > SCREEN_WIDTH || p->y < 0 || p->y < 20) {
            p->active = 0;
            continue;
        }
        
        // Check collision with path balls
        int hit_idx = -1;
        for (int j = 0; j < num_path_balls; j++) {
            if (!path_balls[j].active) continue;
            int16_t dx = path_balls[j].x - p->x;
            int16_t dy = path_balls[j].y - p->y;
            int16_t dist_sq = dx * dx + dy * dy;
            if (dist_sq < (BALL_DIAMETER * BALL_DIAMETER)) {
                hit_idx = j;
                break;
            }
        }
        
        if (hit_idx >= 0) {
            // Insert the projectile's colour into the path
            int insert_idx = insert_ball_into_path(hit_idx, p->colour);
            p->active = 0;
            
            // Play hit sound
            buzzer_tone(&buzzer_cfg, 600, 15);
            HAL_Delay(15);
            buzzer_off(&buzzer_cfg);
            
            // Check for matches
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
    
    // Find the first ball on the path to aim at
    int target_idx = -1;
    int16_t target_x = SCREEN_WIDTH / 2;
    int16_t target_y = 50;
    
    for (int i = 0; i < num_path_balls; i++) {
        if (path_balls[i].active) {
            target_idx = i;
            target_x = path_balls[i].x;
            target_y = path_balls[i].y;
            break;
        }
    }
    
    // Calculate direction from shooter to target
    float dx = (float)(target_x - shooter_x);
    float dy = (float)(target_y - SHOOTER_Y);
    float dist = sqrtf(dx * dx + dy * dy);
    
    if (dist < 1.0f) dist = 1.0f;
    
    Projectile* p = &projectiles[slot];
    p->active = 1;
    p->colour = shooter_colour;
    p->x = shooter_x;
    p->y = SHOOTER_Y - 10;
    p->vx = (dx / dist) * PROJECTILE_SPEED;
    p->vy = (dy / dist) * PROJECTILE_SPEED;
    p->target_x = target_x;
    p->target_y = target_y;
    
    // Get next colour for shooter
    shooter_colour = next_colour;
    next_colour = random_colour();
    
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
    
    // Move shooter with joystick if available
    extern Joystick_t joystick_data;
    extern Joystick_cfg_t joystick_cfg;
    Joystick_Read(&joystick_cfg, &joystick_data);
    
    Direction dir = joystick_data.direction;
    
    if (dir == W || dir == NW || dir == SW) {
        shooter_x -= SHOOTER_SPEED;
        if (shooter_x < SHOOTER_MIN_X) shooter_x = SHOOTER_MIN_X;
    } else if (dir == E || dir == NE || dir == SE) {
        shooter_x += SHOOTER_SPEED;
        if (shooter_x > SHOOTER_MAX_X) shooter_x = SHOOTER_MAX_X;
    }
    
    // Fire with BT2
    if (current_input.btn2_pressed) {
        fire_projectile();
    }
    
    // Return to menu with BT3
    if (current_input.btn3_pressed) {
        game_state = GAME_OVER;  // Will cause exit
        return;
    }
    
    // Update projectiles
    update_projectiles();
    
    // Update path balls
    update_path_balls();
    
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
    
    // Check win condition (cleared all balls and no more to spawn)
    // For this version, game continues indefinitely with increasing difficulty
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
    
    // Draw next colour indicator
    LCD_printString("Next:", shooter_x - 20, SHOOTER_Y + 14, 1, 1);
    draw_ball(shooter_x + 10, SHOOTER_Y + 20, next_colour, 4);
    
    // Draw score
    char score_str[24];
    sprintf(score_str, "Score: %ld", score);
    LCD_printString(score_str, 5, 5, 1, 1);
    
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
    LCD_printString("Joy:L/R  BT2:Fire", 40, 270, 1, 1);
    
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
        }
        
        // Frame timing
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < 30) {
            HAL_Delay(30 - frame_time);
        }
    }
    
    return exit_state;
}
