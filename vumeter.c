/*
    Volume Meter plugin for the DeaDBeeF audio player

    Copyright (C) 2014 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    Based on DeaDBeeFs stock vumeter.
    Copyright (c) 2009-2014 Alexey Yakovenko <waker@users.sourceforge.net>
    Copyright (c) 2011 William Pitcock <nenolod@dereferenced.org>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <gtk/gtk.h>

#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

#include "fastftoi.h"
#include "support.h"

#define REFRESH_INTERVAL 25
#define MAX_CHANNELS 6
#define GRADIENT_TABLE_SIZE 1024

#define     STR_GRADIENT_VERTICAL "Vertical"
#define     STR_GRADIENT_HORIZONTAL "Horizontal"

#define     CONFSTR_MS_REFRESH_INTERVAL       "vu_meter.refresh_interval"
#define     CONFSTR_MS_DB_RANGE               "vu_meter.db_range"
#define     CONFSTR_MS_ENABLE_HGRID           "vu_meter.enable_hgrid"
#define     CONFSTR_MS_ENABLE_VGRID           "vu_meter.enable_vgrid"
#define     CONFSTR_MS_ENABLE_BAR_MODE        "vu_meter.enable_bar_mode"
#define     CONFSTR_MS_BAR_FALLOFF            "vu_meter.bar_falloff"
#define     CONFSTR_MS_BAR_DELAY              "vu_meter.bar_delay"
#define     CONFSTR_MS_PEAK_FALLOFF           "vu_meter.peak_falloff"
#define     CONFSTR_MS_PEAK_DELAY             "vu_meter.peak_delay"
#define     CONFSTR_MS_GRADIENT_ORIENTATION   "vu_meter.gradient_orientation"
#define     CONFSTR_MS_COLOR_BG               "vu_meter.color.background"
#define     CONFSTR_MS_COLOR_VGRID            "vu_meter.color.vgrid"
#define     CONFSTR_MS_COLOR_HGRID            "vu_meter.color.hgrid"
#define     CONFSTR_MS_COLOR_GRADIENT_00      "vu_meter.color.gradient_00"
#define     CONFSTR_MS_COLOR_GRADIENT_01      "vu_meter.color.gradient_01"
#define     CONFSTR_MS_COLOR_GRADIENT_02      "vu_meter.color.gradient_02"
#define     CONFSTR_MS_COLOR_GRADIENT_03      "vu_meter.color.gradient_03"
#define     CONFSTR_MS_COLOR_GRADIENT_04      "vu_meter.color.gradient_04"
#define     CONFSTR_MS_COLOR_GRADIENT_05      "vu_meter.color.gradient_05"

/* Global variables */
static DB_misc_t            plugin;
static DB_functions_t *     deadbeef = NULL;
static ddb_gtkui_t *        gtkui_plugin = NULL;

typedef struct {
    ddb_gtkui_widget_t base;
    GtkWidget *drawarea;
    GtkWidget *popup;
    GtkWidget *popup_item;
    cairo_surface_t *surf;
    guint drawtimer;
    uint32_t colors[GRADIENT_TABLE_SIZE];
    float data[MAX_CHANNELS];
    float bars[MAX_CHANNELS];
    int delay[MAX_CHANNELS];
    float peaks[MAX_CHANNELS];
    int delay_peak[MAX_CHANNELS];
    int channels;
    intptr_t mutex;
} w_vumeter_t;

static int CONFIG_REFRESH_INTERVAL = 25;
static int CONFIG_DB_RANGE = 70;
static int CONFIG_ENABLE_HGRID = 1;
static int CONFIG_ENABLE_VGRID = 1;
static int CONFIG_ENABLE_BAR_MODE = 0;
static int CONFIG_BAR_FALLOFF = -1;
static int CONFIG_BAR_DELAY = 0;
static int CONFIG_PEAK_FALLOFF = 90;
static int CONFIG_PEAK_DELAY = 500;
static int CONFIG_GRADIENT_ORIENTATION = 0;
static int CONFIG_NUM_COLORS = 6;
static GdkColor CONFIG_COLOR_BG;
static GdkColor CONFIG_COLOR_VGRID;
static GdkColor CONFIG_COLOR_HGRID;
static GdkColor CONFIG_GRADIENT_COLORS[6];
static uint32_t CONFIG_COLOR_BG32 = 0xff222222;
static uint32_t CONFIG_COLOR_VGRID32 = 0xff000000;
static uint32_t CONFIG_COLOR_HGRID32 = 0xff666666;

static void
save_config (void)
{
    deadbeef->conf_set_int (CONFSTR_MS_REFRESH_INTERVAL,            CONFIG_REFRESH_INTERVAL);
    deadbeef->conf_set_int (CONFSTR_MS_DB_RANGE,                    CONFIG_DB_RANGE);
    deadbeef->conf_set_int (CONFSTR_MS_ENABLE_HGRID,                CONFIG_ENABLE_HGRID);
    deadbeef->conf_set_int (CONFSTR_MS_ENABLE_VGRID,                CONFIG_ENABLE_VGRID);
    deadbeef->conf_set_int (CONFSTR_MS_ENABLE_BAR_MODE,             CONFIG_ENABLE_BAR_MODE);
    deadbeef->conf_set_int (CONFSTR_MS_BAR_FALLOFF,                 CONFIG_BAR_FALLOFF);
    deadbeef->conf_set_int (CONFSTR_MS_BAR_DELAY,                   CONFIG_BAR_DELAY);
    deadbeef->conf_set_int (CONFSTR_MS_PEAK_FALLOFF,                CONFIG_PEAK_FALLOFF);
    deadbeef->conf_set_int (CONFSTR_MS_PEAK_DELAY,                  CONFIG_PEAK_DELAY);
    deadbeef->conf_set_int (CONFSTR_MS_GRADIENT_ORIENTATION,        CONFIG_GRADIENT_ORIENTATION);
    char color[100];
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_COLOR_BG.red, CONFIG_COLOR_BG.green, CONFIG_COLOR_BG.blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_BG, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_COLOR_VGRID.red, CONFIG_COLOR_VGRID.green, CONFIG_COLOR_VGRID.blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_VGRID, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_COLOR_HGRID.red, CONFIG_COLOR_HGRID.green, CONFIG_COLOR_HGRID.blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_HGRID, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[0].red, CONFIG_GRADIENT_COLORS[0].green, CONFIG_GRADIENT_COLORS[0].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_00, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[1].red, CONFIG_GRADIENT_COLORS[1].green, CONFIG_GRADIENT_COLORS[1].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_01, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[2].red, CONFIG_GRADIENT_COLORS[2].green, CONFIG_GRADIENT_COLORS[2].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_02, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[3].red, CONFIG_GRADIENT_COLORS[3].green, CONFIG_GRADIENT_COLORS[3].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_03, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[4].red, CONFIG_GRADIENT_COLORS[4].green, CONFIG_GRADIENT_COLORS[4].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_04, color);
    snprintf (color, sizeof (color), "%d %d %d", CONFIG_GRADIENT_COLORS[5].red, CONFIG_GRADIENT_COLORS[5].green, CONFIG_GRADIENT_COLORS[5].blue);
    deadbeef->conf_set_str (CONFSTR_MS_COLOR_GRADIENT_05, color);
}

static void
load_config (void)
{
    deadbeef->conf_lock ();
    CONFIG_GRADIENT_ORIENTATION = deadbeef->conf_get_int (CONFSTR_MS_GRADIENT_ORIENTATION,   0);
    CONFIG_DB_RANGE = deadbeef->conf_get_int (CONFSTR_MS_DB_RANGE,                          70);
    CONFIG_ENABLE_HGRID = deadbeef->conf_get_int (CONFSTR_MS_ENABLE_HGRID,                   1);
    CONFIG_ENABLE_VGRID = deadbeef->conf_get_int (CONFSTR_MS_ENABLE_VGRID,                   1);
    CONFIG_ENABLE_BAR_MODE = deadbeef->conf_get_int (CONFSTR_MS_ENABLE_BAR_MODE,             0);
    CONFIG_REFRESH_INTERVAL = deadbeef->conf_get_int (CONFSTR_MS_REFRESH_INTERVAL,          25);
    CONFIG_BAR_FALLOFF = deadbeef->conf_get_int (CONFSTR_MS_BAR_FALLOFF,                    -1);
    CONFIG_BAR_DELAY = deadbeef->conf_get_int (CONFSTR_MS_BAR_DELAY,                         0);
    CONFIG_PEAK_FALLOFF = deadbeef->conf_get_int (CONFSTR_MS_PEAK_FALLOFF,                  90);
    CONFIG_PEAK_DELAY = deadbeef->conf_get_int (CONFSTR_MS_PEAK_DELAY,                     500);
    const char *color;
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_BG,                   "8738 8738 8738");
    sscanf (color, "%hd %hd %hd", &CONFIG_COLOR_BG.red, &CONFIG_COLOR_BG.green, &CONFIG_COLOR_BG.blue);
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_VGRID,                         "0 0 0");
    sscanf (color, "%hd %hd %hd", &CONFIG_COLOR_VGRID.red, &CONFIG_COLOR_VGRID.green, &CONFIG_COLOR_VGRID.blue);
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_HGRID,             "26214 26214 26214");
    sscanf (color, "%hd %hd %hd", &CONFIG_COLOR_HGRID.red, &CONFIG_COLOR_HGRID.green, &CONFIG_COLOR_HGRID.blue);
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_00,        "65535 0 0");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[0].red), &(CONFIG_GRADIENT_COLORS[0].green), &(CONFIG_GRADIENT_COLORS[0].blue));
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_01,      "65535 32896 0");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[1].red), &(CONFIG_GRADIENT_COLORS[1].green), &(CONFIG_GRADIENT_COLORS[1].blue));
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_02,      "65535 65535 0");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[2].red), &(CONFIG_GRADIENT_COLORS[2].green), &(CONFIG_GRADIENT_COLORS[2].blue));
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_03,    "32896 65535 30840");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[3].red), &(CONFIG_GRADIENT_COLORS[3].green), &(CONFIG_GRADIENT_COLORS[3].blue));
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_04,      "0 38036 41120");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[4].red), &(CONFIG_GRADIENT_COLORS[4].green), &(CONFIG_GRADIENT_COLORS[4].blue));
    color = deadbeef->conf_get_str_fast (CONFSTR_MS_COLOR_GRADIENT_05,       "0 8224 25700");
    sscanf (color, "%hd %hd %hd", &(CONFIG_GRADIENT_COLORS[5].red), &(CONFIG_GRADIENT_COLORS[5].green), &(CONFIG_GRADIENT_COLORS[5].blue));

    float scale = 255/65535.f;
    CONFIG_COLOR_BG32 = ((uint32_t)(CONFIG_COLOR_BG.red * scale) & 0xFF) << 16 |
                        ((uint32_t)(CONFIG_COLOR_BG.green * scale) & 0xFF) << 8 |
                        ((uint32_t)(CONFIG_COLOR_BG.blue * scale) & 0xFF) << 0;

    CONFIG_COLOR_VGRID32 = ((uint32_t)(CONFIG_COLOR_VGRID.red * scale) & 0xFF) << 16 |
                        ((uint32_t)(CONFIG_COLOR_VGRID.green * scale) & 0xFF) << 8 |
                        ((uint32_t)(CONFIG_COLOR_VGRID.blue * scale) & 0xFF) << 0;

    CONFIG_COLOR_HGRID32 = ((uint32_t)(CONFIG_COLOR_HGRID.red * scale) & 0xFF) << 16 |
                        ((uint32_t)(CONFIG_COLOR_HGRID.green * scale) & 0xFF) << 8 |
                        ((uint32_t)(CONFIG_COLOR_HGRID.blue * scale) & 0xFF) << 0;
    deadbeef->conf_unlock ();
}

static inline void
_memset_pattern (char *data, const void* pattern, size_t data_len, size_t pattern_len)
{
    memmove ((char *)data, pattern, pattern_len);
    char *start = (char *)data;
    char *current = (char *)data + pattern_len;
    char *end = start + data_len;
    while(current + pattern_len < end) {
        memmove (current, start, pattern_len);
        current += pattern_len;
        pattern_len *= 2;
    }
    memmove (current, start, end-current);
}

static inline void
_draw_vline (uint8_t *data, int stride, int x0, int y0, int y1, uint32_t color) {
    if (y0 > y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
        y1--;
    }
    else if (y0 < y1) {
        y0++;
    }
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        *ptr = color;
        ptr += stride/4;
        y0++;
    }
}

static inline void
_draw_hline (uint8_t *data, int stride, int x0, int y0, int x1, uint32_t color) {
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (x0 <= x1) {
        *ptr++ = color;
        x0++;
    }
}

static inline void
_draw_background (uint8_t *data, int w, int h, uint32_t color)
{
    size_t fillLen = w * h * sizeof (uint32_t);
    _memset_pattern ((char *)data, &color, fillLen, sizeof (uint32_t));
}

static inline void
_draw_bar (uint8_t *data, int stride, int x0, int y0, int w, int h, uint32_t color) {
    int y1 = y0+h-1;
    int x1 = x0+w-1;
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        int x = x0;
        while (x++ <= x1) {
            *ptr++ = color;
        }
        y0++;
        ptr += stride/4-w;
    }
}

static inline void
_draw_bar_gradient_v (gpointer user_data, uint8_t *data, int stride, int x0, int y0, int w, int h, int total_h) {
    w_vumeter_t *s = user_data;
    int y1 = y0+h-1;
    int x1 = x0+w-1;
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        int x = x0;
        int index = ftoi(((double)y0/(double)total_h) * (GRADIENT_TABLE_SIZE - 1));
        index = CLAMP (index, 0, GRADIENT_TABLE_SIZE - 1);
        while (x++ <= x1) {
            *ptr++ = s->colors[index];
        }
        y0++;
        ptr += stride/4-w;
    }
}

static inline void
_draw_bar_gradient_h (gpointer user_data, uint8_t *data, int stride, int x0, int y0, int w, int h, int total_w) {
    w_vumeter_t *s = user_data;
    int y1 = y0+h-1;
    int x1 = x0+w-1;
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        int x = x0;
        while (x++ <= x1) {
            int index = ftoi(((double)x/(double)total_w) * (GRADIENT_TABLE_SIZE - 1));
            index = CLAMP (index, 0, GRADIENT_TABLE_SIZE - 1);
            *ptr++ = s->colors[index];
        }
        y0++;
        ptr += stride/4-w;
    }
}

static inline void
_draw_bar_gradient_bar_mode_v (gpointer user_data, uint8_t *data, int stride, int x0, int y0, int w, int h, int total_h) {
    w_vumeter_t *s = user_data;
    int y1 = y0+h-1;
    int x1 = x0+w-1;
    y0 -= y0 % 2;
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        int x = x0;
        int index = ftoi(((double)y0/(double)total_h) * (GRADIENT_TABLE_SIZE - 1));
        index = CLAMP (index, 0, GRADIENT_TABLE_SIZE - 1);
        while (x++ <= x1) {
            *ptr++ = s->colors[index];
        }
        y0 += 2;
        ptr += stride/2-w;
    }
}

static inline void
_draw_bar_gradient_bar_mode_h (gpointer user_data, uint8_t *data, int stride, int x0, int y0, int w, int h, int total_w) {
    w_vumeter_t *s = user_data;
    int y1 = y0+h-1;
    int x1 = x0+w-1;
    y0 -= y0 % 2;
    uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
    while (y0 <= y1) {
        int x = x0;
        while (x++ <= x1) {
            int index = ftoi(((double)x/(double)total_w) * (GRADIENT_TABLE_SIZE - 1));
            index = CLAMP (index, 0, GRADIENT_TABLE_SIZE - 1);
            *ptr++ = s->colors[index];
        }
        y0 += 2;
        ptr += stride/2-w;
    }
}

/* based on Delphi function by Witold J.Janik */
void
create_gradient_table (gpointer user_data, GdkColor *colors, int num_colors)
{
    w_vumeter_t *w = user_data;

    num_colors -= 1;

    for (int i = 0; i < GRADIENT_TABLE_SIZE; i++) {
        double position = (double)i/GRADIENT_TABLE_SIZE;
        /* if position > 1 then we have repetition of colors it maybe useful    */
        if (position > 1.0) {
            if (position - ftoi (position) == 0.0) {
                position = 1.0;
            }
            else {
                position = position - ftoi (position);
            }
        }

        double m= num_colors * position;
        int n=(int)m; // integer of m
        double f=m-n;  // fraction of m

        w->colors[i] = 0xFF000000;
        float scale = 255/65535.f;
        if (num_colors == 0) {
            w->colors[i] = (uint32_t)(colors[0].red*scale) << 16 |
                (uint32_t)(colors[0].green*scale) << 8 |
                (uint32_t)(colors[0].blue*scale) << 0;
        }
        else if (n < num_colors) {
            w->colors[i] = (uint32_t)((colors[n].red*scale) + f * ((colors[n+1].red*scale)-(colors[n].red*scale))) << 16 |
                (uint32_t)((colors[n].green*scale) + f * ((colors[n+1].green*scale)-(colors[n].green*scale))) << 8 |
                (uint32_t)((colors[n].blue*scale) + f * ((colors[n+1].blue*scale)-(colors[n].blue*scale))) << 0;
        }
        else if (n == num_colors) {
            w->colors[i] = (uint32_t)(colors[n].red*scale) << 16 |
                (uint32_t)(colors[n].green*scale) << 8 |
                (uint32_t)(colors[n].blue*scale) << 0;
        }
        else {
            w->colors[i] = 0xFFFFFFFF;
        }
    }
}

static int
on_config_changed (gpointer user_data, uintptr_t ctx)
{
    w_vumeter_t *w = user_data;
    load_config ();
    return 0;
}

static void
on_button_config (GtkMenuItem *menuitem, gpointer user_data)
{
    GtkWidget *vumeter_properties;
    GtkWidget *config_dialog;
    GtkWidget *vbox01;
    GtkWidget *vbox02;
    GtkWidget *vbox03;
    GtkWidget *vbox04;
    GtkWidget *hbox01;
    GtkWidget *hbox02;
    GtkWidget *valign_01;
    GtkWidget *valign_02;
    GtkWidget *valign_03;
    GtkWidget *color_label;
    GtkWidget *color_frame;
    GtkWidget *color_bg_label;
    GtkWidget *color_bg;
    GtkWidget *color_vgrid_label;
    GtkWidget *color_vgrid;
    GtkWidget *color_hgrid_label;
    GtkWidget *color_hgrid;
    GtkWidget *hseparator_01;
    GtkWidget *color_gradient_00;
    GtkWidget *color_gradient_01;
    GtkWidget *color_gradient_02;
    GtkWidget *color_gradient_03;
    GtkWidget *color_gradient_04;
    GtkWidget *color_gradient_05;
    GtkWidget *num_colors_label;
    GtkWidget *num_colors;
    GtkWidget *hbox03;
    GtkWidget *db_range_label0;
    GtkWidget *db_range;
    GtkWidget *hgrid;
    GtkWidget *vgrid;
    GtkWidget *bar_mode;
    GtkWidget *hbox05;
    GtkWidget *gradient_orientation_label;
    GtkWidget *gradient_orientation;
    GtkWidget *dialog_action_area13;
    GtkWidget *applybutton1;
    GtkWidget *cancelbutton1;
    GtkWidget *okbutton1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    vumeter_properties = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (vumeter_properties), "vumeter Properties");
    gtk_window_set_type_hint (GTK_WINDOW (vumeter_properties), GDK_WINDOW_TYPE_HINT_DIALOG);

    config_dialog = gtk_dialog_get_content_area (GTK_DIALOG (vumeter_properties));
    gtk_widget_show (config_dialog);

    hbox01 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox01);
    gtk_box_pack_start (GTK_BOX (config_dialog), hbox01, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox01), 12);

    color_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_label),"<b>Colors</b>");
    gtk_widget_show (color_label);

    color_frame = gtk_frame_new ("Colors");
    gtk_frame_set_label_widget ((GtkFrame *)color_frame, color_label);
    gtk_frame_set_shadow_type ((GtkFrame *)color_frame, GTK_SHADOW_IN);
    gtk_widget_show (color_frame);
    gtk_box_pack_start (GTK_BOX (hbox01), color_frame, TRUE, FALSE, 0);

    vbox01 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox01);
    gtk_container_add (GTK_CONTAINER (color_frame), vbox01);
    gtk_container_set_border_width (GTK_CONTAINER (vbox01), 12);

    hbox02 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox02);
    gtk_box_pack_start (GTK_BOX (vbox01), hbox02, TRUE, TRUE, 0);

    vbox03 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox03);
    gtk_box_pack_start (GTK_BOX (hbox02), vbox03, TRUE, TRUE, 0);

    vbox04 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox04);
    gtk_box_pack_start (GTK_BOX (hbox02), vbox04, TRUE, TRUE, 0);

    valign_01 = gtk_alignment_new(0, 1, 0, 1);
    gtk_container_add(GTK_CONTAINER(vbox03), valign_01);
    gtk_widget_show (valign_01);

    color_bg_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_bg_label),"Background:");
    gtk_widget_show (color_bg_label);
    gtk_container_add(GTK_CONTAINER(valign_01), color_bg_label);

    color_bg = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_bg, TRUE);
    gtk_widget_show (color_bg);
    gtk_box_pack_start (GTK_BOX (vbox04), color_bg, TRUE, TRUE, 0);

    valign_02 = gtk_alignment_new(0, 1, 0, 1);
    gtk_container_add(GTK_CONTAINER(vbox03), valign_02);
    gtk_widget_show (valign_02);

    color_vgrid_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_vgrid_label),"Vertical grid:");
    gtk_widget_show (color_vgrid_label);
    gtk_container_add(GTK_CONTAINER(valign_02), color_vgrid_label);

    color_vgrid = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_vgrid, TRUE);
    gtk_widget_show (color_vgrid);
    gtk_box_pack_start (GTK_BOX (vbox04), color_vgrid, TRUE, TRUE, 0);

    valign_03 = gtk_alignment_new(0, 1, 0, 1);
    gtk_container_add(GTK_CONTAINER(vbox03), valign_03);
    gtk_widget_show (valign_03);

    color_hgrid_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_hgrid_label),"Horizontal grid:");
    gtk_widget_show (color_hgrid_label);
    gtk_container_add(GTK_CONTAINER(valign_03), color_hgrid_label);

    color_hgrid = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_hgrid, TRUE);
    gtk_widget_show (color_hgrid);
    gtk_box_pack_start (GTK_BOX (vbox04), color_hgrid, TRUE, TRUE, 0);

    hseparator_01 = gtk_hseparator_new ();
    gtk_widget_show (hseparator_01);
    gtk_box_pack_start (GTK_BOX (vbox01), hseparator_01, TRUE, TRUE, 0);

    num_colors_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (num_colors_label),"Number of colors:");
    gtk_widget_show (num_colors_label);
    gtk_box_pack_start (GTK_BOX (vbox01), num_colors_label, FALSE, FALSE, 0);

    num_colors = gtk_spin_button_new_with_range (1,6,1);
    gtk_widget_show (num_colors);
    gtk_box_pack_start (GTK_BOX (vbox01), num_colors, FALSE, FALSE, 0);

    color_gradient_00 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_00, TRUE);
    gtk_widget_show (color_gradient_00);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_00, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_00, -1, 30);

    color_gradient_01 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_01, TRUE);
    gtk_widget_show (color_gradient_01);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_01, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_01, -1, 30);

    color_gradient_02 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_02, TRUE);
    gtk_widget_show (color_gradient_02);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_02, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_02, -1, 30);

    color_gradient_03 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_03, TRUE);
    gtk_widget_show (color_gradient_03);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_03, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_03, -1, 30);

    color_gradient_04 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_04, TRUE);
    gtk_widget_show (color_gradient_04);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_04, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_04, -1, 30);

    color_gradient_05 = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)color_gradient_05, TRUE);
    gtk_widget_show (color_gradient_05);
    gtk_box_pack_start (GTK_BOX (vbox01), color_gradient_05, TRUE, FALSE, 0);
    gtk_widget_set_size_request (color_gradient_05, -1, 30);

    vbox02 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox02);
    gtk_box_pack_start (GTK_BOX (hbox01), vbox02, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox02), 12);

    hbox03 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox03);
    gtk_box_pack_start (GTK_BOX (vbox02), hbox03, FALSE, FALSE, 0);

    db_range_label0 = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (db_range_label0),"dB range:");
    gtk_widget_show (db_range_label0);
    gtk_box_pack_start (GTK_BOX (hbox03), db_range_label0, FALSE, TRUE, 0);

    db_range = gtk_spin_button_new_with_range (50,120,10);
    gtk_widget_show (db_range);
    gtk_box_pack_start (GTK_BOX (hbox03), db_range, TRUE, TRUE, 0);

    hbox05 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox05);
    gtk_box_pack_start (GTK_BOX (vbox02), hbox05, FALSE, FALSE, 0);

    gradient_orientation_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (gradient_orientation_label),"Gradient orientation:");
    gtk_widget_show (gradient_orientation_label);
    gtk_box_pack_start (GTK_BOX (hbox05), gradient_orientation_label, FALSE, TRUE, 0);

    gradient_orientation = gtk_combo_box_text_new ();
    gtk_widget_show (gradient_orientation);
    gtk_box_pack_start (GTK_BOX (hbox05), gradient_orientation, TRUE, TRUE, 0);
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(gradient_orientation), STR_GRADIENT_VERTICAL);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gradient_orientation), STR_GRADIENT_HORIZONTAL);

    hgrid = gtk_check_button_new_with_label ("Horizontal grid");
    gtk_widget_show (hgrid);
    gtk_box_pack_start (GTK_BOX (vbox02), hgrid, FALSE, FALSE, 0);

    vgrid = gtk_check_button_new_with_label ("Vertical grid");
    gtk_widget_show (vgrid);
    gtk_box_pack_start (GTK_BOX (vbox02), vgrid, FALSE, FALSE, 0);

    bar_mode = gtk_check_button_new_with_label ("Bar mode");
    gtk_widget_show (bar_mode);
    gtk_box_pack_start (GTK_BOX (vbox02), bar_mode, FALSE, FALSE, 0);

    dialog_action_area13 = gtk_dialog_get_action_area (GTK_DIALOG (vumeter_properties));
    gtk_widget_show (dialog_action_area13);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area13), GTK_BUTTONBOX_END);

    applybutton1 = gtk_button_new_from_stock ("gtk-apply");
    gtk_widget_show (applybutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (vumeter_properties), applybutton1, GTK_RESPONSE_APPLY);
    gtk_widget_set_can_default (applybutton1, TRUE);

    cancelbutton1 = gtk_button_new_from_stock ("gtk-cancel");
    gtk_widget_show (cancelbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (vumeter_properties), cancelbutton1, GTK_RESPONSE_CANCEL);
    gtk_widget_set_can_default (cancelbutton1, TRUE);

    okbutton1 = gtk_button_new_from_stock ("gtk-ok");
    gtk_widget_show (okbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (vumeter_properties), okbutton1, GTK_RESPONSE_OK);
    gtk_widget_set_can_default (okbutton1, TRUE);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (hgrid), CONFIG_ENABLE_HGRID);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (vgrid), CONFIG_ENABLE_VGRID);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bar_mode), CONFIG_ENABLE_BAR_MODE);
    gtk_combo_box_set_active (GTK_COMBO_BOX (gradient_orientation), CONFIG_GRADIENT_ORIENTATION);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (num_colors), CONFIG_NUM_COLORS);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (db_range), CONFIG_DB_RANGE);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_bg), &CONFIG_COLOR_BG);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_vgrid), &CONFIG_COLOR_VGRID);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_hgrid), &CONFIG_COLOR_HGRID);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_00), &(CONFIG_GRADIENT_COLORS[0]));
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_01), &(CONFIG_GRADIENT_COLORS[1]));
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_02), &(CONFIG_GRADIENT_COLORS[2]));
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_03), &(CONFIG_GRADIENT_COLORS[3]));
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_04), &(CONFIG_GRADIENT_COLORS[4]));
    gtk_color_button_set_color (GTK_COLOR_BUTTON (color_gradient_05), &(CONFIG_GRADIENT_COLORS[5]));

    char text[100];
    for (;;) {
        int response = gtk_dialog_run (GTK_DIALOG (vumeter_properties));
        if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_bg), &CONFIG_COLOR_BG);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_vgrid), &CONFIG_COLOR_VGRID);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_hgrid), &CONFIG_COLOR_HGRID);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_00), &CONFIG_GRADIENT_COLORS[0]);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_01), &CONFIG_GRADIENT_COLORS[1]);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_02), &CONFIG_GRADIENT_COLORS[2]);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_03), &CONFIG_GRADIENT_COLORS[3]);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_04), &CONFIG_GRADIENT_COLORS[4]);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (color_gradient_05), &CONFIG_GRADIENT_COLORS[5]);

            CONFIG_ENABLE_HGRID = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (hgrid));
            CONFIG_ENABLE_VGRID = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (vgrid));
            CONFIG_ENABLE_BAR_MODE = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (bar_mode));
            CONFIG_DB_RANGE = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (db_range));
            CONFIG_NUM_COLORS = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (num_colors));
            switch (CONFIG_NUM_COLORS) {
                case 1:
                    gtk_widget_hide (color_gradient_01);
                    gtk_widget_hide (color_gradient_02);
                    gtk_widget_hide (color_gradient_03);
                    gtk_widget_hide (color_gradient_04);
                    gtk_widget_hide (color_gradient_05);
                    break;
                case 2:
                    gtk_widget_show (color_gradient_01);
                    gtk_widget_hide (color_gradient_02);
                    gtk_widget_hide (color_gradient_03);
                    gtk_widget_hide (color_gradient_04);
                    gtk_widget_hide (color_gradient_05);
                    break;
                case 3:
                    gtk_widget_show (color_gradient_01);
                    gtk_widget_show (color_gradient_02);
                    gtk_widget_hide (color_gradient_03);
                    gtk_widget_hide (color_gradient_04);
                    gtk_widget_hide (color_gradient_05);
                    break;
                case 4:
                    gtk_widget_show (color_gradient_01);
                    gtk_widget_show (color_gradient_02);
                    gtk_widget_show (color_gradient_03);
                    gtk_widget_hide (color_gradient_04);
                    gtk_widget_hide (color_gradient_05);
                    break;
                case 5:
                    gtk_widget_show (color_gradient_01);
                    gtk_widget_show (color_gradient_02);
                    gtk_widget_show (color_gradient_03);
                    gtk_widget_show (color_gradient_04);
                    gtk_widget_hide (color_gradient_05);
                    break;
                case 6:
                    gtk_widget_show (color_gradient_01);
                    gtk_widget_show (color_gradient_02);
                    gtk_widget_show (color_gradient_03);
                    gtk_widget_show (color_gradient_04);
                    gtk_widget_show (color_gradient_05);
                    break;
            }
            snprintf (text, sizeof (text), "%s", gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (gradient_orientation)));
            if (strcmp (text, STR_GRADIENT_VERTICAL) == 0) {
                CONFIG_GRADIENT_ORIENTATION = 0;
            }
            else if (strcmp (text, STR_GRADIENT_HORIZONTAL) == 0) {
                CONFIG_GRADIENT_ORIENTATION = 1;
            }
            else {
                CONFIG_GRADIENT_ORIENTATION = -1;
            }
            save_config ();
            deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
        }
        if (response == GTK_RESPONSE_APPLY) {
            continue;
        }
        break;
    }
    gtk_widget_destroy (vumeter_properties);
#pragma GCC diagnostic pop
    return;
}

///// vumeter vis
void
w_vumeter_destroy (ddb_gtkui_widget_t *w) {
    w_vumeter_t *s = (w_vumeter_t *)w;
    deadbeef->vis_waveform_unlisten (w);
    if (s->drawtimer) {
        g_source_remove (s->drawtimer);
        s->drawtimer = 0;
    }
    if (s->surf) {
        cairo_surface_destroy (s->surf);
        s->surf = NULL;
    }
    if (s->mutex) {
        deadbeef->mutex_free (s->mutex);
        s->mutex = 0;
    }
}

gboolean
w_vumeter_draw_cb (void *data) {
    w_vumeter_t *s = data;
    gtk_widget_queue_draw (s->drawarea);
    return TRUE;
}

static void
vumeter_wavedata_listener (void *ctx, ddb_audio_data_t *data) {
    w_vumeter_t *w = ctx;
    deadbeef->mutex_lock (w->mutex);
    w->channels = MIN (MAX_CHANNELS, data->fmt->channels);
    int nsamples = data->nframes/w->channels;

    for (int i = 0; i < data->fmt->channels; i++) {
        w->data[i] = 0;
    }
    for (int c = 0; c < w->channels; c++) {
        for (int s = 0; s < nsamples + c; s++) {
            w->data[c] += (data->data[ftoi (s * data->fmt->channels) + c] * data->data[ftoi (s * data->fmt->channels) + c]);
        }
    }
    for (int i = 0; i < data->fmt->channels; i++) {
        w->data[i] /= nsamples;
        w->data[i] = sqrt (w->data[i]);
    }
    deadbeef->mutex_unlock (w->mutex);
}

static gboolean
vumeter_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    w_vumeter_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);

    deadbeef->mutex_lock (w->mutex);
    int width, height, bands;
    bands = MIN (w->channels, MAX_CHANNELS);
    bands = MAX (bands, 1);
    width = a.width;
    height = a.height;

    float bar_falloff = CONFIG_BAR_FALLOFF/1000.0 * CONFIG_REFRESH_INTERVAL;
    float peak_falloff = CONFIG_PEAK_FALLOFF/1000.0 * CONFIG_REFRESH_INTERVAL;
    int bar_delay = ftoi (CONFIG_BAR_DELAY/CONFIG_REFRESH_INTERVAL);
    int peak_delay = ftoi (CONFIG_PEAK_DELAY/CONFIG_REFRESH_INTERVAL);

    if (deadbeef->get_output ()->state () == OUTPUT_STATE_PLAYING) {
        for (int i = 0; i < bands; i++)
        {
            float x = CONFIG_DB_RANGE + (20.0 * log10 (w->data[i]));
            // TODO: get rid of hardcoding
            //x += CONFIG_DB_RANGE - 63;
            //if (x > CONFIG_DB_RANGE) {
            //    x = w->peaks[i];
            //}
            w->bars[i] = CLAMP (w->bars[i], 0, CONFIG_DB_RANGE);
            w->peaks[i] = CLAMP (w->peaks[i], 0, CONFIG_DB_RANGE);

            if (CONFIG_BAR_FALLOFF != -1) {
                if (w->delay[i] < 0) {
                    w->bars[i] -= bar_falloff;
                }
                else {
                    w->delay[i]--;
                }
            }
            else {
                w->bars[i] = 0;
            }
            if (CONFIG_PEAK_FALLOFF != -1) {
                if (w->delay_peak[i] < 0) {
                    w->peaks[i] -= peak_falloff;
                }
                else {
                    w->delay_peak[i]--;
                }
            }
            else {
                w->peaks[i] = 0;
            }

            if (x > w->bars[i])
            {
                w->bars[i] = x;
                w->delay[i] = bar_delay;
            }
            if (x > w->peaks[i]) {
                w->peaks[i] = x;
                w->delay_peak[i] = peak_delay;
            }
            if (w->peaks[i] < w->bars[i]) {
                w->peaks[i] = w->bars[i];
            }
        }
    }

    // start drawing
    if (!w->surf || cairo_image_surface_get_width (w->surf) != a.width || cairo_image_surface_get_height (w->surf) != a.height) {
        if (w->surf) {
            cairo_surface_destroy (w->surf);
            w->surf = NULL;
        }
        w->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
    }
    float base_s = (height / (float)CONFIG_DB_RANGE);

    cairo_surface_flush (w->surf);

    unsigned char *data = cairo_image_surface_get_data (w->surf);
    if (!data) {
        return FALSE;
    }
    int stride = cairo_image_surface_get_stride (w->surf);
    memset (data, 0, a.height * stride);

    int barw = CLAMP (width / bands, 2, 1000);

    //draw background
    _draw_background (data, a.width, a.height, CONFIG_COLOR_BG32);
    // draw vertical grid
    if (CONFIG_ENABLE_VGRID) {
        int num_lines = MIN (a.width/barw, bands);
        for (int i = 1; i < num_lines; i++) {
            _draw_vline (data, stride, barw * i, 0, height-1, CONFIG_COLOR_VGRID32);
        }
    }

    int hgrid_num = CONFIG_DB_RANGE/10;
    // draw horizontal grid
    if (CONFIG_ENABLE_HGRID && a.height > 2*hgrid_num && a.width > 1) {
        for (int i = 1; i < hgrid_num; i++) {
            _draw_hline (data, stride, 0, ftoi (i/(float)hgrid_num * a.height), a.width-1, CONFIG_COLOR_HGRID32);
        }
    }

    for (gint i = 0; i < bands; i++)
    {
        int x = barw * i;
        int y = a.height - ftoi (w->bars[i] * base_s);
        if (y < 0) {
            y = 0;
        }
        int bw = barw-1;
        if (x + bw >= a.width) {
            bw = a.width-x-1;
        }
        if (CONFIG_GRADIENT_ORIENTATION == 0) {
            if (CONFIG_ENABLE_BAR_MODE == 0) {
                _draw_bar_gradient_v (user_data, data, stride, x+1, y, bw, a.height-y, a.height);
            }
            else {
                _draw_bar_gradient_bar_mode_v (user_data, data, stride, x+1, y, bw, a.height-y, a.height);
            }
        }
        else {
            if (CONFIG_ENABLE_BAR_MODE == 0) {
                _draw_bar_gradient_h (user_data, data, stride, x+1, y, bw, a.height-y, a.width);
            }
            else {
                _draw_bar_gradient_bar_mode_h (user_data, data, stride, x+1, y, bw, a.height-y, a.width);
            }
        }
        y = a.height - w->peaks[i] * base_s;
        if (y < a.height-1) {
            if (CONFIG_GRADIENT_ORIENTATION == 0) {
                _draw_bar_gradient_v (user_data, data, stride, x + 1, y, bw, 1, a.height);
            }
            else {
                _draw_bar_gradient_h (user_data, data, stride, x + 1, y, bw, 1, a.width);
            }
        }
    }

    cairo_surface_mark_dirty (w->surf);

    cairo_save (cr);
    cairo_set_source_surface (cr, w->surf, 0, 0);
    cairo_rectangle (cr, 0, 0, a.width, a.height);
    cairo_fill (cr);
    cairo_restore (cr);

    deadbeef->mutex_unlock (w->mutex);
    return FALSE;
}


gboolean
vumeter_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (widget));
    gboolean res = vumeter_draw (widget, cr, user_data);
    cairo_destroy (cr);
    return res;
}


gboolean
vumeter_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    //w_vumeter_t *w = user_data;
    if (event->button == 3) {
      return TRUE;
    }
    return TRUE;
}

gboolean
vumeter_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_vumeter_t *w = user_data;
    if (event->button == 3) {
      gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, w->drawarea, 0, gtk_get_current_event_time ());
      return TRUE;
    }
    return TRUE;
}

static int
vumeter_message (ddb_gtkui_widget_t *widget, uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    w_vumeter_t *w = (w_vumeter_t *)widget;

    switch (id) {
        case DB_EV_CONFIGCHANGED:
            on_config_changed (w, ctx);
            if (w->drawtimer) {
                g_source_remove (w->drawtimer);
                w->drawtimer = 0;
            }
            w->drawtimer = g_timeout_add (CONFIG_REFRESH_INTERVAL, w_vumeter_draw_cb, w);
            break;
    }
    return 0;
}

void
w_vumeter_init (ddb_gtkui_widget_t *w) {
    w_vumeter_t *s = (w_vumeter_t *)w;
    load_config ();
    deadbeef->mutex_lock (s->mutex);
    create_gradient_table (s, CONFIG_GRADIENT_COLORS, CONFIG_NUM_COLORS);

    if (s->drawtimer) {
        g_source_remove (s->drawtimer);
        s->drawtimer = 0;
    }
    s->drawtimer = g_timeout_add (CONFIG_REFRESH_INTERVAL, w_vumeter_draw_cb, w);
    deadbeef->mutex_unlock (s->mutex);
}

ddb_gtkui_widget_t *
w_vu_meter_create (void) {
    w_vumeter_t *w = malloc (sizeof (w_vumeter_t));
    memset (w, 0, sizeof (w_vumeter_t));

    w->base.widget = gtk_event_box_new ();
    w->base.init = w_vumeter_init;
    w->base.destroy  = w_vumeter_destroy;
    w->base.message = vumeter_message;
    w->drawarea = gtk_drawing_area_new ();
    w->popup = gtk_menu_new ();
    w->popup_item = gtk_menu_item_new_with_mnemonic ("Configure");
    w->mutex = deadbeef->mutex_create ();

    gtk_container_add (GTK_CONTAINER (w->base.widget), w->drawarea);
    gtk_container_add (GTK_CONTAINER (w->popup), w->popup_item);
    gtk_widget_show (w->drawarea);
    gtk_widget_show (w->popup);
    gtk_widget_show (w->popup_item);

#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->drawarea, "expose_event", G_CALLBACK (vumeter_expose_event), w);
#else
    g_signal_connect_after ((gpointer) w->drawarea, "draw", G_CALLBACK (vumeter_draw), w);
#endif
    g_signal_connect_after ((gpointer) w->base.widget, "button_press_event", G_CALLBACK (vumeter_button_press_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_release_event", G_CALLBACK (vumeter_button_release_event), w);
    g_signal_connect_after ((gpointer) w->popup_item, "activate", G_CALLBACK (on_button_config), w);
    gtkui_plugin->w_override_signals (w->base.widget, w);
    gtk_widget_set_events (w->base.widget, GDK_EXPOSURE_MASK
                                         | GDK_LEAVE_NOTIFY_MASK
                                         | GDK_BUTTON_PRESS_MASK
                                         | GDK_POINTER_MOTION_MASK
                                         | GDK_POINTER_MOTION_HINT_MASK);
    deadbeef->vis_waveform_listen (w, vumeter_wavedata_listener);
    return (ddb_gtkui_widget_t *)w;
}

int
vu_meter_connect (void)
{
    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id (DDB_GTKUI_PLUGIN_ID);
    if (gtkui_plugin) {
        //trace("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor );
        if (gtkui_plugin->gui.plugin.version_major == 2) {
            //printf ("fb api2\n");
            // 0.6+, use the new widget API
            gtkui_plugin->w_reg_widget ("VU Meter", 0, w_vu_meter_create, "vu_meter", NULL);
            return 0;
        }
    }
    return -1;
}

int
vu_meter_start (void)
{
    load_config ();
    return 0;
}

int
vu_meter_stop (void)
{
    save_config ();
    return 0;
}

int
vu_meter_startup (GtkWidget *cont)
{
    return 0;
}

int
vu_meter_shutdown (GtkWidget *cont)
{
    return 0;
}
int
vu_meter_disconnect (void)
{
    gtkui_plugin = NULL;
    return 0;
}

static const char settings_dlg[] =
    "property \"Refresh interval (ms): \"           spinbtn[10,1000,1] "      CONFSTR_MS_REFRESH_INTERVAL         " 25 ;\n"
    "property \"Bar falloff (dB/s): \"           spinbtn[-1,1000,1] "      CONFSTR_MS_BAR_FALLOFF         " -1 ;\n"
    "property \"Bar delay (ms): \"                spinbtn[0,10000,100] "      CONFSTR_MS_BAR_DELAY           " 0 ;\n"
    "property \"Peak falloff (dB/s): \"          spinbtn[-1,1000,1] "      CONFSTR_MS_PEAK_FALLOFF        " 90 ;\n"
    "property \"Peak delay (ms): \"               spinbtn[0,10000,100] "      CONFSTR_MS_PEAK_DELAY          " 500 ;\n"
;

static DB_misc_t plugin = {
    //DB_PLUGIN_SET_API_VERSION
    .plugin.type            = DB_PLUGIN_MISC,
    .plugin.api_vmajor      = 1,
    .plugin.api_vminor      = 5,
    .plugin.version_major   = 0,
    .plugin.version_minor   = 1,
#if GTK_CHECK_VERSION(3,0,0)
    .plugin.id              = "vu_meter-gtk3",
#else
    .plugin.id              = "vu_meter",
#endif
    .plugin.name            = "VU Meter",
    .plugin.descr           = "VU Meter",
    .plugin.copyright       =
        "Copyright (C) 2013 Christian Boxdörfer <christian.boxdoerfer@posteo.de>\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website         = "https://github.com/cboxdoerfer/ddb_vu_meter",
    .plugin.start           = vu_meter_start,
    .plugin.stop            = vu_meter_stop,
    .plugin.connect         = vu_meter_connect,
    .plugin.disconnect      = vu_meter_disconnect,
    .plugin.configdialog    = settings_dlg,
};

#if !GTK_CHECK_VERSION(3,0,0)
DB_plugin_t *
ddb_vis_vu_meter_GTK2_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#else
DB_plugin_t *
ddb_vis_vu_meter_GTK3_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#endif
