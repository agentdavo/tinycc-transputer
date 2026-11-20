/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "relay.h"
#include <string.h>

#ifdef NIGHT_RELAY_HOST
#include <stdio.h>
#else
#include "../../fbwm/fbwm_theme.h"
#endif

static Relay game;

#ifdef NIGHT_RELAY_HOST
static uint32_t screen[800 * 600];
static void mark(const char *line) { fputs(line, stdout); }
static void present(void) { }
#else
#define UART0 0x10000000u
#define UART1 0x11000000u
#define UART2 0x12000000u
#define VGA_FLIP 0xa0000014u
#define TIMER_HZ 15625u
static uint32_t *screen;

unsigned int __ldtimer(void);

static unsigned port_read(unsigned address)
{
    return *(volatile unsigned int *)address;
}

static int input_byte(unsigned port)
{
    if (port_read(port + 4) & 2)
        return (int)(port_read(port) & 255u);
    return -1;
}

static void mark(const char *line)
{
    while (*line)
        *(volatile unsigned char *)UART0 = (unsigned char)*line++;
}

static void present(void)
{
    *(volatile unsigned int *)VGA_FLIP = TPOS_FB_BASE;
}

static int launch_key(void)
{
    if (game.mode == GAME_OVER) {
        relay_reset(&game);
    } else if (game.mode == WAITING) {
        game.mode = CHARGING;
        game.tick = 0;
    } else if (game.mode == CHARGING) {
        relay_launch(&game);
        mark("NIGHT_RELAY_LAUNCH\r\n");
    }
    return 1;
}

static int run_live(void)
{
    unsigned last = __ldtimer();
    unsigned accum = 0;
    int mouse = 0, mouse_rest = 0, n, ch, left = 0, right = 0, held;
    int redraw = 0, step = 0, running = 1;
    while (running) {
        for (n = 0; n < 16; n++) {
            ch = input_byte(UART2);
            if (ch < 0)
                break;
            if (mouse_rest) {
                mouse_rest--;
            } else if (ch & 0x80) {
                mouse = ch & 7;
                mouse_rest = 2;
            }
        }
        for (n = 0; n < 16; n++) {
            ch = input_byte(UART1);
            if (ch < 0)
                ch = input_byte(UART0);
            if (ch < 0)
                break;
            switch (ch) {
            case 27: running = 0; break;
            case ' ': redraw = launch_key(); break;
            case 'z': case 'Z': left = 13; break;
            case 'x': case 'X': right = 13; break;
            case 'c': case 'C': left = right = 13; break;
            case 'p': case 'P': game.paused = !game.paused; redraw = 1; break;
            case 'a': case 'A': case 'd': case 'D':
                if (game.mode == PLAYING && game.tilt < 100) {
                    game.nudge = ch == 'a' || ch == 'A' ? -180 : 180;
                    game.tilt += 27;
                    if (game.tilt >= 100)
                        game.tilt = 100;
                }
                break;
            default: break;
            }
        }
        if (!running)
            break;
        {
            unsigned now = __ldtimer();
            unsigned elapsed = now - last;
            last = now;
            if (elapsed > TIMER_HZ / 20)
                elapsed = TIMER_HZ / 20;
            accum += elapsed * 120;
        }
        if (accum >= TIMER_HZ) {
            int previous_mode = game.mode;
            int left_pose = game.flip[0];
            int right_pose = game.flip[1];
            accum -= TIMER_HZ;
            held = (mouse & 1) ? LEFT_FLIP : 0;
            if (mouse & 4)
                held |= RIGHT_FLIP;
            if (left) {
                held |= LEFT_FLIP;
                left--;
            }
            if (right) {
                held |= RIGHT_FLIP;
                right--;
            }
            relay_tick(&game, held);
            if (previous_mode != game.mode || left_pose != game.flip[0] ||
                right_pose != game.flip[1])
                redraw = 1;
            if (++step >= 2) {
                step = 0;
                redraw = game.mode == PLAYING || game.mode == CHARGING ||
                         redraw || held;
            }
        }
        if (redraw) {
            relay_draw(&game, 0);
            present();
            redraw = 0;
        }
    }
    return 0;
}
#endif

static unsigned pixel_checksum(void)
{
    unsigned hash = 2166136261u;
    int i;
    for (i = 0; i < 800 * 600; i++)
        hash = (hash ^ screen[i]) * 16777619u;
    return hash;
}

static void mark_checksum(unsigned hash)
{
    static const char digits[] = "0123456789abcdef";
    char hex[11];
    int i;
    for (i = 0; i < 8; i++)
        hex[i] = digits[(hash >> (28 - i * 4)) & 15];
    hex[8] = '\r';
    hex[9] = '\n';
    hex[10] = '\0';
    mark("NIGHT_RELAY_CHECKSUM=");
    mark(hex);
}

static int verify(void)
{
    int status = relay_verify(&game);
    if (status) {
        char code[3];
        code[0] = (char)('0' + status);
        code[1] = '\n';
        code[2] = '\0';
        mark("NIGHT_RELAY_VERIFY_FAIL=");
        mark(code);
        return status;
    }
    relay_draw(&game, 1);
    present();
    mark("NIGHT_RELAY_FRAME\r\n");
    mark_checksum(pixel_checksum());
    mark("NIGHT_RELAY_VERIFY_PASS\r\n");
    return 0;
}

#ifdef NIGHT_RELAY_HOST
static int write_frame(const char *filename)
{
    FILE *file = fopen(filename, "wb");
    unsigned pixel;
    int i;
    if (!file)
        return 1;
    fputs("P6\n800 600\n255\n", file);
    for (i = 0; i < 800 * 600; i++) {
        pixel = screen[i];
        fputc((int)(pixel >> 16) & 255, file);
        fputc((int)(pixel >> 8) & 255, file);
        fputc((int)pixel & 255, file);
    }
    return fclose(file) != 0;
}
#endif

int main(int argc, char **argv)
{
    int status;
#ifndef NIGHT_RELAY_HOST
    screen = (uint32_t *)TPOS_FB_BASE;
#endif
    relay_reset(&game);
    relay_draw_start(screen);
    relay_draw(&game, 1);
    present();
    mark("NIGHT_RELAY_READY\r\n");
#ifdef NIGHT_RELAY_HOST
    status = verify();
    if (!status && argc > 1)
        status = write_frame(argv[1]);
#else
    if (argc > 1 && strcmp(argv[1], "--verify") == 0)
        status = verify();
    else
        status = run_live();
#endif
    mark("NIGHT_RELAY_QUIT\r\n");
    return status;
}
