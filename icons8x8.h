/* 8x8 pixel icon/sprite system for the LaMetric Time RGBW section (S1, cols 0-7).
 *
 * Each icon is 8 rows x 8 cols, each pixel is 4 bytes: [B, G, R, W].
 * S1 frame layout: byte offset = row * 32 + col * 4, channels [B, G, R, W].
 *
 * Usage:
 *   unsigned char frame[488];
 *   memset(frame, 0, 488);
 *   render_icon("sun", frame);
 *   spi_send_frame(frame, 488);
 */
#ifndef ICONS8X8_H
#define ICONS8X8_H

#include <string.h>

/* ── Color palette (BGRW order) ──────────────────────────────────── */
#define _K  {  0,   0,   0,   0}   /* off / transparent              */
#define _W  {  0,   0,   0, 200}   /* white (W channel only)         */
#define _Wb {  0,   0,   0, 255}   /* bright white                   */
#define _R  {  0,   0, 220,   0}   /* red                            */
#define _Rb {  0,   0, 255,   0}   /* bright red                     */
#define _G  {  0, 220,   0,   0}   /* green                          */
#define _Gb {  0, 255,   0,   0}   /* bright green                   */
#define _B  {220,   0,   0,   0}   /* blue                           */
#define _Bb {255,   0,   0,   0}   /* bright blue                    */
#define _Y  {  0, 200, 255,   0}   /* yellow  (R+G)                  */
#define _Yb {  0, 255, 255,   0}   /* bright yellow                  */
#define _O  {  0, 120, 255,   0}   /* orange  (R + half G)           */
#define _C  {220, 220,   0,   0}   /* cyan    (B+G)                  */
#define _P  {180,   0, 180,   0}   /* purple  (R+B)                  */
#define _Pk {100,   0, 255,   0}   /* pink    (R + some B)           */
#define _Gy {  0,   0,   0,  80}   /* dim grey                       */
#define _Lb {160, 160,   0,   0}   /* light blue (B+some G)          */
#define _Dg {  0,  80,   0,   0}   /* dark green                     */

/* ── Icon type: 8 rows x 8 cols x 4 bytes (BGRW) ────────────────── */
typedef unsigned char icon8x8_t[8][8][4];

/* ================================================================
 *  WEATHER ICONS
 * ================================================================ */

/* SUN: bright yellow circle with rays */
static const icon8x8_t icon_sun = {
    { _K,  _Y,  _K,  _Yb, _Yb, _K,  _Y,  _K  },
    { _Y,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _Y  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _Yb, _Yb, _Yb, _Wb, _Wb, _Yb, _Yb, _Yb },
    { _Yb, _Yb, _Yb, _Wb, _Wb, _Yb, _Yb, _Yb },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _Y,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _Y  },
    { _K,  _Y,  _K,  _Yb, _Yb, _K,  _Y,  _K  },
};

/* CLOUD: white puffy cloud shape */
static const icon8x8_t icon_cloud = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _W,  _W,  _K,  _K,  _K,  _K  },
    { _K,  _W,  _Wb, _Wb, _W,  _K,  _K,  _K  },
    { _K,  _W,  _Wb, _Wb, _Wb, _W,  _W,  _K  },
    { _W,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _W  },
    { _W,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _W  },
    { _K,  _W,  _Wb, _Wb, _Wb, _Wb, _W,  _K  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* RAIN: small cloud top with blue rain drops below */
static const icon8x8_t icon_rain = {
    { _K,  _K,  _W,  _W,  _K,  _K,  _K,  _K  },
    { _K,  _W,  _Wb, _Wb, _W,  _W,  _K,  _K  },
    { _W,  _Wb, _Wb, _Wb, _Wb, _Wb, _W,  _K  },
    { _K,  _W,  _W,  _W,  _W,  _W,  _K,  _K  },
    { _K,  _Bb, _K,  _Bb, _K,  _Bb, _K,  _K  },
    { _K,  _K,  _Bb, _K,  _Bb, _K,  _Bb, _K  },
    { _K,  _Bb, _K,  _Bb, _K,  _Bb, _K,  _K  },
    { _K,  _K,  _Bb, _K,  _Bb, _K,  _K,  _K  },
};

/* SNOW: cloud top with white snowflakes below */
static const icon8x8_t icon_snow = {
    { _K,  _K,  _W,  _W,  _K,  _K,  _K,  _K  },
    { _K,  _W,  _Wb, _Wb, _W,  _W,  _K,  _K  },
    { _W,  _Wb, _Wb, _Wb, _Wb, _Wb, _W,  _K  },
    { _K,  _W,  _W,  _W,  _W,  _W,  _K,  _K  },
    { _K,  _Wb, _K,  _K,  _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _K,  _K,  _Wb, _K  },
    { _K,  _K,  _Wb, _K,  _K,  _Wb, _K,  _K  },
    { _K,  _Wb, _K,  _K,  _Wb, _K,  _K,  _K  },
};

/* THUNDER: cloud with yellow lightning bolt */
static const icon8x8_t icon_thunder = {
    { _K,  _K,  _W,  _W,  _K,  _K,  _K,  _K  },
    { _K,  _W,  _Wb, _Wb, _W,  _W,  _K,  _K  },
    { _W,  _Wb, _Wb, _Wb, _Wb, _Wb, _W,  _K  },
    { _K,  _W,  _W,  _W,  _W,  _W,  _K,  _K  },
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
    { _K,  _K,  _Yb, _Yb, _K,  _K,  _K,  _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Yb, _K,  _K,  _K,  _K  },
};

/* ================================================================
 *  STATUS ICONS
 * ================================================================ */

/* CHECK: green checkmark */
static const icon8x8_t icon_check = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _Gb, _K  },
    { _K,  _K,  _K,  _K,  _K,  _Gb, _Gb, _K  },
    { _K,  _K,  _K,  _K,  _Gb, _Gb, _K,  _K  },
    { _Gb, _K,  _K,  _Gb, _Gb, _K,  _K,  _K  },
    { _Gb, _Gb, _Gb, _Gb, _K,  _K,  _K,  _K  },
    { _K,  _Gb, _Gb, _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* X_MARK: red X */
static const icon8x8_t icon_x_mark = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _Rb, _Rb, _K,  _K,  _K,  _K,  _Rb, _Rb },
    { _K,  _Rb, _Rb, _K,  _K,  _Rb, _Rb, _K  },
    { _K,  _K,  _Rb, _Rb, _Rb, _Rb, _K,  _K  },
    { _K,  _K,  _Rb, _Rb, _Rb, _Rb, _K,  _K  },
    { _K,  _Rb, _Rb, _K,  _K,  _Rb, _Rb, _K  },
    { _Rb, _Rb, _K,  _K,  _K,  _K,  _Rb, _Rb },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* WARNING: yellow triangle with exclamation */
static const icon8x8_t icon_warning = {
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
    { _K,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K  },
    { _K,  _K,  _Yb, _R,  _R,  _Yb, _K,  _K  },
    { _K,  _Yb, _Yb, _R,  _R,  _Yb, _Yb, _K  },
    { _K,  _Yb, _Yb, _R,  _R,  _Yb, _Yb, _K  },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
    { _Yb, _Yb, _Yb, _R,  _R,  _Yb, _Yb, _Yb },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
};

/* INFO: blue circle with white "i" */
static const icon8x8_t icon_info = {
    { _K,  _K,  _Bb, _Bb, _Bb, _Bb, _K,  _K  },
    { _K,  _Bb, _Bb, _Bb, _Bb, _Bb, _Bb, _K  },
    { _Bb, _Bb, _Bb, _Wb, _Wb, _Bb, _Bb, _Bb },
    { _Bb, _Bb, _Bb, _Bb, _Bb, _Bb, _Bb, _Bb },
    { _Bb, _Bb, _Bb, _Wb, _Wb, _Bb, _Bb, _Bb },
    { _Bb, _Bb, _Bb, _Wb, _Wb, _Bb, _Bb, _Bb },
    { _K,  _Bb, _Bb, _Wb, _Wb, _Bb, _Bb, _K  },
    { _K,  _K,  _Bb, _Bb, _Bb, _Bb, _K,  _K  },
};

/* HEART: red heart shape */
static const icon8x8_t icon_heart = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _Rb, _Rb, _K,  _K,  _Rb, _Rb, _K  },
    { _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb },
    { _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb },
    { _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _Rb },
    { _K,  _Rb, _Rb, _Rb, _Rb, _Rb, _Rb, _K  },
    { _K,  _K,  _Rb, _Rb, _Rb, _Rb, _K,  _K  },
    { _K,  _K,  _K,  _Rb, _Rb, _K,  _K,  _K  },
};

/* ================================================================
 *  ARROW ICONS
 * ================================================================ */

/* UP arrow: white upward arrow */
static const icon8x8_t icon_up = {
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _Wb, _Wb, _Wb, _Wb, _K,  _K  },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _Wb, _Wb, _K,  _Wb, _Wb, _K,  _Wb, _Wb },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
};

/* DOWN arrow: white downward arrow */
static const icon8x8_t icon_down = {
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _Wb, _Wb, _K,  _Wb, _Wb, _K,  _Wb, _Wb },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _K,  _K,  _Wb, _Wb, _Wb, _Wb, _K,  _K  },
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
};

/* LEFT arrow: white leftward arrow */
static const icon8x8_t icon_left = {
    { _K,  _K,  _K,  _Wb, _K,  _K,  _K,  _K  },
    { _K,  _K,  _Wb, _Wb, _K,  _K,  _K,  _K  },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _K,  _K,  _Wb, _Wb, _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _Wb, _K,  _K,  _K,  _K  },
};

/* RIGHT arrow: white rightward arrow */
static const icon8x8_t icon_right = {
    { _K,  _K,  _K,  _K,  _Wb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _K,  _Wb, _Wb, _K,  _K  },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _K,  _K,  _K,  _K,  _Wb, _Wb, _K,  _K  },
    { _K,  _K,  _K,  _K,  _Wb, _K,  _K,  _K  },
};

/* TREND_UP: diagonal arrow going up-right (green) */
static const icon8x8_t icon_trend_up = {
    { _K,  _K,  _K,  _Gb, _Gb, _Gb, _Gb, _Gb },
    { _K,  _K,  _K,  _K,  _K,  _Gb, _Gb, _Gb },
    { _K,  _K,  _K,  _K,  _Gb, _Gb, _Gb, _K  },
    { _K,  _K,  _K,  _Gb, _Gb, _K,  _K,  _K  },
    { _K,  _K,  _Gb, _Gb, _K,  _K,  _K,  _K  },
    { _K,  _Gb, _Gb, _K,  _K,  _K,  _K,  _K  },
    { _Gb, _Gb, _K,  _K,  _K,  _K,  _K,  _K  },
    { _Gb, _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* TREND_DOWN: diagonal arrow going down-right (red) */
static const icon8x8_t icon_trend_down = {
    { _Rb, _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _Rb, _Rb, _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _Rb, _Rb, _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _Rb, _Rb, _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _Rb, _Rb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _K,  _Rb, _Rb, _Rb, _K  },
    { _K,  _K,  _K,  _K,  _K,  _Rb, _Rb, _Rb },
    { _K,  _K,  _K,  _Rb, _Rb, _Rb, _Rb, _Rb },
};

/* ================================================================
 *  MISC ICONS
 * ================================================================ */

/* CLOCK: white clock face with hands */
static const icon8x8_t icon_clock = {
    { _K,  _K,  _C,  _C,  _C,  _C,  _K,  _K  },
    { _K,  _C,  _K,  _K,  _Wb, _K,  _C,  _K  },
    { _C,  _K,  _K,  _K,  _Wb, _K,  _K,  _C  },
    { _C,  _K,  _K,  _K,  _Wb, _K,  _K,  _C  },
    { _C,  _K,  _K,  _K,  _Wb, _Wb, _K,  _C  },
    { _C,  _K,  _K,  _K,  _K,  _K,  _K,  _C  },
    { _K,  _C,  _K,  _K,  _K,  _K,  _C,  _K  },
    { _K,  _K,  _C,  _C,  _C,  _C,  _K,  _K  },
};

/* MUSIC: green music note */
static const icon8x8_t icon_music = {
    { _K,  _K,  _K,  _Gb, _Gb, _Gb, _Gb, _K  },
    { _K,  _K,  _K,  _Gb, _K,  _K,  _Gb, _K  },
    { _K,  _K,  _K,  _Gb, _K,  _K,  _Gb, _K  },
    { _K,  _K,  _K,  _Gb, _K,  _K,  _Gb, _K  },
    { _K,  _K,  _K,  _Gb, _K,  _K,  _Gb, _K  },
    { _K,  _Gb, _Gb, _Gb, _Gb, _Gb, _Gb, _K  },
    { _Gb, _Gb, _Gb, _Gb, _Gb, _Gb, _Gb, _Gb },
    { _K,  _Gb, _Gb, _K,  _K,  _Gb, _Gb, _K  },
};

/* MAIL: white envelope */
static const icon8x8_t icon_mail = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _Wb, _Wb, _W,  _W,  _W,  _W,  _Wb, _Wb },
    { _Wb, _W,  _Wb, _W,  _W,  _Wb, _W,  _Wb },
    { _Wb, _W,  _W,  _Wb, _Wb, _W,  _W,  _Wb },
    { _Wb, _W,  _W,  _W,  _W,  _W,  _W,  _Wb },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* BELL: yellow notification bell */
static const icon8x8_t icon_bell = {
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
    { _K,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
};

/* CHART: bar chart with ascending bars (cyan) */
static const icon8x8_t icon_chart = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _C  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _C  },
    { _K,  _K,  _K,  _K,  _K,  _C,  _K,  _C  },
    { _K,  _K,  _K,  _K,  _K,  _C,  _K,  _C  },
    { _K,  _K,  _K,  _C,  _K,  _C,  _K,  _C  },
    { _K,  _C,  _K,  _C,  _K,  _C,  _K,  _C  },
    { _K,  _C,  _K,  _C,  _K,  _C,  _K,  _C  },
    { _Gy, _Gy, _Gy, _Gy, _Gy, _Gy, _Gy, _Gy },
};

/* STAR: yellow 5-pointed star */
static const icon8x8_t icon_star = {
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
    { _K,  _K,  _K,  _Yb, _Yb, _K,  _K,  _K  },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _K,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K  },
    { _K,  _Yb, _Yb, _K,  _K,  _Yb, _Yb, _K  },
    { _Yb, _Yb, _K,  _K,  _K,  _K,  _Yb, _Yb },
};

/* WIFI: cyan wifi signal arcs */
static const icon8x8_t icon_wifi = {
    { _K,  _K,  _C,  _C,  _C,  _C,  _K,  _K  },
    { _K,  _C,  _K,  _K,  _K,  _K,  _C,  _K  },
    { _C,  _K,  _K,  _C,  _C,  _K,  _K,  _C  },
    { _K,  _K,  _C,  _K,  _K,  _C,  _K,  _K  },
    { _K,  _K,  _K,  _C,  _C,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _C,  _C,  _K,  _K,  _K  },
    { _K,  _K,  _K,  _C,  _C,  _K,  _K,  _K  },
};

/* BATTERY: green battery icon */
static const icon8x8_t icon_battery = {
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _K,  _Wb, _Gb, _Gb, _Gb, _Gb, _Wb, _Wb },
    { _K,  _Wb, _Gb, _Gb, _Gb, _Gb, _Wb, _Wb },
    { _K,  _Wb, _Gb, _Gb, _Gb, _Gb, _Wb, _Wb },
    { _K,  _Wb, _Gb, _Gb, _Gb, _Gb, _Wb, _Wb },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _K,  _K,  _K,  _K,  _K,  _K,  _K,  _K  },
};

/* HOME: white house shape */
static const icon8x8_t icon_home = {
    { _K,  _K,  _K,  _Wb, _Wb, _K,  _K,  _K  },
    { _K,  _K,  _Wb, _Wb, _Wb, _Wb, _K,  _K  },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _Wb },
    { _K,  _Wb, _Wb, _Wb, _Wb, _Wb, _Wb, _K  },
    { _K,  _Wb, _Wb, _K,  _K,  _Wb, _Wb, _K  },
    { _K,  _Wb, _Wb, _K,  _K,  _Wb, _Wb, _K  },
    { _K,  _Wb, _Wb, _K,  _K,  _Wb, _Wb, _K  },
};

/* FIRE: orange/red flame */
static const icon8x8_t icon_fire = {
    { _K,  _K,  _K,  _O,  _K,  _K,  _K,  _K  },
    { _K,  _K,  _O,  _O,  _K,  _O,  _K,  _K  },
    { _K,  _K,  _O,  _O,  _O,  _O,  _K,  _K  },
    { _K,  _O,  _Rb, _O,  _Rb, _O,  _O,  _K  },
    { _K,  _O,  _Rb, _Yb, _Rb, _O,  _O,  _K  },
    { _O,  _Rb, _Yb, _Yb, _Yb, _Rb, _O,  _K  },
    { _O,  _Rb, _Rb, _Yb, _Rb, _Rb, _O,  _K  },
    { _K,  _O,  _Rb, _Rb, _Rb, _O,  _K,  _K  },
};

/* SMILE: yellow smiley face */
static const icon8x8_t icon_smile = {
    { _K,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K  },
    { _K,  _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _K  },
    { _Yb, _Yb, _K,  _Yb, _Yb, _K,  _Yb, _Yb },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
    { _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb, _Yb },
    { _Yb, _K,  _Yb, _Yb, _Yb, _Yb, _K,  _Yb },
    { _K,  _Yb, _K,  _K,  _K,  _K,  _Yb, _K  },
    { _K,  _K,  _Yb, _Yb, _Yb, _Yb, _K,  _K  },
};

/* ================================================================
 *  ICON REGISTRY — name-to-data mapping
 * ================================================================ */

typedef struct {
    const char      *name;
    const icon8x8_t *data;
} icon_entry_t;

#define ICON_COUNT 22

static const icon_entry_t icon_registry[ICON_COUNT] = {
    /* weather */
    { "sun",        &icon_sun        },
    { "cloud",      &icon_cloud      },
    { "rain",       &icon_rain       },
    { "snow",       &icon_snow       },
    { "thunder",    &icon_thunder    },
    /* status */
    { "check",      &icon_check      },
    { "x_mark",     &icon_x_mark     },
    { "warning",    &icon_warning    },
    { "info",       &icon_info       },
    { "heart",      &icon_heart      },
    /* arrows */
    { "up",         &icon_up         },
    { "down",       &icon_down       },
    { "left",       &icon_left       },
    { "right",      &icon_right      },
    { "trend_up",   &icon_trend_up   },
    { "trend_down", &icon_trend_down },
    /* misc */
    { "clock",      &icon_clock      },
    { "music",      &icon_music      },
    { "mail",       &icon_mail       },
    { "bell",       &icon_bell       },
    { "chart",      &icon_chart      },
    { "star",       &icon_star       },
};

/* Array of all icon names (null-terminated) for enumeration */
static const char *icon_names[ICON_COUNT + 1] = {
    "sun", "cloud", "rain", "snow", "thunder",
    "check", "x_mark", "warning", "info", "heart",
    "up", "down", "left", "right", "trend_up", "trend_down",
    "clock", "music", "mail", "bell", "chart", "star",
    NULL
};

/* Additional icons accessible but not in the default 22 list */
static const icon_entry_t icon_extras[] = {
    { "wifi",       &icon_wifi       },
    { "battery",    &icon_battery    },
    { "home",       &icon_home       },
    { "fire",       &icon_fire       },
    { "smile",      &icon_smile      },
    { NULL,         NULL             },
};

/* ================================================================
 *  RENDER FUNCTION
 * ================================================================ */

/* Look up an icon by name. Returns pointer to icon data, or NULL. */
static const icon8x8_t *find_icon(const char *name) {
    int i;
    if (!name) return NULL;

    /* Search main registry */
    for (i = 0; i < ICON_COUNT; i++) {
        if (strcmp(name, icon_registry[i].name) == 0)
            return icon_registry[i].data;
    }

    /* Search extras */
    for (i = 0; icon_extras[i].name != NULL; i++) {
        if (strcmp(name, icon_extras[i].name) == 0)
            return icon_extras[i].data;
    }

    return NULL;
}

/* Render an icon into the S1 section of a 488-byte frame buffer.
 *
 * S1 layout: 8 rows x 8 cols, stride 32 bytes/row, 4 bytes/pixel [B,G,R,W].
 * Byte offset for pixel (row, col) = row * 32 + col * 4.
 *
 * Returns 0 on success, -1 if icon not found.
 */
static int render_icon(const char *name, unsigned char frame[488]) {
    const icon8x8_t *icon;
    int row, col, idx;

    icon = find_icon(name);
    if (!icon) return -1;

    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            idx = row * 32 + col * 4;
            frame[idx + 0] = (*icon)[row][col][0];   /* B */
            frame[idx + 1] = (*icon)[row][col][1];   /* G */
            frame[idx + 2] = (*icon)[row][col][2];   /* R */
            frame[idx + 3] = (*icon)[row][col][3];   /* W */
        }
    }

    return 0;
}

/* Render an icon at a custom brightness scale (0-255).
 * The icon pixel values are scaled proportionally. */
static int render_icon_brightness(const char *name, unsigned char frame[488],
                                  unsigned char brightness) {
    const icon8x8_t *icon;
    int row, col, idx, ch;

    icon = find_icon(name);
    if (!icon) return -1;

    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            idx = row * 32 + col * 4;
            for (ch = 0; ch < 4; ch++) {
                unsigned int v = (*icon)[row][col][ch];
                frame[idx + ch] = (unsigned char)((v * brightness) / 255);
            }
        }
    }

    return 0;
}

/* Render raw icon data (any icon8x8_t) directly into the frame.
 * Useful for custom/generated icons or animation frames. */
static void render_icon_raw(const icon8x8_t *icon, unsigned char frame[488]) {
    int row, col, idx;
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            idx = row * 32 + col * 4;
            frame[idx + 0] = (*icon)[row][col][0];
            frame[idx + 1] = (*icon)[row][col][1];
            frame[idx + 2] = (*icon)[row][col][2];
            frame[idx + 3] = (*icon)[row][col][3];
        }
    }
}

/* Clear only the S1 icon area (bytes 0-255) in the frame buffer. */
static void clear_icon_area(unsigned char frame[488]) {
    int row, col, idx;
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            idx = row * 32 + col * 4;
            frame[idx + 0] = 0;
            frame[idx + 1] = 0;
            frame[idx + 2] = 0;
            frame[idx + 3] = 0;
        }
    }
}

/* Get the total number of available icons (registry + extras). */
static int icon_total_count(void) {
    int n = ICON_COUNT;
    int i;
    for (i = 0; icon_extras[i].name != NULL; i++) n++;
    return n;
}

#endif /* ICONS8X8_H */
