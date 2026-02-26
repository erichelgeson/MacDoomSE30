/*
 * i_video_mac.c — Video interface for Doom SE/30
 *
 * Doom renders to a 320x200 8-bit buffer (screens[0]).
 * During gameplay (GS_LEVEL), wall/floor/ceiling rendering goes directly to
 * the 1-bit framebuffer via R_DrawColumn_Mono / R_DrawSpan_Mono.
 * I_FinishUpdate blits only the non-view area (status bar, border) and any
 * HUD/menu overlay pixels (non-zero in screens[0]) over the direct render.
 *
 * During menus, wipes, intermission: full screens[0] blit as before.
 */

#include <QuickDraw.h>
#include <Memory.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"

/* ---- Framebuffer info — non-static so r_draw.c can access them ---- */
byte           *fb_mono_base     = NULL;  /* 1-bit framebuffer base address */
int             fb_mono_rowbytes = 0;     /* bytes per row in framebuffer */
int             fb_mono_xoff     = 0;     /* (fb_width - SCREENWIDTH) / 2 */
int             fb_mono_yoff     = 0;     /* (fb_height - SCREENHEIGHT) / 2 */

/* Grayscale palette: maps Doom's 256-color palette to 0-255 grayscale.
 * Non-static so r_draw.c can use it for direct 1-bit rendering. */
byte            grayscale_pal[256];

/* 4x4 Bayer ordered dither threshold matrix (0-15 scaled to 0-255).
 * Non-static so r_draw.c can use the same matrix. */
const byte bayer4x4[4][4] = {
    {  0, 136,  34, 170 },
    { 204,  68, 238, 102 },
    {  51, 187,  17, 153 },
    { 255, 119, 221,  85 }
};

/* Dedicated menu overlay buffer — M_Drawer renders here (not into screens[1],
 * which Doom uses as its border tile cache). Non-static so d_main.c can use it
 * to redirect M_Drawer output. */
byte *menu_overlay_buf = NULL;

/* Set to true by d_main.c during the screen-wipe loop to force full blit.
 * During a wipe, wipe_ScreenWipe owns every pixel of screens[0]; we must not
 * skip the view area or the melt will show corrupted content. */
boolean wipe_in_progress = false;

/* View geometry — from r_draw.c/r_main.c, used for selective blit */
extern int viewwindowx;
extern int viewwindowy;
extern int viewheight;
extern int scaledviewwidth;

/* Gamma applied to grayscale before dithering.
 * Doom's palette is designed for CRT gamma ~2.2; raw luminance values
 * are perceptually dark, causing midtone surfaces to dither to solid black
 * on a 1-bit display.  A gamma < 1.0 brightens midtones while leaving
 * true black (0) and true white (255) unchanged.
 * Tune DITHER_GAMMA: lower = brighter. */
#define DITHER_GAMMA 0.50f

/* Precomputed gamma curve: gray_in (0-255) -> gray_out (0-255).
 * Populated once in I_InitGraphics.  I_SetPalette uses a table lookup
 * instead of calling powf() at runtime — critical on the SE/30 because
 * software-emulated powf() costs ~20K cycles per call (no FPU), and
 * I_SetPalette is called every game tic during damage/pickup flashes. */
static byte gamma_curve[256];

void I_InitGraphics(void)
{
    BitMap *screen = &qd.screenBits;
    fb_mono_base     = (byte *)screen->baseAddr;
    fb_mono_rowbytes = screen->rowBytes;
    fb_mono_xoff     = (screen->bounds.right  - screen->bounds.left  - SCREENWIDTH)  / 2;
    fb_mono_yoff     = (screen->bounds.bottom - screen->bounds.top   - SCREENHEIGHT) / 2;

    doom_log("I_InitGraphics: %dx%d, rowBytes=%d, xoff=%d, yoff=%d\r",
             screen->bounds.right  - screen->bounds.left,
             screen->bounds.bottom - screen->bounds.top,
             fb_mono_rowbytes, fb_mono_xoff, fb_mono_yoff);

    /* Precompute gamma correction table.
     * powf() is called here 254 times — once at startup, never again.
     * I_SetPalette (called every tic during damage/pickup flashes) does
     * a table lookup instead, costing 1 cycle instead of ~20K cycles. */
    {
        int g;
        gamma_curve[0]   = 0;
        gamma_curve[255] = 255;
        for (g = 1; g < 255; g++)
            gamma_curve[g] = (byte)(255.0f * powf(g / 255.0f, DITHER_GAMMA) + 0.5f);
    }

    /* Hide the Mac OS software cursor.
     *
     * System 7's cursor VBL task runs at 60 Hz: it saves the pixels under
     * the cursor, draws the cursor bitmap, and on the next tick restores the
     * saved pixels.  Because we write directly to the framebuffer (bypassing
     * QuickDraw), the "saved pixels" can be stale — when the VBL task
     * restores them it overwrites our freshly-rendered content for one scan
     * cycle, causing the aperiodic "candle flicker" on sprites and overlays.
     * HideCursor() increments the OS hide-level so the VBL task stops
     * touching the framebuffer entirely.  ShowCursor() in I_ShutdownGraphics
     * decrements it back when we exit. */
    HideCursor();

    /* Allocate Doom's screen buffer: 320x200 8-bit */
    screens[0] = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!screens[0])
        I_Error("I_InitGraphics: failed to allocate screen buffer");
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);

    /* Allocate a dedicated menu overlay buffer.
     * NOTE: screens[1] is Doom's border tile cache (R_FillBackScreen writes
     * the border pattern there; R_DrawViewBorder copies from it to screens[0]).
     * We must NOT touch screens[1].  Use a separate allocation instead. */
    menu_overlay_buf = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!menu_overlay_buf)
        I_Error("I_InitGraphics: failed to allocate overlay buffer");
    memset(menu_overlay_buf, 0, SCREENWIDTH * SCREENHEIGHT);
}

void I_ShutdownGraphics(void)
{
    ShowCursor();
}

/*
 * I_SetPalette — Doom calls this when the palette changes.
 * We convert the RGB palette to grayscale values for our 1-bit output.
 * palette is 768 bytes: 256 entries of (R, G, B).
 */
void I_SetPalette(byte *palette)
{
    int i;
    for (i = 0; i < 256; i++) {
        int r = palette[i * 3 + 0];
        int g = palette[i * 3 + 1];
        int b = palette[i * 3 + 2];
        /* Standard luminance (BT.601), then gamma via precomputed table */
        int gray = (r * 77 + g * 150 + b * 29) >> 8;
        grayscale_pal[i] = gamma_curve[gray];
    }
}

/*
 * Blit 8 pixels from src (palette indices) to one byte at dst (1-bit).
 * bayer_r is the Bayer row for this screen y (4-element, indexed by x&3).
 * x is the screen-x of the leftmost of the 8 pixels (for Bayer column index).
 */
static inline unsigned char blit8(const byte *src, const byte *bayer_r, int x)
{
    unsigned char out = 0;
    if (grayscale_pal[src[0]] < bayer_r[(x+0) & 3]) out |= 0x80;
    if (grayscale_pal[src[1]] < bayer_r[(x+1) & 3]) out |= 0x40;
    if (grayscale_pal[src[2]] < bayer_r[(x+2) & 3]) out |= 0x20;
    if (grayscale_pal[src[3]] < bayer_r[(x+3) & 3]) out |= 0x10;
    if (grayscale_pal[src[4]] < bayer_r[(x+4) & 3]) out |= 0x08;
    if (grayscale_pal[src[5]] < bayer_r[(x+5) & 3]) out |= 0x04;
    if (grayscale_pal[src[6]] < bayer_r[(x+6) & 3]) out |= 0x02;
    if (grayscale_pal[src[7]] < bayer_r[(x+7) & 3]) out |= 0x01;
    return out;
}

/*
 * blit8_black — for HUD/menu overlay over direct-rendered view area.
 * Any non-zero pixel in screens[0] maps to black (set bit) in the 1-bit fb.
 * This gives maximum contrast: overlay text/elements always appear solid black
 * regardless of what the 1-bit renderer put underneath.
 * Returns 0 for an all-zero chunk (common case), so OR-ing into fb is safe.
 */
static inline unsigned char blit8_black(const byte *src)
{
    unsigned char out = 0;
    if (src[0]) out |= 0x80;
    if (src[1]) out |= 0x40;
    if (src[2]) out |= 0x20;
    if (src[3]) out |= 0x10;
    if (src[4]) out |= 0x08;
    if (src[5]) out |= 0x04;
    if (src[6]) out |= 0x02;
    if (src[7]) out |= 0x01;
    return out;
}

/*
 * I_FinishUpdate — blit Doom's 320x200 8-bit buffer to the 512x342 1-bit screen.
 *
 * During gameplay (GS_LEVEL, not automap):
 *   - The view area was rendered directly to 1-bit by R_DrawColumn_Mono /
 *     R_DrawSpan_Mono.  screens[0]'s view area was pre-cleared to 0 by
 *     R_RenderPlayerView so we can detect HUD/menu overlays by non-zero check.
 *   - We blit the non-view area (status bar, border) from screens[0].
 *   - We overlay non-zero view pixels (HUD text, active menu) from screens[0].
 *
 * During menus, wipes, intermission:
 *   - Full blit of screens[0] as before.
 *
 * fb_mono_xoff (96) and viewwindowx (48) are multiples of 8, so all
 * boundaries fall on byte boundaries in the framebuffer.
 */
void I_FinishUpdate(void)
{
    byte          *src    = screens[0];
    int            xoff   = fb_mono_xoff;
    int            yoff   = fb_mono_yoff;
    int            y, x;
    boolean is_direct;

    is_direct = (gamestate == GS_LEVEL && !automapactive && gametic > 0
                 && fb_mono_base != NULL && !wipe_in_progress);

    for (y = 0; y < SCREENHEIGHT; y++) {
        const byte    *src_row  = src + y * SCREENWIDTH;
        unsigned char *dst_row  = (unsigned char *)(fb_mono_base + (y + yoff) * fb_mono_rowbytes);
        unsigned char *dst      = dst_row + (xoff >> 3);
        const byte    *bayer_r  = bayer4x4[y & 3];

        if (is_direct) {
            int in_vy = (y >= viewwindowy && y < viewwindowy + viewheight);
            /* view_x_start / view_x_end in SCREENWIDTH coords, 8-aligned */
            int vx0 = viewwindowx;
            int vx1 = viewwindowx + scaledviewwidth;

            for (x = 0; x < SCREENWIDTH; x += 8) {
                if (in_vy && x >= vx0 && x < vx1) {
                    /* View area: clear overlay pixels (solid white) over direct render.
                     * screens[0] view area was pre-zeroed; non-zero = HUD/menu overlay.
                     * blit8_black returns 0 for all-zero input, so the direct-rendered
                     * 1-bit content is preserved when there is no overlay. */
                    *dst &= ~blit8_black(src_row + x);
                } else {
                    /* Non-view area: always blit from screens[0] */
                    *dst = blit8(src_row + x, bayer_r, x);
                }
                dst++;
            }
        } else {
            /* Standard mode (menu, wipe, etc.): full blit */
            for (x = 0; x < SCREENWIDTH; x += 8) {
                *dst++ = blit8(src_row + x, bayer_r, x);
            }
        }
    }

    /* Apply menu overlay (screens[1]) as solid white over the full screen.
     * M_Drawer redirects its output to screens[1], keeping menu pixels cleanly
     * separated from the border tiles and status bar in screens[0].  Any
     * non-zero pixel in screens[1] is forced white (clear bit) so menu text
     * is consistently bright regardless of what is underneath it. */
    if (menu_overlay_buf) {
        byte          *overlay = menu_overlay_buf;
        for (y = 0; y < SCREENHEIGHT; y++) {
            const byte    *ovr_row = overlay + y * SCREENWIDTH;
            unsigned char *dst_row = (unsigned char *)(fb_mono_base + (y + yoff) * fb_mono_rowbytes);
            unsigned char *dst     = dst_row + (xoff >> 3);
            for (x = 0; x < SCREENWIDTH; x += 8) {
                *dst &= ~blit8_black(ovr_row + x);
                dst++;
            }
        }
    }
}

void I_UpdateNoBlit(void)
{
    /* Empty — Doom calls this but we do all blitting in I_FinishUpdate */
}

void I_WaitVBL(int count)
{
    /* Wait for count/70 seconds (original was VGA vblank at 70Hz) */
    /* Use Mac TickCount (60Hz). Approximate. */
    long target = TickCount() + (count * 60 / 70);
    while (TickCount() < target)
        ;
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_BeginRead(void)
{
    /* Disk icon — not needed */
}

void I_EndRead(void)
{
    /* Disk icon — not needed */
}
