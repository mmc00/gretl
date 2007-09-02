/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

/* graphing.h for gretl */

#ifndef GRAPHING_H
#define GRAPHING_H

#include <stdio.h>

#define GRAPH_NO_DATA -999

typedef enum {
    GPT_IMPULSES       = 1 << 0,  /* use impulses for plotting */
    GPT_LINES          = 1 << 1,  /* force use of lines for plotting */
    GPT_RESIDS         = 1 << 2,  /* doing residual plot */
    GPT_FA             = 1 << 3,  /* doing fitted/actual plot */
    GPT_DUMMY          = 1 << 4,  /* using a dummy for separation */
    GPT_BATCH          = 1 << 5,  /* working in batch mode */
    GPT_GUI            = 1 << 6,  /* called from GUI context */
    GPT_FIT_OMIT       = 1 << 7,  /* User said don't draw fitted line on graph */
    GPT_DATA_STYLE     = 1 << 8,  /* data style is set by user */
    GPT_FILE           = 1 << 9,  /* send output to named file */
    GPT_IDX            = 1 << 10, /* plot against time or obs index */
    GPT_TS             = 1 << 11, /* doing time series plot */
    GPT_Y2AXIS         = 1 << 12, /* plot has second y-axis */
    GPT_AUTO_FIT       = 1 << 13, /* automatic (OLS) fitted line was added */
    GPT_FIT_HIDDEN     = 1 << 14, /* autofit line calculated, but suppressed */
    GPT_MINIMAL_BORDER = 1 << 15, /* omitting top and right borders */
    GPT_PNG_OUTPUT     = 1 << 16, /* output is to PNG file */
    GPT_ALL_MARKERS    = 1 << 17, /* all observation markers displayed */
    GPT_ALL_MARKERS_OK = 1 << 18, /* OK to show all observation markers */
    GPT_LETTERBOX      = 1 << 19  /* special format for time series graphs */
} GptFlags; 

#define MAXTITLE 128
#define MAX_PLOT_LABELS 3
#define N_GP_COLORS 4
#define BOXCOLOR (N_GP_COLORS - 1)

typedef enum {
    PLOT_REGULAR = 0,
    PLOT_H_TEST,
    PLOT_PROB_DIST,
    PLOT_FORECAST,
    PLOT_GARCH,
    PLOT_FREQ_SIMPLE,
    PLOT_FREQ_NORMAL,
    PLOT_FREQ_GAMMA,
    PLOT_PERIODOGRAM,
    PLOT_CORRELOGRAM,
    PLOT_CUSUM,
    PLOT_MULTI_SCATTER,
    PLOT_TRI_GRAPH,
    PLOT_RANGE_MEAN,
    PLOT_HURST,
    PLOT_LEVERAGE,
    PLOT_IRFBOOT,
    PLOT_KERNEL,
    PLOT_VAR_ROOTS,
    PLOT_ELLIPSE,
    PLOT_MULTI_IRF,
    PLOT_PANEL,
    PLOT_BI_GRAPH,
    PLOT_MANY_TS,
    PLOT_TYPE_MAX
} PlotType;

typedef enum {
    PLOT_FIT_NONE,
    PLOT_FIT_OLS,
    PLOT_FIT_QUADRATIC,
    PLOT_FIT_INVERSE,
    PLOT_FIT_LOESS,
    PLOT_FIT_NA       /* fit option not applicable */
} FitType;

typedef enum {
    GP_TERM_NONE,
    GP_TERM_PNG,
    GP_TERM_EPS,
    GP_TERM_PDF,
    GP_TERM_FIG,
    GP_TERM_TEX,
    GP_TERM_EMF,
    GP_TERM_SVG,
    GP_TERM_PLT
} TermType;

#define frequency_plot_code(c) (c == PLOT_FREQ_SIMPLE || \
				c == PLOT_FREQ_NORMAL || \
				c == PLOT_FREQ_GAMMA)

#define set_png_output(p) (p->flags |= GPT_PNG_OUTPUT)
#define get_png_output(p) (p->flags & GPT_PNG_OUTPUT) 
    
const char *get_gretl_png_term_line (PlotType ptype, GptFlags flags);

const char *get_gretl_emf_term_line (PlotType ptype, int color);

const char *gp_justification_string (int j);

const char *gnuplot_label_front_string (void);

void gnuplot_missval_string (FILE *fp);

int gnuplot_init (PlotType ptype, FILE **fpp);

int write_plot_type_string (PlotType ptype, FILE *fp);

PlotType plot_type_from_string (const char *str);

int gnuplot_make_graph (void);

void reset_plot_count (void);

int gnuplot (const int *plotlist, const char *literal,
	     const double **Z, const DATAINFO *pdinfo, 
	     gretlopt opt);

int multi_scatters (const int *list, const double **Z,
		    const DATAINFO *pdinfo, gretlopt opt);

int gnuplot_3d (int *list, const char *literal,
		double ***pZ, DATAINFO *pdinfo, 
		gretlopt opt);

int plot_freq (FreqDist *freq, DistCode dist);

int garch_resid_plot (const MODEL *pmod, const DATAINFO *pdinfo); 

int rmplot (const int *list, const double **Z, DATAINFO *pdinfo, 
	    PRN *prn);

int hurstplot (const int *list, const double **Z, DATAINFO *pdinfo, 
	       PRN *prn);

int 
gretl_panel_ts_plot (const int *list, const double **Z, DATAINFO *pdinfo);

int plot_fcast_errs (int t1, int t2, const double *obs, 
		     const double *depvar, const double *yhat, 
		     const double *maxerr, const char *varname, 
		     int time_series);

int 
gretl_VAR_plot_impulse_response (GRETL_VAR *var,
				 int targ, int shock, int periods,
				 const double **Z,
				 const DATAINFO *pdinfo);

int 
gretl_VAR_plot_multiple_irf (GRETL_VAR *var, int periods,
			     const double **Z,
			     const DATAINFO *pdinfo);

int gretl_VAR_residual_plot (const GRETL_VAR *var, const DATAINFO *pdinfo);

int gretl_VAR_residual_mplot (const GRETL_VAR *var, const DATAINFO *pdinfo); 

int gretl_VAR_roots_plot (GRETL_VAR *var);

int confidence_ellipse_plot (gretl_matrix *V, double *b, double t, double c,
			     const char *iname, const char *jname);

int is_auto_fit_string (const char *s);

int gnuplot_has_ttf (int reset);

int gnuplot_has_pdf (void);

int gnuplot_has_cairo (void);

int gnuplot_has_specified_colors (void);

int gnuplot_has_style_fill (void);

void set_graph_palette (int i, const char *colstr);

void graph_palette_reset (int i);

void print_palette_string (char *targ);

const char *graph_color_string (int i);

int gnuplot_test_command (const char *cmd);

#endif /* GRAPHING_H */

