/*
 *  Copyright (c) by Allin Cottrell
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef GPT_CONTROL_H
#define GPT_CONTROL_H

typedef struct png_plot_t png_plot;

int remove_png_term_from_plotfile_by_name (const char *fname);

void display_session_graph_png (const char *pltname);

void gnuplot_show_png_by_name (const char *fname);

void plot_label_position_click (GtkWidget *w, png_plot *plot);

int redisplay_edited_png (png_plot *plot);

void plot_remove_controller (png_plot *plot);

void set_plot_has_y2_axis (png_plot *plot, gboolean s);

int plot_is_mouseable (const png_plot *plot);

GtkWidget *plot_get_shell (png_plot *plot);

void revise_distribution_plotspec (png_plot *plot, int d, int df1, int df2);

int gp_term_code (gpointer p);

void save_graph_to_file (gpointer p, const char *fname);

#endif /* GPT_CONTROL_H */
