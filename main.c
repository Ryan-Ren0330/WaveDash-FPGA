#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//=========================== Hardware Address Macros ==========================//
// Media Processing / Audio Interface
#define AUDIO_BASE 0xFF203040
// Hardware Timer (Interval Timer)
#define TIMER_BASE 0xFF202000
#define TIMER_STATUS (TIMER_BASE + 0x0)    // Status Register
#define TIMER_CONTROL (TIMER_BASE + 0x4)   // Control Register
#define TIMER_START_LO (TIMER_BASE + 0x8)  // Start Count (Low 16-bits)
#define TIMER_START_HI (TIMER_BASE + 0xC)  // Start Count (High 16-bits)

//------------------ Screen / Color Macros ------------------//
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define BLOCK_SIZE 2
#define WHITE 0xFFFF
#define BLACK 0x0000
#define RED 0xF800
#define GREEN 0x07E0
#define PINK 0x1FF8
#define YELLOW 0xFFE0
#define BLUE 0x001F
#define ORANGE 0xFD20

//=========================== Parameter Settings ===========================//
#define THRESHOLD 900000000 //for audio detection
#define TIMER_0_5_SEC_HI 0x02FA
#define TIMER_0_5_SEC_LO 0xF080


#define HEX3_HEX0_BASE 0xFF200020
const unsigned int seg7[10] = {
   0x3F,  // 0
   0x06,  // 1
   0x5B,  // 2
   0x4F,  // 3
   0x66,  // 4
   0x6D,  // 5
   0x7D,  // 6
   0x07,  // 7
   0x7F,  // 8
   0x6F   // 9
};


#define LED_REG ((volatile int *)0xFF2000F0)

//=========================== Global Variables ===========================//
volatile int pixel_buffer_start;
short int Buffer1[240][512];
short int Buffer2[240][512];

int time_hundredths = 0;
int rounds = 0;         
int frameCount = 0;
int score = 0;
int speed_factor = 1;


enum GameState { RUNNING, GAME_OVER, PAUSED };
enum GameState game_state = RUNNING;

//--------------------- Simple / Difficult Mode -------------------------//
// =1 means simple mode, =0 hard mode
int simple_mode = 0;

//=========================== Obstacle Related ===========================//
typedef struct {
 int x, y;
 int width, height;
 short int color;
 int active;
} Obstacle;

typedef struct {
 int x, y;
 int width, height;
 short int color;
 int active;
 int type;  // 0: slow down, 1: get score, 2: boost and get three scores
} Collectible;
Collectible collectible[3]; 

#define MAX_OBSTACLES 200
Obstacle obstacles[MAX_OBSTACLES];
int num_obstacles = 0;
int obstacle_spawn_counter = 0;
int obstacle_spawn_interval = 30;

//=========================== Audio Interface Structure ===========================//
typedef struct {
 volatile unsigned int control;
 volatile unsigned char rarc;
 volatile unsigned char ralc;
 volatile unsigned char wsrc;
 volatile unsigned char wslc;
 volatile unsigned int ldata;
 volatile unsigned int rdata;
} audio_t;

//=========================== Data Structures ===========================//
typedef struct {
 int x, y;
 short int color;
} Block;
#define MAX_POINTS 100
Block turning_points[MAX_POINTS];
int num_points = 0;

//=========================== Background & Particle Effects ===========================//
unsigned short bg_color = BLACK;
int bg_timer = 0; 

short int get_random_color() {
    return (short int)(rand() & 0xFFFF);
}
//the particle displayed when the audio is detected and the block turns
typedef struct {
 int x, y;    
 int vx, vy; 
 int life;  
} Particle;
#define MAX_PARTICLES 50
Particle particles[MAX_PARTICLES];
int num_particles = 0;

void spawn_particles(int x, int y, int count) {
 for (int i = 0; i < count; i++) {
   if (num_particles < MAX_PARTICLES) {
     particles[num_particles].x = x;
     particles[num_particles].y = y;
     particles[num_particles].vx = (rand() % 7) - 3;  
     particles[num_particles].vy = (rand() % 7) - 3;
     particles[num_particles].life = (rand() % 20) + 10;  
     num_particles++;
   }
 }
}

void update_particles() {
 for (int i = 0; i < num_particles;) {
   particles[i].x += particles[i].vx;
   particles[i].y += particles[i].vy;
   particles[i].life--;
   if (particles[i].life <= 0) {
     particles[i] = particles[num_particles - 1];
     num_particles--;
   } else {
     i++;
   }
 }
}

void draw_particles(int global_x, int global_y) {
 for (int i = 0; i < num_particles; i++) {
   int screen_x = particles[i].x - global_x + (SCREEN_WIDTH / 2);
   int screen_y = particles[i].y - global_y + (SCREEN_HEIGHT / 2);
   if (screen_x >= 0 && screen_x < SCREEN_WIDTH && screen_y >= 0 &&
       screen_y < SCREEN_HEIGHT)
     plot_pixel(screen_x, screen_y, WHITE);
 }
}


int key_released = 0;
//detect the stop function according to key value
int pause_key_prev = 0;

//=========================== Function Declarations ===========================//
void plot_pixel(int x, int y, short int color);
void draw_line(int x0, int y0, int x1, int y1, short int color);
void clear_screen();
void wait_for_vsync();
void swap(int *a, int *b);
void spawn_obstacle(int global_x, int global_y);
void prune_obstacles(int global_x, int global_y);
void draw_obstacles(int global_x, int global_y);
int check_collision(int global_x, int global_y);
void reset_game(int *global_x, int *global_y, int *direction);
void show_game_over();
void display_time();
void update_background();
void spawn_particles(int x, int y, int count);
void update_particles();
void draw_particles(int global_x, int global_y);
void display_score(int score);
void spawn_collectible(int index, int global_x, int global_y);
void draw_collectibles(int global_x, int global_y);
void draw_pause_overlay(void);
void draw_char(int x, int y, char c, short int color);
void draw_string(int x, int y, const char *str, short int color);

//=========================== Double Buffering ===========================//

void update_background() {
 if (bg_timer > 0)
   bg_timer--;
 else
   bg_color = BLACK;
}

void clear_screen() {
 int total = 512 * SCREEN_HEIGHT * sizeof(short int);
 memset((void *)pixel_buffer_start, bg_color, total);
}

void clear_all_buffers() {
 int total = 512 * 240 * sizeof(short int);
 memset((void *)Buffer1, BLACK, total);
 memset((void *)Buffer2, BLACK, total);
}

//=========================== Obstacle & Collectible Functions ===========================//
void spawn_obstacle(int global_x, int global_y) {
 if (num_obstacles >= MAX_OBSTACLES) return;
 int attempts = 0;
 while (attempts < 10) {
   int offset_x = rand() % 81;
   int offset_y = -((rand() % 81) + 20);
   Obstacle new_obs;
   new_obs.width = 10;
   new_obs.height = 10;
   new_obs.color = GREEN;
   new_obs.active = 1;
   new_obs.x = global_x + offset_x;
   new_obs.y = global_y + offset_y;

   int overlap = 0;
   for (int i = 0; i < num_obstacles; i++) {
     if (obstacles[i].active) {
       if (new_obs.x < obstacles[i].x + obstacles[i].width &&
           new_obs.x + new_obs.width > obstacles[i].x &&
           new_obs.y < obstacles[i].y + obstacles[i].height &&
           new_obs.y + new_obs.height > obstacles[i].y) {
         overlap = 1;
         break;
       }
     }
   }

   if (!overlap) {
     for (int i = 0; i < 3; i++) {
       if (collectible[i].active) {
         int col_left = collectible[i].x;
         int col_right = collectible[i].x + collectible[i].width;
         int col_top = collectible[i].y;
         int col_bottom = collectible[i].y + collectible[i].height;
         int obs_left = new_obs.x;
         int obs_right = new_obs.x + new_obs.width;
         int obs_top = new_obs.y;
         int obs_bottom = new_obs.y + new_obs.height;
         if (!(obs_right <= col_left || obs_left >= col_right ||
               obs_bottom <= col_top || obs_top >= col_bottom)) {
           overlap = 1;
           break;
         }
       }
     }
   }

   if (!overlap) {
     obstacles[num_obstacles] = new_obs;
     num_obstacles++;
     break;
   }
   attempts++;
 }
}

void prune_obstacles(int global_x, int global_y) {
 int margin = 20;
 int new_count = 0;
 for (int i = 0; i < num_obstacles; i++) {
   if (obstacles[i].active) {
     if (obstacles[i].x >= global_x - (SCREEN_WIDTH / 2 + margin) &&
         obstacles[i].x <= global_x + (SCREEN_WIDTH / 2 + margin) &&
         obstacles[i].y >= global_y - (SCREEN_HEIGHT / 2 + margin) &&
         obstacles[i].y <= global_y + (SCREEN_HEIGHT / 2 + margin)) {
       obstacles[new_count++] = obstacles[i];
     }
   }
 }
 num_obstacles = new_count;
}

void draw_obstacles(int global_x, int global_y) {
 for (int i = 0; i < num_obstacles; i++) {
   if (obstacles[i].active) {
     int screen_x = obstacles[i].x - global_x + (SCREEN_WIDTH / 2);
     int screen_y = obstacles[i].y - global_y + (SCREEN_HEIGHT / 2);
     if (screen_x + obstacles[i].width < 0 || screen_x >= SCREEN_WIDTH ||
         screen_y + obstacles[i].height < 0 || screen_y >= SCREEN_HEIGHT)
       continue;
     for (int y = 0; y < obstacles[i].height; y++) {
       for (int x = 0; x < obstacles[i].width; x++) {
         plot_pixel(screen_x + x, screen_y + y, obstacles[i].color);
       }
     }
   }
 }
}

int check_collision(int global_x, int global_y) {
 int block_left = global_x - 1;
 int block_right = global_x + 1;
 int block_top = global_y - 1;
 int block_bottom = global_y + 1;
 for (int i = 0; i < num_obstacles; i++) {
   if (obstacles[i].active) {
     int obs_left = obstacles[i].x;
     int obs_right = obstacles[i].x + obstacles[i].width;
     int obs_top = obstacles[i].y;
     int obs_bottom = obstacles[i].y + obstacles[i].height;
     if (block_right >= obs_left && block_left <= obs_right &&
         block_bottom >= obs_top && block_top <= obs_bottom)
       return 1;
   }
 }
 return 0;
}

void spawn_collectible(int index, int global_x, int global_y) {
 int candidate_x, candidate_y;
 while (1) {
   int offset_x = rand() % 81 + 30;
   int offset_y = -((rand() % 81) + 30);
   candidate_x = global_x + offset_x;
   candidate_y = global_y + offset_y;
   int overlap = 0;
   for (int i = 0; i < num_obstacles; i++) {
     if (obstacles[i].active) {
       int obs_left = obstacles[i].x;
       int obs_right = obstacles[i].x + obstacles[i].width;
       int obs_top = obstacles[i].y;
       int obs_bottom = obstacles[i].y + obstacles[i].height;
       int col_left = candidate_x;
       int col_right = candidate_x + 8;
       int col_top = candidate_y;
       int col_bottom = candidate_y + 8;
       if (!(col_right <= obs_left || col_left >= obs_right ||
             col_bottom <= obs_top || col_top >= obs_bottom)) {
         overlap = 1;
         break;
       }
     }
   }
   if (!overlap) {
     for (int i = 0; i < 3; i++) {
       if (i != index && collectible[i].active) {
         int col2_left = collectible[i].x;
         int col2_right = collectible[i].x + collectible[i].width;
         int col2_top = collectible[i].y;
         int col2_bottom = collectible[i].y + collectible[i].height;
         int col_left = candidate_x;
         int col_right = candidate_x + 8;
         int col_top = candidate_y;
         int col_bottom = candidate_y + 8;
         if (!(col_right <= col2_left || col_left >= col2_right ||
               col_bottom <= col2_top || col_top >= col2_bottom)) {
           overlap = 1;
           break;
         }
       }
     }
   }
   if (!overlap) break;
 }
 collectible[index].x = candidate_x;
 collectible[index].y = candidate_y;
 collectible[index].width = 8;
 collectible[index].height = 8;
 collectible[index].active = 1;
 if (index == 0) {
   collectible[index].type = 0;  // blue, slow down
   collectible[index].color = BLUE;
 } else if (index == 1) {
   collectible[index].type = 1;  // yellow, score++
   collectible[index].color = YELLOW;
 } else if (index == 2) {
   collectible[index].type = 2;  // orange, score+3, speed up
   collectible[index].color = ORANGE;
 }
}

void draw_collectibles(int global_x, int global_y) {
 for (int i = 0; i < 3; i++) {
   if (!collectible[i].active) continue;
   int screen_x = collectible[i].x - global_x + (SCREEN_WIDTH / 2);
   int screen_y = collectible[i].y - global_y + (SCREEN_HEIGHT / 2);
   if (screen_x + collectible[i].width < 0 || screen_x >= SCREEN_WIDTH ||
       screen_y + collectible[i].height < 0 || screen_y >= SCREEN_HEIGHT)
     continue;
   for (int y = 0; y < collectible[i].height; y++) {
     for (int x = 0; x < collectible[i].width; x++) {
       plot_pixel(screen_x + x, screen_y + y, collectible[i].color);
     }
   }
 }
}

//=========================== Game State Functions ===========================//
void reset_game(int *global_x, int *global_y, int *direction) {
 *global_x = SCREEN_WIDTH / 2;
 *global_y = SCREEN_HEIGHT / 2;
 *direction = 0;
 num_points = 1;
 turning_points[0].x = *global_x;
 turning_points[0].y = *global_y;
 turning_points[0].color = WHITE;
 num_obstacles = 0;
 obstacle_spawn_counter = 0;
 time_hundredths = 0;
 rounds = 0;
 score = 0;
 obstacle_spawn_interval = (simple_mode ? 60 : 30);
 display_score(score);

 *(volatile int *)TIMER_START_HI = TIMER_0_5_SEC_HI;
 *(volatile int *)TIMER_START_LO = TIMER_0_5_SEC_LO;
 *(volatile int *)TIMER_CONTROL = 0x7;

 clear_all_buffers();

 key_released = 0;
}

void show_game_over() {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      plot_pixel(x, y, RED);
    }
  }
  draw_string(50, 20, "GAME OVER", WHITE);
}


void display_time() {
    while (time_hundredths >= 9900) {
        time_hundredths -= 9900;
        rounds++;
        *LED_REG = (1 << (rounds - 1));
    }
    int int_part = time_hundredths / 100;
    int frac_part = time_hundredths % 100;
    int d3 = int_part / 10;
    int d2 = int_part % 10;
    int d1 = frac_part / 10;
    int d0 = frac_part % 10;
    volatile int *hex_ptr = (int *)HEX3_HEX0_BASE;
    *hex_ptr = (seg7[d3] << 24) | ((seg7[d2] | 0x80) << 16) | (seg7[d1] << 8) | seg7[d0];
}

void display_score(int score) {
 int tens = score / 10;
 int ones = score % 10;
 volatile int *score_hex_ptr = (int *)0xFF200030;
 *score_hex_ptr = (seg7[tens] << 8) | seg7[ones];
}

void plot_pixel(int x, int y, short int color) {
 if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
 volatile short int *one_pixel_address =
     (volatile short int *)(pixel_buffer_start + (y << 10) + (x << 1));
 *one_pixel_address = color;
}

void draw_line(int x0, int y0, int x1, int y1, short int color) {
 if (x0 == x1) {
   if (y1 < y0) swap(&y0, &y1);
   for (int i = 0; i <= (y1 - y0); i++) plot_pixel(x0, y0 + i, color);
 } else {
   if (x1 < x0) swap(&x0, &x1);
   for (int i = 0; i <= (x1 - x0); i++) plot_pixel(x0 + i, y0, color);
 }
}

void wait_for_vsync() {
 volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
 *pixel_ctrl_ptr = 1;
 while ((*(pixel_ctrl_ptr + 3) & 1));
}

void swap(int *a, int *b) {
 int temp = *a;
 *a = *b;
 *b = temp;
}
//below display the pause
void draw_pause_overlay(void) {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      plot_pixel(x, y, YELLOW);
    }
  }
  int bar_width = 4;
  int bar_height = 40;
  int gap = 8;
  int center_x = SCREEN_WIDTH / 2;
  int center_y = SCREEN_HEIGHT / 2;
  int left_bar_x = center_x - gap/2 - bar_width;
  int right_bar_x = center_x + gap/2;
  for (int y = center_y - bar_height/2; y < center_y + bar_height/2; y++) {
    for (int x = left_bar_x; x < left_bar_x + bar_width; x++) {
      plot_pixel(x, y, WHITE);
    }
    for (int x = right_bar_x; x < right_bar_x + bar_width; x++) {
      plot_pixel(x, y, WHITE);
    }
  }
}


//below will display GAME OVER on the screen
void draw_char(int x, int y, char c, short int color) {
    const uint8_t *bitmap = 0;
   
    switch(c) {
      case 'G': {
          static const uint8_t bits[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x0F, 0x00};
          bitmap = bits;
          break;
      }
      case 'A': {
          static const uint8_t bits[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00};
          bitmap = bits;
          break;
      }
      case 'M': {
          static const uint8_t bits[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x00};
          bitmap = bits;
          break;
      }
      case 'E': {
          static const uint8_t bits[7] = {0x1F, 0x10, 0x1E, 0x10, 0x1F, 0x00, 0x00};
          bitmap = bits;
          break;
      }
      case 'O': {
          static const uint8_t bits[7] = {0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00};
          bitmap = bits;
          break;
      }
      case 'V': {
          static const uint8_t bits[7] = {0x11, 0x11, 0x0A, 0x0A, 0x04, 0x00, 0x00};
          bitmap = bits;
          break;
      }
      case 'R': {
          static const uint8_t bits[7] = {0x1E, 0x11, 0x1E, 0x14, 0x12, 0x00, 0x00};
          bitmap = bits;
          break;
      }
      case ' ': {
          static const uint8_t bits[7] = {0,0,0,0,0,0,0};
          bitmap = bits;
          break;
      }
      default:
          return;
    }
    
    for (int row = 0; row < 7; row++) {
        uint8_t line = bitmap[row];
        for (int col = 0; col < 5; col++) {
            if (line & (1 << (4 - col))) {
                plot_pixel(x + col, y + row, color);
            }
        }
    }
}


void draw_string(int x, int y, const char *str, short int color) {
    int orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 8; 
            x = orig_x;
        } else {
            draw_char(x, y, *str, color);
            x += 6;
        }
        str++;
    }
}

//=========================== Main Program ===========================//
int main(void) {
 srand((unsigned int)time(NULL));
 audio_t *const audiop = (audio_t *)AUDIO_BASE;
 volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
 int global_x = SCREEN_WIDTH / 2;
 int global_y = SCREEN_HEIGHT / 2;
 turning_points[0].x = global_x;
 turning_points[0].y = global_y;
 turning_points[0].color = WHITE;
 num_points = 1;
 int direction = 0;


 volatile int *sw_ptr = (int *)0xFF200040;
 //if sw0 on, simple mode, else hard mode
 if ((*sw_ptr) & 0x1)
   simple_mode = 1;
 else
   simple_mode = 0;


 obstacle_spawn_interval = (simple_mode ? 60 : 30);


 *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
 wait_for_vsync();
 pixel_buffer_start = *pixel_ctrl_ptr;
 clear_screen();

 *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
 pixel_buffer_start = *(pixel_ctrl_ptr + 1);
 clear_screen();


 *(volatile int *)TIMER_START_HI = TIMER_0_5_SEC_HI;
 *(volatile int *)TIMER_START_LO = TIMER_0_5_SEC_LO;
 *(volatile int *)TIMER_CONTROL = 0x7;
 for (int i = 0; i < 3; i++) {
   spawn_collectible(i, global_x, global_y);
 }

 while (1) {
   wait_for_vsync();
   int front_buf = *pixel_ctrl_ptr;
   if (front_buf == (int)Buffer1) {
     *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
     pixel_buffer_start = (int)Buffer2;
   } else {
     *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
     pixel_buffer_start = (int)Buffer1;
   }
   update_background();
   clear_screen();

 
   volatile int *key_ptr = (int *)0xFF200050;
   int current_pause = ((*key_ptr) & 0x2) ? 1 : 0;
   if (game_state == RUNNING && current_pause && !pause_key_prev) {
     game_state = PAUSED;
   } else if (game_state == PAUSED && current_pause && !pause_key_prev) {
     game_state = RUNNING;
   }
   pause_key_prev = current_pause;

   if (game_state == PAUSED) {
     draw_pause_overlay();
     continue; 
   }

   if (game_state == RUNNING) {
     if (*(volatile int *)TIMER_STATUS & 0x1) {
       *(volatile int *)TIMER_STATUS = 1;
       time_hundredths += 50; 
       display_time();
     }
   }
   update_particles();

   if (game_state == RUNNING) {
     // turn if there is an audio
     if (frameCount == 0) {
       if (audiop->rarc > 0) {
         int max_amplitude = 0;
         while (audiop->rarc > 0) {
           int left_sample = audiop->ldata;
           int right_sample = audiop->rdata;
           int amplitude = (left_sample < 0) ? -left_sample : left_sample;
           if (amplitude > max_amplitude) max_amplitude = amplitude;
         }
         if (max_amplitude > THRESHOLD) {
           if (num_points < MAX_POINTS) {
             turning_points[num_points].x = global_x;
             turning_points[num_points].y = global_y;
             turning_points[num_points].color = WHITE;
             num_points++;
           }
         
           direction = (direction == 0) ? 1 : 0;
           frameCount = 5;
           // randomly choose color
           bg_color = get_random_color();
           bg_timer = 10;
           spawn_particles(global_x, global_y, 10);
         }
       }
     } else {
       frameCount--;
       if (frameCount == 1) {
         while (audiop->rarc > 0) {
           int left_sample = audiop->ldata;
           int right_sample = audiop->rdata;
         }
       }
     }

     //movement update
     if (direction == 0)
       global_y -= speed_factor;
     else
       global_x += speed_factor;

     obstacle_spawn_counter++;
     if (obstacle_spawn_counter >= obstacle_spawn_interval) {
       spawn_obstacle(global_x, global_y);
       obstacle_spawn_counter = 0;
     }
     prune_obstacles(global_x, global_y);
     if (check_collision(global_x, global_y)) game_state = GAME_OVER;
     for (int i = 0; i < 3; i++) {
       if (collectible[i].active) {
         if (global_x >= collectible[i].x &&
             global_x <= collectible[i].x + collectible[i].width &&
             global_y >= collectible[i].y &&
             global_y <= collectible[i].y + collectible[i].height) {
           if (collectible[i].type == 0) {
             speed_factor = 1;
             obstacle_spawn_interval = (simple_mode ? 60 : 30);
           } else if (collectible[i].type == 1) {
             if (score < 99) score += 2;
             obstacle_spawn_interval /= 1.3;
           } else if (collectible[i].type == 2) {
             speed_factor++;
             if (score < 99) score += 4;
           }
           display_score(score);
           collectible[i].active = 0;
           spawn_collectible(i, global_x, global_y);
         }
         int screen_x = collectible[i].x - global_x + (SCREEN_WIDTH / 2);
         int screen_y = collectible[i].y - global_y + (SCREEN_HEIGHT / 2);
         if ((screen_x + collectible[i].width < 0) ||
             (screen_x >= SCREEN_WIDTH) ||
             (screen_y + collectible[i].height < 0) ||
             (screen_y >= SCREEN_HEIGHT)) {
           spawn_collectible(i, global_x, global_y);
         }
       }
     }

     //display route behind
     int prev_disp_x = turning_points[0].x - global_x + (SCREEN_WIDTH / 2);
     int prev_disp_y = turning_points[0].y - global_y + (SCREEN_HEIGHT / 2);
     for (int i = 1; i < num_points; i++) {
       int curr_disp_x = turning_points[i].x - global_x + (SCREEN_WIDTH / 2);
       int curr_disp_y = turning_points[i].y - global_y + (SCREEN_HEIGHT / 2);
       draw_line(prev_disp_x, prev_disp_y, curr_disp_x, curr_disp_y, WHITE);
       prev_disp_x = curr_disp_x;
       prev_disp_y = curr_disp_y;
     }
     int current_disp_x = SCREEN_WIDTH / 2;
     int current_disp_y = SCREEN_HEIGHT / 2;
     draw_line(prev_disp_x, prev_disp_y, current_disp_x, current_disp_y, WHITE);

     draw_obstacles(global_x, global_y);
     draw_particles(global_x, global_y);
     draw_collectibles(global_x, global_y);


     for (int i = -1; i < 2; i++) {
       for (int j = -1; j < 2; j++) {
         plot_pixel(current_disp_x + i, current_disp_y + j, RED);
       }
     }
   } else { // GAME_OVER 状态
     show_game_over();
     if (!key_released) {
       if (((*key_ptr) & 0x1) != 0) key_released = 1;
     } else {
       if (((*key_ptr) & 0x1) == 0) {
         reset_game(&global_x, &global_y, &direction);
         num_points = 1;
         turning_points[0].x = global_x;
         turning_points[0].y = global_y;
         turning_points[0].color = WHITE;
         game_state = RUNNING;
         frameCount = 0;
         key_released = 0;
 
         if ((*sw_ptr) & 0x1)
           simple_mode = 1;
         else
           simple_mode = 0;
         obstacle_spawn_interval = (simple_mode ? 60 : 30);
         speed_factor = 1;
         for (int i = 0; i < 3; i++) {
           spawn_collectible(i, global_x, global_y);
         }
       }
     }
   }
 }

 return 0;
}
