#include <stdlib.h>
#include "background.h"
#include "map.h"
#include "map2.h"
#include "characters.h"

#define WIDTH 240
#define HEIGHT 160

#define PALETTE_SIZE 256
#define SPRITE_NUM 128

#define MODE0 0x00
#define BG0_ENABLE 0x100
#define BG1_ENABLE 0x200

#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000

#define DMA_ENABLE 0x80000000
#define DMA_16 0x00000000
#define DMA_32 0x04000000

volatile unsigned long* display_control = (volatile unsigned long*)0x4000000;
volatile unsigned short* bg0_control = (volatile unsigned short*)0x4000008;
volatile unsigned short* bg_palette = (volatile unsigned short*)0x5000000;
volatile unsigned short* sprite_palette = (volatile unsigned short*)0x5000200;
volatile unsigned short* sprite_data = (volatile unsigned short*)0x6010000;
/*object data memory*/
volatile unsigned short* sprite_att = (volatile unsigned short*)0x7000000;

volatile unsigned short* buttons = (volatile unsigned short*)0x04000130;
volatile unsigned short* scanline_counter = (volatile unsigned short*)0x4000006;

volatile short* bg0_x_scroll = (volatile short*)0x4000010;
volatile short* bg0_y_scroll = (volatile short*)0x4000012;

volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

#define BUTTON_A (1 << 0)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_UP (1 << 6)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)

void wait_vblank() {
    while (*scanline_counter < HEIGHT) {}
}

unsigned char button_pressed(unsigned short button) {
    unsigned short pressed = *buttons & button;
    return (pressed == 0) ? 1 : 0;
}

volatile unsigned short* char_block(unsigned long block) {
    return (volatile unsigned short*)(0x6000000 + (block * 0x4000));
}

volatile unsigned short* screen_block(unsigned long block) {
    return (volatile unsigned short*)(0x6000000 + (block * 0x800));
}

void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

void setup_zombie_background() { 

    memcpy16_dma((unsigned short*) bg_palette, (unsigned short*) background_palette, PALETTE_SIZE);

    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) background_data, (background_width * background_height) / 2);

    *bg0_control = 0 | (0 << 2) | (0 << 6) | (1 << 7) | (16 << 8) | (1 << 13) | (0 << 14);

    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) map, map_width * map_height);
}

void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

struct Sprite sprites[SPRITE_NUM];
int next_sprite_index = 0;

enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};

struct Sprite* sprite_init(int x, int y, enum SpriteSize size, int horizontal_flip, int vertical_flip, int tile_index, int priority) {

    int index = next_sprite_index++;

    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    int h = horizontal_flip ? 1 : 0;
    int v = vertical_flip ? 1 : 0;

    sprites[index].attribute0 = y | (0 << 8) | (0 << 10) | (0 << 12) | (1 << 13) | (shape_bits << 14);

    sprites[index].attribute1 = x | (0 << 9) | (h << 12) | (v << 13) | (size_bits << 14);

    sprites[index].attribute2 = tile_index | (priority << 10) | (0 << 12);

    return &sprites[index];
}

void sprite_position(struct Sprite* sprite, int x, int y) {

    sprite->attribute0 &= 0xff00;

    sprite->attribute0 |= (y & 0xff);

    sprite->attribute1 &= 0xfe00;

    sprite->attribute1 |= (x & 0x1ff);
}

void sprite_move(struct Sprite* sprite, int dx, int dy) {

    int y = sprite->attribute0 & 0xff;

    int x = sprite->attribute1 & 0x1ff;

    sprite_position(sprite, x + dx, y + dy);
}

void sprite_set_vertical_flip(struct Sprite* sprite, int vertical_flip) {
    if (vertical_flip) {

        sprite->attribute1 |= 0x2000;

    } else {

        sprite->attribute1 &= 0xdfff;
    }
}

void sprite_set_horizontal_flip(struct Sprite* sprite, int horizontal_flip) {
    if (horizontal_flip) {

        sprite->attribute1 |= 0x1000;
    } else {

        sprite->attribute1 &= 0xefff;
    }
}

void sprite_set_offset(struct Sprite* sprite, int offset) {

    sprite->attribute2 &= 0xfc00;

    sprite->attribute2 |= (offset & 0x03ff);
}

void sprite_update_all() {

    memcpy16_dma((unsigned short*) sprite_att, (unsigned short*) sprites, SPRITE_NUM * 4);
}

void sprite_clear() {

    next_sprite_index = 0;

    for(int i = 0; i < SPRITE_NUM; i++) {
        sprites[i].attribute0 = HEIGHT;
        sprites[i].attribute1 = WIDTH;
    }
}

void setup_sprite_image() {
    //survivor
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) characters_palette, PALETTE_SIZE);

    memcpy16_dma((unsigned short*) sprite_data, (unsigned short*) characters_data, (characters_width * characters_height) / 2);
}


struct Survivor {
    struct Sprite* sprite;

    int x, y;

    int frame;

    int animation_delay;

    int counter;

    int move;

    int border;
};

struct Zombie {
	struct Sprite* sprite;

	int x, y;

	int frame;

	int animation_delay;

	int counter;

	int move;

	int border;
};

void survivor_init(struct Survivor* survivor) {
    survivor->x = 100;
    survivor->y = 140;
    survivor->border = 40;
    survivor->frame = 0;
    survivor->move = 0;
    survivor->counter = 0;
    survivor->animation_delay = 8;
    survivor->sprite = sprite_init(survivor->x, survivor->y, SIZE_16_16, 0, 0, survivor->frame, 0);
}

void zombie_init(struct Zombie* zombie) {
    zombie->x = rand() % (WIDTH - 16);
    zombie->y = 0;
    zombie->border = 40;
    zombie->frame = 8;
    zombie->move = 1;
    zombie->counter = 0;
    zombie->animation_delay = 8;
    zombie->sprite = sprite_init(zombie->x, zombie->y + 16, SIZE_16_32, 0, 0, zombie->frame, 0);
}

int survivor_left(struct Survivor* survivor) {

    sprite_set_horizontal_flip(survivor->sprite, 1);
    survivor->move = 1;

    /* if we are at the left end, block sprite from scrolling the screen */
    if (survivor->x <= survivor->border) {
        return 1;
    } else {
        survivor->x--;
        return 0;
    }
}

int survivor_right(struct Survivor* survivor) {
    /* face right */
    sprite_set_horizontal_flip(survivor->sprite, 0);
    survivor->move = 1;

    /* if we are at the right end, block sprite from scrolling the screen */
    if (survivor->x >= (WIDTH - 16 - survivor->border)) {
        return 1;
    } else {
        survivor->x++;
        return 0;
    }
}

int survivor_top(struct Survivor* survivor) {
    sprite_set_vertical_flip(survivor->sprite, 0);
    survivor->move = 1;

    /* if we are at the top, don't move further up */
    if (survivor->y <= 100) {
        return 1;
    } else {
        survivor->y--;
        return 0;
    }
}

int survivor_bottom(struct Survivor* survivor) {
    sprite_set_vertical_flip(survivor->sprite, 0);
    survivor->move = 1;

    /* if we are at the bottom, don't move further down */
    if (survivor->y >= 140) {
        return 1;
    } else {
        survivor->y++;
        return 0;
    }
}

void survivor_stop(struct Survivor* survivor) {
    survivor->move = 0;
    survivor->frame = 0;
    survivor->counter = 7;
    sprite_set_offset(survivor->sprite, survivor->frame);
}

void survivor_update(struct Survivor* survivor) {
    if (survivor->move) {
        survivor->counter++;
        if (survivor->counter >= survivor->animation_delay) {
            survivor->frame = survivor->frame + 16;
            if (survivor->frame > 16) {
                survivor->frame = 0;
            }
            sprite_set_offset(survivor->sprite, survivor->frame);
            survivor->counter = 0;
        }
    }

    if (survivor->move) {
        sprite_position(survivor->sprite, survivor->x, survivor->y);
    }

}

void spawn_zombie(struct Zombie* zombie) {
    zombie->x = rand() % WIDTH;
    zombie->y = 0;
    zombie->move = 1;
}

void zombie_update(struct Zombie* zombie) {
    if (zombie->move) {
        zombie->y++;

    }
    sprite_position(zombie->sprite, zombie->x, zombie->y);
}


int checkCollision(int x1, int y1, int width1, int height1, int x2, int y2, int width2, int height2) {
    return (x1 < x2 + width2 &&
            x1 + width1 > x2 &&
            y1 < y2 + height2 &&
            y1 + height1 > y2);
}

int checkSpriteCollision(struct Survivor* survivor, struct Zombie* zombie) {

    return checkCollision(
        survivor->x, survivor->y, 16, 16,
        zombie->x, zombie->y, 16, 16
    );
}

int summon(int a, int b);

int main() {
    *display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    setup_zombie_background();
    setup_sprite_image();
    sprite_clear();

    struct Survivor survivor;
    survivor_init(&survivor);

    struct Zombie zombie;
    zombie_init(&zombie);

    int xscroll = 0;
    int yscroll = 0;

    int zombieCounter = 0;
    
    int timeAlive = 0;

    int livesLeft = 3;

    while (1) {
        wait_vblank();

        timeAlive++;

        yscroll--;
        survivor_update(&survivor);

        zombie_update(&zombie);

        if (checkSpriteCollision(&survivor, &zombie)) {
            livesLeft = livesLeft - 1;
        }

        zombieCounter++;
        if (zombieCounter >= 180) {
            zombieCounter = 0;

            int zombieSummon = summon(livesLeft, timeAlive);
        
            for (int i = 0; i < zombieSummon; i++) {
                zombie_init(&zombie);
        }
        }
        if (button_pressed(BUTTON_RIGHT)) {
            if (survivor_right(&survivor)) {
                xscroll++;
            }
        } else if (button_pressed(BUTTON_LEFT)) {
            if (survivor_left(&survivor)) {
                xscroll--;
            }
        } else {
            survivor_stop(&survivor);
        }

        if (button_pressed(BUTTON_UP)) {
            if (survivor_top(&survivor)) {
                yscroll--;
            }
        } else if (button_pressed(BUTTON_DOWN)) {
            survivor_bottom(&survivor);
        }

        
        *bg0_x_scroll = xscroll;
        *bg0_y_scroll = yscroll;
        sprite_update_all();

        delay(400);
    }

    return 0;
}

