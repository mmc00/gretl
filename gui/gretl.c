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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* gretl.c : main for gretl */

/* #define DND 1 */

#include "gretl.h"
#include <dirent.h>

#ifndef G_OS_WIN32
# include <unistd.h>
# include "pixmaps/gretl.xpm"  /* program icon for X */
#else
# include <windows.h>
# ifdef USE_HHCTRL
#  include "htmlhelp.h"
# endif
#endif

/* pixmaps for gretl toolbar */
#include "pixmaps/mini.calc.xpm"
#include "pixmaps/mini.edit.xpm"
#include "pixmaps/mini.sh.xpm"
#include "pixmaps/mini.session.xpm"
#include "pixmaps/mini.manual.xpm"
#include "pixmaps/mini.netscape.xpm"
#include "pixmaps/mini.pdf.xpm"
#include "pixmaps/mini.plot.xpm"
#include "pixmaps/mini.camera.xpm"
#include "pixmaps/mini.ofolder.xpm"

/* functions from other gretl GUI files */
extern void free_modelspec (void);    /* lib.c */
extern void allocate_fileptrs (void); /* gui_utils.c */
extern void stats_calculator (gpointer data, guint ttest, GtkWidget *widget);
extern void bool_subsample (gpointer data, guint dropmiss, GtkWidget *w);
extern void free_command_stack (void);
extern void open_named_db_clist (char *dbname);
extern void open_named_remote_clist (char *dbname);
extern void gui_set_panel_structure (gpointer data, guint u, GtkWidget *w);
extern void time_series_dialog (gpointer data, guint u, GtkWidget *w);

/* functions private to gretl.c */
static void make_toolbar (GtkWidget *w, GtkWidget *box);
static void clip_init (GtkWidget *w);
static GtkWidget *make_main_window (int gui_get_data);
static GtkWidget *build_var_menu (void);
static gint popup_activated (GtkWidget *widget, gpointer data);
static void check_for_pwt (void);
static void set_up_main_menu (void);
static void startR (gpointer p, guint opt, GtkWidget *w);
static void Rcleanup (void);
static void auto_store (void);

GtkWidget *toolbar_box = NULL; /* shared with gui_utils.c */

static GtkWidget *dataframe;
static GtkWidget *main_vbox;
static GtkWidget *gretl_toolbar = NULL;
GtkTooltips *gretl_tips;
GdkColor red, blue;

static int popup_connected;
int *default_list = NULL;

static GtkTargetEntry target_table[] = {
    {"text/uri-list", 0, 1},
};

static void  
drag_data_received  (GtkWidget          *widget,
		     GdkDragContext     *dc,
		     gint                x,
		     gint                y,
		     GtkSelectionData   *data,
		     guint               info,
		     guint               time,
		     gpointer            p);

#ifdef USE_GNOME
char *optrun = NULL, *optdb = NULL;

static const struct poptOption options[] = {
	{"run", 'r', POPT_ARG_STRING, &optrun, 0, 
	 N_("open a script file on startup"), N_("SCRIPT")},
	{"db", 'd', POPT_ARG_STRING, &optdb, 0, 
	 N_("open a database on startup"), N_("DATABASE")},
	{"webdb", 'w', POPT_ARG_STRING, &optdb, 0, 
	 N_("open a remote (web) database on startup"), N_("REMOTE_DB")},
	{NULL, '\0', 0, NULL, 0}
};
#endif /* USE_GNOME */

windata_t *mdata;
DATAINFO *datainfo;
DATAINFO *subinfo;
DATAINFO *fullinfo;
char *errtext;
char cmdfile[MAXLEN], scriptfile[MAXLEN];
char trydatfile[MAXLEN], tryscript[MAXLEN];
char line[1024];
PATHS paths;                /* useful paths */
CMD command;                /* gretl command struct */
double **Z;                 /* data set */
double **subZ;              /* sub-sampled data set */
double **fullZ;             /* convenience pointer */
MODEL **models;             /* gretl models structs */
SESSION session;            /* hold models, graphs */
SESSIONBUILD rebuild;       /* rebuild session later */

int plot_count, data_status, orig_vars;
PRN *cmds;
gchar *clipboard_buf; /* for copying models as HTML, LaTeX */

/* defaults for some options */
int expert = FALSE; 
int updater = FALSE;
int want_toolbar = TRUE;
char dbproxy[21];

#ifdef G_OS_WIN32
    char Rcommand[MAXSTR] = "RGui.exe";
    char editor[MAXSTR] = "winword.exe";
    char calculator[MAXSTR] = "calc.exe";
#else
    char editor[MAXSTR] = "emacs";
    char calculator[MAXSTR] = "xcalc";
# ifdef USE_GNOME
    char Rcommand[MAXSTR] = "R --gui=gnome";
    extern const char *version_string;
# else
    char Rcommand[MAXSTR] = "xterm -e R";
# endif
#endif

static void spreadsheet_edit (gpointer p, guint u, GtkWidget *w) 
{
    extern void show_spreadsheet (DATAINFO *pdinfo);  
  
    show_spreadsheet(NULL);
}

#ifdef USE_GNOME
static void gnome_help (void)
{
    static GnomeHelpMenuEntry help_entry = { "gretl", "index.html" };

    gnome_help_display(NULL, &help_entry);
}
#endif /* USE_GNOME */

#ifdef G_OS_WIN32
static void win_help (void)
{
    char hlpfile[MAXLEN];

# ifdef USE_HHCTRL
    sprintf(hlpfile, "%s\\gretl.chm", paths.gretldir);
    if (!HtmlHelp(GetDesktopWindow(), hlpfile, HH_DISPLAY_TOPIC, 0))
	errbox(_("Couldn't access help file"));
# else
    /* hh.exe myfile.chm::/mytopic.htm */
    sprintf(hlpfile, "hh.exe \"%s\\gretl.chm\"", paths.gretldir);
    if (WinExec(hlpfile, SW_SHOWNORMAL) < 32)
        errbox(_("Couldn't access help file"));
# endif /* USE_HHCTRL */
}

static int unmangle (const char *dosname, char *longname);

#endif /* G_OS_WIN32 */

GtkItemFactoryEntry data_items[] = {
    { _("/_File"), NULL, NULL, 0, "<Branch>" },
    { _("/File/_Open data/user file..."), NULL, open_data, OPEN_DATA, NULL },
    { _("/File/_Open data/sample file/Ramanathan..."), NULL, 
      display_files, RAMU_DATA, NULL },
    { _("/File/_Open data/sample file/Greene..."), NULL, 
      display_files, GREENE_DATA, NULL },
    { _("/File/_Open data/sample file/Penn World Table..."), NULL, 
      display_files, PWT_DATA, NULL },
    { _("/File/_Open data/sep1"), NULL, NULL, 0, "<Separator>" },    
    { _("/File/_Open data/import CSV..."), NULL, open_data, OPEN_CSV, NULL },
    { _("/File/_Open data/import BOX..."), NULL, open_data, OPEN_BOX, NULL },
    { _("/File/_Save data"), NULL, auto_store, 0, NULL },
    { _("/File/Save data _as/_standard format..."), NULL, file_save, 
      SAVE_DATA, NULL },
    { _("/File/Save data _as/_gzipped..."), NULL, 
      file_save, SAVE_GZDATA, NULL },
#ifdef notdef
    { _("/File/Save data _as/_alternative formats/_single precision binary..."), 
      NULL, file_save, SAVE_BIN1, NULL },
    { _("/File/Save data _as/_alternative formats/_double precision binary..."),
      NULL, file_save, SAVE_BIN2, NULL },
#endif 
    { _("/File/_Export data/_CSV..."), NULL, file_save, 
      EXPORT_CSV, NULL },
    { _("/File/_Export data/GNU _R..."), NULL, file_save, 
      EXPORT_R, NULL },
    { _("/File/_Export data/GNU _octave..."), NULL, file_save, 
      EXPORT_OCTAVE, NULL },
    { _("/File/C_lear data set"), NULL, verify_clear_data, 0, NULL },
    { _("/File/sep0"), NULL, NULL, 0, "<Separator>" },
    { _("/File/_Browse databases/_gretl native"), NULL, display_files, 
      NATIVE_DB, NULL },
    { _("/File/_Browse databases/_RATS 4"), NULL, display_files, 
      RATS_DB, NULL },
    { _("/File/_Browse databases/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/File/_Browse databases/on database _server"), NULL, display_files, 
      REMOTE_DB, NULL },
    { _("/File/_Create data set/time-series/annual"), 
      NULL, newdata_dialog, 1, NULL },    
    { _("/File/_Create data set/time-series/quarterly"), 
      NULL, newdata_dialog, 4, NULL },    
    { _("/File/_Create data set/time-series/monthly"), 
      NULL, newdata_dialog, 12, NULL }, 
    { _("/File/_Create data set/time-series/undated"), 
      NULL, newdata_dialog, 0, NULL }, 
    { _("/File/_Create data set/time-series/high frequency/weekly"), 
      NULL, newdata_dialog, 52, NULL }, 
    { _("/File/_Create data set/time-series/high frequency/daily (5-day week)"), 
      NULL, newdata_dialog, 5, NULL }, 
    { _("/File/_Create data set/time-series/high frequency/daily (7-day week)"), 
      NULL, newdata_dialog, 7, NULL }, 
    { _("/File/_Create data set/time-series/high frequency/hourly"), 
      NULL, newdata_dialog, 24, NULL }, 
    { _("/File/_Create data set/cross-sectional"), 
      NULL, newdata_dialog, 0, NULL }, 
#ifdef notdef  
    { _("/File/_Create data set/panel"), 
      NULL, start_panel_dialog, 0, NULL }, 
#endif 
    { _("/File/_Create data set/simulation"), NULL, gretl_callback, 
      NULLDATA, NULL },
    { _("/File/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/File/Save last graph"), NULL, gpt_save_dialog, 0, NULL }, 
    { _("/File/sep2"), NULL, NULL, 0, "<Separator>" },
    { _("/File/_View command log"), NULL, view_log, 0, NULL },
    { _("/File/sep2a"), NULL, NULL, 0, "<Separator>" },
    { _("/File/Open command file/user file..."), NULL, open_script, 
      OPEN_SCRIPT, NULL },
    { _("/File/Open command file/practice file/Ramanathan..."), NULL, 
      display_files, RAMU_PS, NULL },
    { _("/File/Open command file/practice file/Greene..."), NULL, 
      display_files, GREENE_PS, NULL },
    { _("/File/Open command file/practice file/Penn World Table..."), NULL, 
      display_files, PWT_PS, NULL },
    { _("/File/New command file/regular script"), NULL, do_new_script, 0, NULL },
    { _("/File/New command file/Monte Carlo loop"), NULL, 
      do_new_script, 1, NULL },
    { _("/File/sep3"), NULL, NULL, 0, "<Separator>" },
    { _("/File/_Preferences/_General..."), NULL, options_dialog, 0, NULL },
    { _("/File/_Preferences/_Fixed font..."), NULL, font_selector, 0, NULL },
    { _("/File/sep5"), NULL, NULL, 0, "<Separator>" },
    { _("/File/E_xit"), NULL, menu_exit_check, 0, NULL },
    { _("/_Utilities"), NULL, NULL, 0, "<Branch>" },
    { _("/Utilities/Statistical tables"), NULL, stats_calculator, 1, NULL },
    { _("/Utilities/p-value finder"), NULL, stats_calculator, 0, NULL },
    { _("/Utilities/Test statistic calculator"), NULL, stats_calculator, 2, NULL },
    { _("/Utilities/sep"), NULL, NULL, 0, "<Separator>" },
    { _("/Utilities/Gretl console"), NULL, console, 0, NULL },
    { _("/Utilities/sep2"), NULL, NULL, 0, "<Separator>" },
    { _("/Utilities/Start GNU R"), NULL, startR, 0, NULL },
    { _("/_Session"), NULL, NULL, 0, "<Branch>" },
    { _("/Session/_Icon view"), NULL, view_session, 0, NULL },
    { _("/Session/_Add last graph"), NULL, add_last_graph, 0, NULL },
    { _("/Session/sep0"), NULL, NULL, 0, "<Separator>" },
    { _("/Session/_Open..."), NULL, open_script, OPEN_SESSION, NULL },
    { _("/Session/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/Session/_Save"), NULL, save_session_callback, 0, NULL },
    { _("/Session/Save _as..."), NULL, save_session_callback, 1, NULL },
    { _("/_Data"), NULL, NULL, 0, "<Branch>" },
    { _("/Data/_Display values/all variables"), NULL, display_data, 0, NULL },
    { _("/Data/_Display values/selected variables..."), 
      NULL, gretl_callback, PRINT, NULL },
    { _("/Data/_Edit values"), NULL, spreadsheet_edit, 0, NULL },
    { _("/Data/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/_Graph specified vars/Time series plot..."), 
      NULL, graph_dialog, GR_PLOT, NULL },
    { _("/Data/_Graph specified vars/X-Y scatter..."), 
      NULL, graph_dialog, GR_XY, NULL },
    { _("/Data/_Graph specified vars/X-Y with impulses..."), 
      NULL, graph_dialog, GR_IMP, NULL },
    { _("/Data/_Graph specified vars/X-Y with factor separation..."), 
      NULL, graph_dialog, GR_DUMMY, NULL },
    { _("/Data/_Multiple scatterplots..."), 
      NULL, graph_dialog, SCATTERS, NULL},
    { _("/Data/_Graph specified vars/Boxplots..."), 
      NULL, graph_dialog, GR_BOX, NULL },
    { _("/Data/_Graph specified vars/Notched boxplots..."), 
      NULL, graph_dialog, GR_NBOX, NULL },
    { _("/Data/sep2"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/_Read info"), NULL, open_info, 0, NULL },
    { _("/Data/Edit _info"), NULL, edit_header, 0, NULL },
    { _("/Data/sep3"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/_Summary statistics"), NULL, do_menu_op, SUMMARY, NULL },
    { _("/Data/_Correlation matrix"), NULL, do_menu_op, CORR, NULL },
    { _("/Data/sep4"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/Difference of means/assuming equal variances..."), NULL, 
      gretl_callback, MEANTEST, NULL },
    { _("/Data/Difference of means/assuming unequal variances..."), NULL, 
      gretl_callback, MEANTEST2, NULL },
    { _("/Data/Difference of variances..."), NULL, gretl_callback, VARTEST, NULL },
    { _("/Data/sep5"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/Add variables/time trend"), NULL, add_time, 0, NULL },
    { _("/Data/Add variables/index variable"), NULL, add_time, 1, NULL },
    { _("/Data/Add variables/logs of variables..."), NULL, 
      addvars_dialog, LOGS, NULL },
    { _("/Data/Add variables/lags of variables..."), NULL, 
      addvars_dialog, LAGS, NULL },
    { _("/Data/Add variables/squares of variables..."), NULL, 
      addvars_dialog, SQUARE, NULL },
    { _("/Data/Add variables/periodic dummies"), NULL, add_dummies, 0, NULL },
    { _("/Data/Add variables/panel dummies"), NULL, add_dummies, 1, NULL },
    { _("/Data/Add variables/first differences..."), NULL, 
      addvars_dialog, DIFF, NULL },
    { _("/Data/Add variables/log differences..."), NULL, 
      addvars_dialog, LDIFF, NULL },
    { _("/Data/Add variables/sep"), NULL, NULL, 0, "<Separator>" },
    { _("/Data/Add variables/random normal..."), NULL, random_dialog, 0, NULL },
    { _("/Data/Add variables/random uniform..."), NULL, random_dialog, 1, NULL },
    { _("/Data/Add variables/seed generator..."), NULL, gretl_callback, 
      SEED, NULL },
    { _("/Data/Refresh window"), NULL, refresh_data, 0, NULL },
    { _("/_Sample"), NULL, NULL, 0, "<Branch>" },
    { _("/Sample/_Set range..."), NULL, gretl_callback, SMPL, NULL },
    { _("/Sample/_Restore full range"), NULL, restore_sample, 1, NULL },
    { _("/Sample/sep1"), NULL, NULL, 0, "<Separator>" },    
    { _("/Sample/Set _frequency, startobs..."), NULL, gretl_callback, 
      SETOBS, NULL },
    { _("/Sample/sep2"), NULL, NULL, 0, "<Separator>" },   
    { _("/Sample/_Define, based on dummy..."), NULL, gretl_callback, 
      SMPLDUM, NULL },
    { _("/Sample/_Restrict, based on criterion..."), NULL, gretl_callback, 
      SMPLBOOL, NULL },
    { _("/Sample/sep3"), NULL, NULL, 0, "<Separator>" },  
    { _("/Sample/Drop all obs with _missing values"), NULL, bool_subsample, 
      0, NULL },
    { _("/Sample/_Count missing values"), NULL, count_missing, 0, NULL },
    { _("/Sample/Set missing _value code..."), NULL, gretl_callback, 
      GSETMISS, NULL },
    { _("/Sample/sep4"), NULL, NULL, 0, "<Separator>" },  
    { _("/Sample/_Add case markers..."), NULL, gretl_callback, MARKERS, NULL },
    { _("/Sample/sep5"), NULL, NULL, 0, "<Separator>" },
    { _("/Sample/_Interpret as time series..."), NULL, time_series_dialog, 0, NULL },
    { _("/Sample/Interpret as _panel..."), NULL, gui_set_panel_structure, 0, NULL },
    { _("/_Variable"), NULL, NULL, 0, "<Branch>" },
    { _("/Variable/_Display values"), NULL, display_var, 0, NULL },
    { _("/Variable/_Summary statistics"), NULL, do_menu_op, 
      VAR_SUMMARY, NULL },
    { _("/Variable/_Time series plot"), NULL, do_graph_var, 0, NULL },
    { _("/Variable/_Frequency distribution"), NULL, do_menu_op, 
      FREQ, NULL },
    { _("/Variable/Frequency plot/simple"), NULL, do_freqplot, 0, NULL },
    { _("/Variable/Frequency plot/against Normal"), NULL, do_freqplot, 
      NORMAL, NULL },
    { _("/Variable/Frequency plot/against Gamma"), NULL, do_freqplot, 
      GAMMA, NULL },
    { _("/Variable/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/Variable/Correlogram"), NULL, gretl_callback, CORRGM, NULL }, 
    { _("/Variable/Spectrum/sample periodogram"), NULL, do_pergm, 0, NULL }, 
    { _("/Variable/Spectrum/Bartlett lag window"), NULL, do_pergm, 1, NULL }, 
    { _("/Variable/_Augmented Dickey-Fuller test"), NULL, gretl_callback, 
      ADF, NULL },
    { _("/Variable/Runs test"), NULL, do_menu_op, RUNS, NULL }, 
    { _("/Variable/sep2"), NULL, NULL, 0, "<Separator>" },
    { _("/Variable/_Rename"), NULL, gretl_callback, RENAME, NULL },
    { _("/Variable/_Edit label"), NULL, gretl_callback, RELABEL, NULL },
    { _("/Variable/Set missing value code..."), NULL, gretl_callback, 
      VSETMISS, NULL },
    { _("/Variable/sep3"), NULL, NULL, 0, "<Separator>" },
    { _("/Variable/Simulate..."), NULL, gretl_callback, SIM, NULL },
    { _("/Variable/Define _new variable..."), NULL, gretl_callback, GENR, NULL },
    { _("/Variable/Delete last variable"), NULL, delete_var, 0, NULL },
    { _("/_Model"), NULL, NULL, 0, "<Branch>" },
    { _("/Model/_Ordinary Least Squares..."), NULL, model_callback, OLS, NULL },
    { _("/Model/_Weighted Least Squares..."), NULL, model_callback, WLS, NULL },
    { _("/Model/sep1"),  NULL, NULL, 0, "<Separator>" },
    { _("/Model/HCC_M..."), NULL, model_callback, HCCM, NULL },
    { _("/Model/H_eteroskedasticity corrected..."), NULL, model_callback, 
      HSK, NULL },
    { _("/Model/sep2"),  NULL, NULL, 0, "<Separator>" },
    { _("/Model/_Cochrane-Orcutt..."), NULL, model_callback, CORC, NULL },
    { _("/Model/_Hildreth-Lu..."), NULL, model_callback, HILU, NULL },
    { _("/Model/_Autoregressive estimation..."), NULL, model_callback, AR, NULL },
    { _("/Model/sep3"),  NULL, NULL, 0, "<Separator>" },
    { _("/Model/_Vector Autoregression..."), NULL, model_callback, VAR, NULL },
    { _("/Model/Cointe_gration test..."), NULL, gretl_callback, COINT, NULL },
    { _("/Model/_Two-Stage Least Squares..."), NULL, model_callback, TSLS, NULL },
    { _("/Model/sep4"),  NULL, NULL, 0, "<Separator>" },
    { _("/Model/_Logit..."), NULL, model_callback, LOGIT, NULL },
    { _("/Model/_Probit..."), NULL, model_callback, PROBIT, NULL },
    { _("/Model/_Rank correlation..."), NULL, gretl_callback, SPEARMAN, NULL },
    { _("/Model/_Pooled OLS (panel)..."), NULL, model_callback, POOLED, NULL },
    { _("/_Help"), NULL, NULL, 0, "<LastBranch>" },
    { _("/Help/_GUI commands"), NULL, do_gui_help, 0, NULL },
    { _("/Help/_Script commands syntax"), NULL, do_script_help, 1, NULL },
    { _("/Help/sep1"), NULL, NULL, 0, "<Separator>" },
#if defined(USE_GNOME)
    { _("/Help/Manual in HTML"), NULL, gnome_help, 0, NULL },
    { _("/Help/sep2"), NULL, NULL, 0, "<Separator>" },
#elif defined(G_OS_WIN32)
    { _("/Help/Manual in HTML"), NULL, win_help, 0, NULL },
    { _("/Help/sep2"), NULL, NULL, 0, "<Separator>" },
#endif
    { _("/Help/_About gretl"), NULL, about_dialog, 0, NULL }
};

static void make_userdir (PATHS *ppaths) 
{
    DIR *test;
    char buf[MAXLEN];
    
    if ((test = opendir(ppaths->userdir)) == NULL) {
	sprintf(buf, "mkdir -p %s", ppaths->userdir);
	system(buf);
	fprintf(stderr, _("Created user directory %s\n"
		"If you prefer to use a different directory for "
		"gretl user files, please make changes under\n"
		"File, Preferences, General...\n"), ppaths->userdir);
    } else 
	closedir(test);
}

static void gui_usage (void)
{
    gui_logo(stdout);
    printf(_("You may supply the name of a data file on the command line.\n"));
    printf(_("Or you may do \"gretl -r script_file\" to open a script.\n"));
    printf(_("Or you may do \"gretl -d database\" to open a gretl database.\n"));
    exit(0);
}

static void noalloc (char *str)
{
    fprintf(stderr, _("Couldn't allocate memory for %s\n"), str);
    exit(EXIT_FAILURE);
}

static void get_runfile (char *fname)
{
    int i;

    tryscript[0] = '\0';
#ifdef G_OS_WIN32
    if (unmangle(fname, tryscript)) return;
#else
    strncat(tryscript, fname, MAXLEN-1);
#endif
    if (addpath(tryscript, &paths, 1) == NULL) {
	fprintf(stderr, _("Couldn't find script '%s'\n"), tryscript);
	exit(EXIT_FAILURE);
    } else {
	fprintf(stderr, _("%s found\n"), tryscript);
	i = slashpos(tryscript);
	if (i) {
	    paths.currdir[0] = '\0';
	    strncat(paths.currdir, tryscript, i);
	}
	strcat(paths.currdir, SLASHSTR);
    }
}

static void fix_dbname (char *db)
{
    FILE *fp;

    if (strstr(db, ".bin") == NULL)
	strcat(db, ".bin");
    fp = fopen(db, "r");
    if (fp == NULL && strstr(db, paths.binbase) == NULL) {
	char tmp[MAXLEN];

	strcpy(tmp, db);
	sprintf(db, "%s%s", paths.binbase, tmp);
    }
    if (fp != NULL) fclose(fp);
}

static void destroy (GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

#ifdef G_OS_WIN32
extern int ws_startup (void);

void dummy_output_handler (const gchar *log_domain,
                           GLogLevelFlags log_level,
                           const gchar *message,
                           gpointer user_data)
{
    return;
}
#endif 

#ifdef DND
    FILE *dp;
#endif

int main (int argc, char *argv[])
{
    int err = 0, gui_get_data = 0;
    char dbname[MAXLEN];
#ifdef DND
    int i;

    dp = fopen("c:\\userdata\\debug.txt", "w");
    if (dp == NULL) exit(EXIT_FAILURE);
    for (i=0; i<argc; i++)
	fprintf(dp, "argv[%d] = '%s'\n", i, argv[i]);
    fflush(dp);
#endif

    if ((errtext = malloc(MAXLEN)) == NULL) 
	noalloc(_("startup"));

    tryscript[0] = '\0';
    scriptfile[0] = '\0';
    paths.datfile[0] = '\0';

    /* Initialize gnome or GTK */
#ifdef USE_GNOME
    gnome_init_with_popt_table("gretl", version_string, argc, argv,
			       options, 0, NULL);
#else
    gtk_init(&argc, &argv);
#endif

    set_paths(&paths, 1, 1); /* 1 = defaults, 1 = gui */
#ifdef G_OS_WIN32
    read_rc(); /* get config info from registry */
    g_log_set_handler ("Gtk",
		       G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING,
		       (GLogFunc) dummy_output_handler,
		       NULL);
    g_log_set_handler ("Gdk",
		       G_LOG_LEVEL_WARNING,
		       (GLogFunc) dummy_output_handler,
		       NULL);
    ws_startup(); 
    atexit(write_rc);
#else 
    set_rcfile();
    make_userdir(&paths);
#endif/* G_OS_WIN32 */

#ifdef DND
    fprintf(dp, "About to parse arguments if argc > 1\n");
    fflush(dp);
#endif
    if (argc > 1) {
	int opt = parseopt(argv[1]);

	switch (opt) {
	case OPT_HELP:
	    gui_usage();
	    break;
	case OPT_VERSION:
	    gui_logo(stdout);
	    exit(EXIT_SUCCESS);
	    break;
	case OPT_RUNIT:
#ifdef USE_GNOME
	    get_runfile(optrun);
#else
	    if (argc != 3) gui_usage();
	    get_runfile(argv[2]);
#endif
	    gui_get_data = 1;
	    break;
	case OPT_DBOPEN:
	case OPT_WEBDB:
#ifdef USE_GNOME
	    strncpy(dbname, optdb, MAXLEN-1);
#else
	    if (argc != 3) gui_usage();
	    strncpy(dbname, argv[2], MAXLEN-1);
#endif
	    if (opt == OPT_DBOPEN) fix_dbname(dbname);
	    gui_get_data = opt;
	    break;
	default:
	    /* let's suppose the argument is a data file */
	    break;
	}
    } else 
	gui_get_data = 1;

#ifdef DND
    fprintf(dp, "Parsed args: gui_get_data = %d\n",
	    opt, gui_get_data);
    fflush(dp);
#endif

    strcpy(cmdfile, paths.userdir);
    strcat(cmdfile, "session.inp");
    cmds = gretl_print_new(GRETL_PRINT_FILE, cmdfile);
    if (cmds == NULL) {
	fprintf(stderr, _("Can't open file to save commands\n"));
	return EXIT_FAILURE;
    }
    fclose(cmds->fp);

    /* allocate data information struct */
    datainfo = datainfo_new();
    if (datainfo == NULL)
	noalloc(_("data information"));

    /* allocate memory for models */
    models = malloc(3 * sizeof *models);
    if (models == NULL) noalloc(_("models")); 
    models[0] = gretl_model_new(datainfo);
    models[1] = gretl_model_new(datainfo);
    models[2] = gretl_model_new(datainfo);
    if (models[0] == NULL || models[1] == NULL || models[2] == NULL) 
	noalloc(_("models")); 

    command.list = malloc(sizeof(int));
    command.param = malloc(1);
    if (command.list == NULL || command.param == NULL)  
	noalloc(_("command list")); 

    /* initialize random number generator */
    srand((unsigned) time(NULL));

    helpfile_init();
    session_init();

    /* get the data file, if specified on the command line */
    if (!(gui_get_data)) {
	int ftype;
	PRN *prn; 

#ifdef DND
	fprintf(dp, "About to try to get file\n");
	fflush(dp);
#endif
	prn = gretl_print_new(GRETL_PRINT_STDERR, NULL);
	if (prn == NULL) 
	    exit(EXIT_FAILURE);
	paths.datfile[0] = '\0';
#ifdef G_OS_WIN32
	if (unmangle(argv[1], paths.datfile)) 
	    exit(EXIT_FAILURE);
#else
	strncat(paths.datfile, argv[1], MAXLEN-1);
#endif
	ftype = detect_filetype(paths.datfile, &paths, prn);
#ifdef DND
	fprintf(dp, "detect_filetype: got ftype = %d\n", ftype);
	fflush(dp);
#endif
	switch (ftype) {
	case GRETL_UNRECOGNIZED:
	    exit(EXIT_FAILURE);
	case GRETL_NATIVE_DATA:
	    err = get_data(&Z, datainfo, paths.datfile, &paths, data_status, 
			   prn);
	    break;
	case GRETL_XML_DATA:
	    err = get_xmldata(&Z, datainfo, paths.datfile, &paths, data_status, 
			      prn, 0);
	    break;
	case GRETL_CSV_DATA:
	    err = import_csv(&Z, datainfo, paths.datfile, prn);
	    break;
	case GRETL_BOX_DATA:
	    err = import_box(&Z, datainfo, paths.datfile, prn);
	    break;
	case GRETL_SCRIPT:
	    gui_get_data = 1;
	    get_runfile(paths.datfile);
	    paths.datfile[0] = '\0';
	    break;
	}
	if (ftype != GRETL_SCRIPT) {
	    if (err) {
		errmsg(err, prn);
		exit(EXIT_FAILURE);
	    }
	    data_status = HAVE_DATA;
	    /* FIXME need more here? */
	    orig_vars = datainfo->v;
	    /* record the data file in command log */
	    sprintf(line, "open %s", paths.datfile);
	    check_cmd(line);
	    cmd_init(line);
	}
	gretl_print_destroy(prn);
    }
#ifdef DND
	fprintf(dp, "About to create GUI; err = %d\n", err);
	fflush(dp);
#endif

    /* create the GUI */
    gretl_tips = gtk_tooltips_new();
    colorize_tooltips(gretl_tips);

    /* make red, blue available globally for colorizing text */
    gdk_color_parse("red", &red);
    gdk_color_parse("blue", &blue);
    if (!gdk_color_alloc(gdk_colormap_get_system(), &red) ||
	!gdk_color_alloc(gdk_colormap_get_system(), &blue)) 
	noalloc(_("colors"));

    /* create main window */
    if ((mdata = mymalloc(sizeof(windata_t))) == NULL)
	noalloc(_("GUI"));
    if ((dataframe = make_main_window(gui_get_data)) == NULL) 
	noalloc(_("main window"));
    if (!gui_get_data) set_sample_label(datainfo);

    /* enable special copying to clipboard */
    clip_init(mdata->w);

    allocate_fileptrs();
    add_files_to_menu(1);
    add_files_to_menu(2);
    add_files_to_menu(3);
    graphmenu_state(FALSE);
    session_state(FALSE);
    restore_sample_state(FALSE);
    menubar_state(FALSE);
			  
    check_for_pwt();

    if (!gui_get_data)
	register_data(paths.datfile, 1);

    /* opening a script from the command line? */
    if (tryscript[0] != '\0') { 
	do_open_script(NULL, NULL);
    }

    /* check for program updates? */
    proxy_init(dbproxy);
    if (updater) update_query(); 

    /* try opening specified database */
    if (gui_get_data == OPT_DBOPEN)
	open_named_db_clist(dbname);
    else if (gui_get_data == OPT_WEBDB)
	open_named_remote_clist(dbname);

    /* Enter the event loop */
    gtk_main();

    /* clean up before exiting */
    /* if (mdata) free_windata(NULL, mdata); */
    free_session();
    if (Z) free_Z(Z, datainfo);
    if (fullZ) free_Z(fullZ, fullinfo);
    free_model(models[0]);
    free_model(models[1]);
    free_model(models[2]);
    free(models);
    if (command.list != NULL) free(command.list);
    if (command.param != NULL) free(command.param);
    if (data_status) free_datainfo(datainfo);
    if (fullinfo) {
	clear_datainfo(fullinfo, CLEAR_SUBSAMPLE);
	free(fullinfo);
    }

    free_command_stack();
    free_modelspec();

    remove(paths.plotfile);
    Rcleanup();

    return EXIT_SUCCESS;
}

/* ........................................................... */

void refresh_data (void)
{
    if (data_status)
	populate_clist(mdata->listbox, datainfo);
}

/* ........................................................... */

void menubar_state (gboolean s)
{
    if (mdata->ifac == NULL) return;

    flip(mdata->ifac, _("/File/Clear data set"), s);
    flip(mdata->ifac, _("/File/Save data"), s);
    flip(mdata->ifac, _("/File/Save data as"), s);
    flip(mdata->ifac, _("/File/Export data"), s);
    flip(mdata->ifac, _("/File/Create data set"), !s);
    flip(mdata->ifac, _("/Data"), s);
    flip(mdata->ifac, _("/Sample"), s);
    flip(mdata->ifac, _("/Variable"), s);
    flip(mdata->ifac, _("/Model"), s);

    if (s && (data_status & BOOK_DATA))
	flip(mdata->ifac, _("/Data/Edit info"), 0);

}

/* ........................................................... */

void graphmenu_state (gboolean s)
{
    if (mdata->ifac != NULL) {
	flip(mdata->ifac, _("/File/Save last graph"), s);
	flip(mdata->ifac, _("/Session/Add last graph"), s);
    }
}

/* ........................................................... */

void panel_menu_state (gboolean s)
{
    if (mdata->ifac != NULL) {
	flip(mdata->ifac, _("/Model/Pooled OLS (panel)..."), s);
	flip(mdata->ifac, _("/Data/Add variables/panel dummies"), s);
    }
}

/* ........................................................... */

void session_state (gboolean s)
{
    if (mdata->ifac != NULL) {
	flip(mdata->ifac, _("/Session/Icon view"), s);
	flip(mdata->ifac, _("/Session/Save"), s);
	flip(mdata->ifac, _("/Session/Save as..."), s);
    }	
}

/* ........................................................... */

void restore_sample_state (gboolean s)
{
    if (mdata->ifac != NULL) {
	flip(mdata->ifac, _("/Sample/Restore full range"), s);
	flip(mdata->ifac, _("/Variable/Delete last variable"), !s);
    }
}

/* ........................................................... */

gint main_popup (GtkWidget *widget, GdkEventButton *event, 
		 gpointer data)
{
    GdkWindow *topwin;
    GdkModifierType mods;

    if (mdata->active_var == 0) return FALSE;
    topwin = gtk_widget_get_parent_window(mdata->listbox);
    gdk_window_get_pointer(topwin, NULL, NULL, &mods); 
    if (mods & GDK_BUTTON3_MASK) { 
	if (mdata->popup) g_free(mdata->popup);
	mdata->popup = build_var_menu();
	gtk_menu_popup(GTK_MENU(mdata->popup), NULL, NULL, NULL, NULL,
		       event->button, event->time);
    }
    return TRUE;
}

/* ........................................................... */

gint populate_clist (GtkWidget *widget, DATAINFO *datainfo)
{
    char id[4];
    char *row[3];
    gint i;

    gtk_clist_clear(GTK_CLIST (widget));
    for (i=0; i<datainfo->v; i++) {
	if (hidden_var(i, datainfo)) continue;
	sprintf(id, "%d", i);
	row[0] = id;
	row[1] = datainfo->varname[i];
	row[2] = datainfo->label[i];
	gtk_clist_append(GTK_CLIST (widget), row);
    }
    mdata->active_var = 1;
    if (mdata->active_var > datainfo->v - 1)
	mdata->active_var -= 1;
    gtk_clist_select_row 
	(GTK_CLIST (mdata->listbox), mdata->active_var, 1);  

    if (!popup_connected) {
	gtk_signal_connect(GTK_OBJECT(mdata->listbox),
			   "button_press_event",
			   (GtkSignalFunc) main_popup, NULL);
	popup_connected = 1;
    }
    return 0;
}

/* ........................................................... */

void clear_clist (GtkWidget *widget)
{
    gtk_clist_clear(GTK_CLIST(widget));
    if (popup_connected) {
	gtk_signal_disconnect_by_func(GTK_OBJECT(mdata->listbox),
				      (GtkSignalFunc) main_popup, 
				      NULL);
	popup_connected = 0;
    }
}

/* ......................................................... */

void clear_sample_label (void)
{
    gtk_label_set_text(GTK_LABEL(mdata->status), "");
    gtk_frame_set_label(GTK_FRAME(dataframe), _(" No datafile loaded "));
}

/* ......................................................... */

void set_sample_label (DATAINFO *pdinfo)
{
    char startdate[9], enddate[9], pdstr[10];
    char labeltxt[80];

    ntodate(startdate, pdinfo->t1, pdinfo);
    ntodate(enddate, pdinfo->t2, pdinfo);

    if (dataset_is_time_series(pdinfo)) {
	switch (pdinfo->pd) {
	case 1:
	    strcpy(pdstr, _("Annual")); break;
	case 4:
	    strcpy(pdstr, _("Quarterly")); break;
	case 12:
	    strcpy(pdstr, _("Monthly")); break;
	case 24:
	    strcpy(pdstr, _("Hourly")); break;
	case 52:
	    strcpy(pdstr, _("Weekly")); break;
	case 5:
	    strcpy(pdstr, _("Daily")); break;
	case 7:
	    strcpy(pdstr, _("Daily")); break;
	default:
	    strcpy(pdstr, _("Unknown")); break;
	}
    } 
    else if (dataset_is_panel(pdinfo)) 
	strcpy(pdstr, _("Panel"));
    else 
	strcpy(pdstr, _("Undated"));

    panel_menu_state(dataset_is_panel(pdinfo));

    flip(mdata->ifac, _("/Sample/Interpret as time series..."), 
	 !(dataset_is_time_series(pdinfo)));

    flip(mdata->ifac, _("/Sample/Interpret as panel..."), 
	 !(pdinfo->pd == 1));

    sprintf(labeltxt, _("%s: Full range %s - %s; current sample"
	    " %s - %s"), pdstr, pdinfo->stobs, pdinfo->endobs,
	    startdate, enddate);
    gtk_label_set_text(GTK_LABEL(mdata->status), labeltxt);

    if (strlen(paths.datfile) > 2) {
	if (strrchr(paths.datfile, SLASH) == NULL)
	    sprintf(labeltxt, " %s ", paths.datfile);
	else
	    sprintf(labeltxt, " %s ", 
		    strrchr(paths.datfile, SLASH) + 1);
	if (data_status & MODIFIED_DATA) 
	    strcat(labeltxt, "* ");
	if (dataframe != NULL)
	    gtk_frame_set_label(GTK_FRAME(dataframe), labeltxt);
    } 
    else if (data_status & MODIFIED_DATA) {
	strcpy(labeltxt, _(" Unsaved data "));
	gtk_frame_set_label(GTK_FRAME(dataframe), labeltxt);
    }
}

/* ......................................................... */

#ifdef G_OS_WIN32

#define NAME_BUFFER_LEN 32

static int get_windows_font (char *fontspec)
{
    HDC h_dc;
    HGDIOBJ h_font;
    TEXTMETRIC tm;
    char name[NAME_BUFFER_LEN];
    int len, pix_height;

    h_dc = CreateDC("DISPLAY", NULL, NULL, NULL);
    if (h_dc == NULL) return 1;
    h_font = GetStockObject(DEFAULT_GUI_FONT);
    if (h_font == NULL || !SelectObject(h_dc, h_font)) {
	DeleteDC(h_dc);
	return 1;
    }
    len = GetTextFace(h_dc, NAME_BUFFER_LEN, name);
    if (len <= 0) {
	DeleteDC(h_dc);
	return 1;
    }
    if (!GetTextMetrics(h_dc, &tm)) {
	DeleteDC(h_dc);
	return 1;
    }
    pix_height = tm.tmHeight;
    DeleteDC(h_dc);
    sprintf(fontspec, "-*-%s-*-*-*-*-%i-*-*-*-p-*-iso8859-1", name,
	    pix_height);
    return 0;
}
#endif

/* ......................................................... */

static GtkWidget *make_main_window (int gui_get_data) 
{
    GtkWidget *box, *scroller, *dframe;
    char *titles[3] = {"ID #", _("Variable name"), _("Descriptive label")};
    int listbox_id_width = 30;
    int listbox_varname_width = 100;
    int listbox_label_width = 400;
    int listbox_data_width = 480;
    int listbox_file_height = 300;
#ifdef G_OS_WIN32
    GtkStyle *style;
    char winfont[128];
#endif

    mdata->data = NULL;  
    mdata->listbox = NULL;
    mdata->popup = NULL;
    mdata->role = MAINWIN;

#ifdef USE_GNOME
    mdata->w = gnome_app_new("gretl", _("Econometrics program"));
#else
    mdata->w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif

#ifdef G_OS_WIN32
    style = gtk_widget_get_style(mdata->w);
    if (get_windows_font(winfont) == 0)
	style->font = gdk_font_load(winfont);
    else
	gdk_font_load("-microsoft-tahoma-medium-r-normal--*-100-*-*-*-*-*-*");
    if (style->font) gtk_widget_set_style(mdata->w, style);
#endif

    gtk_signal_connect (GTK_OBJECT (mdata->w), "delete_event",
			GTK_SIGNAL_FUNC (exit_check), NULL);
    gtk_signal_connect (GTK_OBJECT (mdata->w), "destroy",
			GTK_SIGNAL_FUNC (destroy), NULL);

    gtk_window_set_title(GTK_WINDOW (mdata->w), "gretl");
    gtk_window_set_policy(GTK_WINDOW (mdata->w), TRUE, TRUE, FALSE);
#ifndef G_OS_WIN32
    gtk_signal_connect_after(GTK_OBJECT(mdata->w), "realize", 
                             GTK_SIGNAL_FUNC(set_wm_icon), 
                             NULL);
#endif

    main_vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER (main_vbox), 10);

#ifdef USE_GNOME
    gnome_app_set_contents (GNOME_APP (mdata->w), main_vbox);
#else
    gtk_container_add(GTK_CONTAINER (mdata->w), main_vbox);
#endif

    set_up_main_menu();
    gtk_box_pack_start(GTK_BOX (main_vbox), mdata->mbar, FALSE, TRUE, 0);
    gtk_widget_show(mdata->mbar);

    mdata->active_var = 1; 

    dframe = gtk_frame_new(_(" No datafile loaded ")); 
    gtk_widget_set_usize(dframe, listbox_data_width, listbox_file_height);
    gtk_widget_show(dframe);

    box = gtk_vbox_new (FALSE, 0);
    gtk_container_border_width (GTK_CONTAINER (box), 5);
    gtk_container_add (GTK_CONTAINER (dframe), box);
   
    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    mdata->listbox = gtk_clist_new_with_titles (3, titles);
    gtk_clist_column_titles_passive(GTK_CLIST(mdata->listbox));
    gtk_container_add (GTK_CONTAINER (scroller), mdata->listbox);
    gtk_clist_set_selection_mode (GTK_CLIST (mdata->listbox), 
				  GTK_SELECTION_BROWSE);
    gtk_clist_set_column_width (GTK_CLIST (mdata->listbox), 
				0, listbox_id_width);
    gtk_clist_set_column_justification (GTK_CLIST (mdata->listbox), 0, 
					GTK_JUSTIFY_LEFT);
    setup_column(mdata->listbox, 1, listbox_varname_width);
    setup_column(mdata->listbox, 2, listbox_label_width);

    gtk_drag_dest_set (mdata->listbox,
		       GTK_DEST_DEFAULT_ALL,
		       target_table, 1,
		       GDK_ACTION_COPY);
    gtk_signal_connect (GTK_OBJECT(mdata->listbox), "drag_data_received",
			GTK_SIGNAL_FUNC(drag_data_received),
			NULL);

    gtk_box_pack_start (GTK_BOX (box), scroller, TRUE, TRUE, TRUE);
    gtk_signal_connect_after (GTK_OBJECT (mdata->listbox), "select_row", 
			      GTK_SIGNAL_FUNC (selectrow), (gpointer) mdata);
    gtk_widget_show(mdata->listbox);
    gtk_widget_show(scroller);

    gtk_widget_show(box);

    gtk_box_pack_start(GTK_BOX (main_vbox), dframe, TRUE, TRUE, 0);

    mdata->status = gtk_label_new("");
    
    gtk_box_pack_start (GTK_BOX (main_vbox), mdata->status, FALSE, TRUE, 0);

    /* put stuff into clist, activate menus */
    if (!gui_get_data) 
	populate_clist(mdata->listbox, datainfo);

    /* create gretl toolbar */
    if (want_toolbar)
	make_toolbar(mdata->w, main_vbox);

    /* get a monospaced font for various windows */
    load_fixed_font();

    gtk_widget_show_all(mdata->w); 

    return dframe;
}

/* ........................................................... */

static void set_up_main_menu (void)
{
    GtkAccelGroup *main_accel;
   
    gint n_items = sizeof data_items / sizeof data_items[0];

    main_accel = gtk_accel_group_new();
    mdata->ifac = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", 
				     main_accel);
    gtk_item_factory_create_items (mdata->ifac, n_items, data_items, NULL);
    mdata->mbar = gtk_item_factory_get_widget (mdata->ifac, "<main>");
    gtk_accel_group_attach(main_accel, GTK_OBJECT (mdata->w));
}

/* ........................................................... */

extern void delete_var_by_id (int id); /* callbacks.c */

static gint popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item;

    item = (gchar *) data;
    if (!strcmp(item, _("Display values"))) display_var();
    if (!strcmp(item, _("Descriptive statistics"))) 
	do_menu_op(NULL, VAR_SUMMARY, NULL);
    else if (!strcmp(item, _("Time series plot"))) do_graph_var();
    else if (!strcmp(item, _("Frequency distribution"))) 
	do_menu_op(NULL, FREQ, NULL);
    else if (!strcmp(item, _("Frequency plot"))) do_freqplot(NULL, 0, NULL);
    else if (!strcmp(item, _("Boxplot")))
	do_boxplot_var();
    else if (!strcmp(item, _("Correlogram"))) 
	gretl_callback(NULL, CORRGM, NULL);
    else if (!strcmp(item, _("Spectrum"))) 
	do_pergm(NULL, 0, NULL);
    else if (!strcmp(item, _("Dickey-Fuller test"))) 
	gretl_callback(NULL, ADF, NULL);
    else if (!strcmp(item, _("Runs test"))) 
	do_menu_op(NULL, RUNS, NULL);
    else if (!strcmp(item, _("Rename"))) 
	gretl_callback(NULL, RENAME, NULL);
    else if (!strcmp(item, _("Edit label"))) 
	gretl_callback(NULL, RELABEL, NULL);
    else if (!strcmp(item, _("Delete"))) 
	delete_var_by_id(mdata->active_var);
    else if (!strcmp(item, _("Simulate..."))) 
	gretl_callback(NULL, SIM, NULL);
    else if (!strcmp(item, _("Define new variable..."))) 
	gretl_callback(NULL, GENR, NULL);
    gtk_widget_destroy(mdata->popup);
    return TRUE;
}

/* ........................................................... */

static GtkWidget *build_var_menu (void)
{
    static char *var_items[]={
	_("Display values"),
	_("Descriptive statistics"),
	_("Time series plot"),
	_("Frequency distribution"),
	_("Frequency plot"),
	_("Boxplot"),
	_("Correlogram"),
	_("Spectrum"),
	_("Dickey-Fuller test"),
	_("Runs test"),
	_("Rename"),
	_("Edit label"),
	_("Delete"),
	_("Simulate..."),
	_("Define new variable...")
    };

    GtkWidget *var_menu;
    GtkWidget *var_item;
    int i;

    var_menu = gtk_menu_new();
    for (i=0; i<(sizeof var_items / sizeof var_items[0]); i++) {
	var_item = gtk_menu_item_new_with_label(var_items[i]);
	gtk_signal_connect(GTK_OBJECT(var_item), "activate",
			   (GtkSignalFunc) popup_activated,
			   var_items[i]);
	GTK_WIDGET_SET_FLAGS (var_item, GTK_SENSITIVE | GTK_CAN_FOCUS);
	gtk_widget_show(var_item);
	gtk_menu_append(GTK_MENU(var_menu), var_item);
    }
    return var_menu;
}

/* ........................................................... */

static void check_for_pwt (void)
{
    DIR *dir;
    char pwtdir[MAXLEN];
    extern char pwtpath[MAXLEN];

    sprintf(pwtdir, "%spwt56%c", paths.datadir, SLASH);
    if ((dir = opendir(pwtdir)) != NULL) {
	closedir(dir);
	strcpy(pwtpath, pwtdir);
	return;
    }
    sprintf(pwtdir, "%spwt56%c", paths.userdir, SLASH);
    if ((dir = opendir(pwtdir)) != NULL) {
	closedir(dir);
	strcpy(pwtpath, pwtdir);
	return;
    }
    gtk_widget_set_sensitive(gtk_item_factory_get_item
			     (mdata->ifac, 
			      _("/File/Open data/sample file/Penn World Table...")), 
			     FALSE);
    gtk_widget_set_sensitive(gtk_item_factory_get_item
			     (mdata->ifac, 
			      _("/File/Open command file/practice file/"
			      "Penn World Table...")), 
			     FALSE);
}

/* ........................................................... */

void restore_sample (gpointer data, int verbose, GtkWidget *w)
{
    int err = 0;

    err = restore_full_sample(&subZ, &fullZ, &Z,
			      &subinfo, &fullinfo, &datainfo);
    if (err) {
	gui_errmsg(err);
	return;
    }
    if (verbose) {
	infobox(_("Full sample range restored"));
	set_sample_label(datainfo);    
	restore_sample_state(FALSE);
	strcpy(line, "smpl full");
	check_cmd(line);
	cmd_init(line);
    }
}

/* ........................................................... */

#ifdef G_OS_WIN32
BOOL CreateChildProcess (char *prog) 
{ 
   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFO siStartInfo; 
 
   ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
   ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
   siStartInfo.cb = sizeof(STARTUPINFO); 
 
   return CreateProcess(
      NULL, 
      prog,          /* command line */
      NULL,          /* process security attributes  */
      NULL,          /* primary thread security attributes */ 
      TRUE,          /* handles are inherited  */
      0,             /* creation flags  */
      NULL,          /* use parent's environment  */
      NULL,          /* use parent's current directory  */
      &siStartInfo,  /* STARTUPINFO pointer */ 
      &piProcInfo);  /* receives PROCESS_INFORMATION  */
}
#endif	

/* ........................................................... */

static void startR (gpointer p, guint opt, GtkWidget *w)
{
    char Rdata[MAXLEN], line[MAXLEN];
    FILE *fp;
#ifndef G_OS_WIN32
    int i;
    char *s0, *s1, *s2;
    pid_t pid;
#endif

    if (!data_status) {
	errbox(_("Please open a data file first"));
	return;
    }

    fp = fopen(".Rprofile", "r");
    if (fp != NULL) {
	fclose(fp);
	if (copyfile(".Rprofile", ".Rprofile.gretltmp")) {
	    errbox(_("Couldn't move existing .Rprofile out of the way"));
	    return;
	}
    }
    fp = fopen(".Rprofile", "w");
    if (fp == NULL) {
	errbox(_("Couldn't write R startup file"));
	return;
    }
    sprintf(Rdata, "%sRdata.tmp", paths.userdir);
    sprintf(line, "store -r %s", Rdata); 
    if (check_cmd(line) || cmd_init(line) ||
	write_data(Rdata, command.list, Z, datainfo, OPT_R, NULL)) {
	errbox(_("Write of R data file failed"));
	fclose(fp);
	return; 
    }
    if (dataset_is_time_series(datainfo)) {
	fprintf(fp, "source(\"%s\")\n", Rdata);
	fprintf(fp, "ls()\n");
    } else {
	fprintf(fp, "gretldata <- read.table(\"%s\")\n", Rdata);
	fprintf(fp, "attach(gretldata)\n");
	fprintf(fp, "ls(2)\n");
    }
    fclose(fp);

#ifdef G_OS_WIN32
    CreateChildProcess(Rcommand);
#else
    s0 = mymalloc(64);
    s1 = mymalloc(32);
    s2 = mymalloc(32);
    if (s0 == NULL || s0 == NULL || s0 == NULL)
	return;
    s0[0] = s1[0] = s2[0] = 0;
    i = sscanf(Rcommand, "%63s %31s %31s", s0, s1, s2);
    if (i == 0) {
	errbox(_("No command was supplied to start R"));
	free(s0); free(s1); free(s2);
	return;
    }

    pid = fork();

    if (pid == -1) {
	errbox(_("Couldn't fork"));
	perror("fork");
	return;
    } else if (pid == 0) {  
	if (i == 1)
	    execlp(s0, s0, NULL);
	else if (i == 2)
	    execlp(s0, s0, s1, NULL);
	else if (i == 3)
	    execlp(s0, s0, s1, s2, NULL);
	perror("execlp");
	_exit(EXIT_FAILURE);
    }
    free(s0); 
    free(s1); 
    free(s2);
#endif 
}

/* ........................................................... */

static void Rcleanup (void)
{
    FILE *fp;
    char Rdata[MAXLEN];

    sprintf(Rdata, "%sRdata.tmp", paths.userdir);
    remove(Rdata);

    fp = fopen(".Rprofile.gretltmp", "r");
    if (fp != NULL) {
	fclose(fp);
	if (copyfile(".Rprofile.gretltmp", ".Rprofile")) 
	    errbox(_("Error restoring .Rprofile from\n"
		   "the temporary copy, .Rprofile.gretltmp"));
	else 
	    remove(".Rprofile.gretltmp");
    }
}

/* ........................................................... */

static void show_calc (void)
{
#ifdef G_OS_WIN32
    CreateChildProcess(calculator);
#else
    pid_t pid = fork();

    if (pid == -1) {
	errbox(_("Couldn't fork"));
	perror("fork");
	return;
    } else if (pid == 0) {  
	execlp(calculator, calculator, NULL);
	perror("execlp");
	_exit(EXIT_FAILURE);
    }
#endif 
}

/* ........................................................... */

static void show_edit (void)
{
#ifdef G_OS_WIN32
    CreateChildProcess(editor);
#else
    pid_t pid = fork();

    if (pid == -1) {
	errbox(_("Couldn't fork"));
	perror("fork");
	return;
    } else if (pid == 0) {  
	execlp(editor, editor, NULL);
	perror("execlp");
	_exit(EXIT_FAILURE);
    }
#endif 
}

/* ........................................................... */

static void open_ramudata (void)
{
    display_files(NULL, RAMU_DATA, NULL);
}

/* ........................................................... */

void colorize_tooltips (GtkTooltips *tip)
{
    GdkColor t_back;
    GtkStyle *style;

    if (gdk_color_parse("light yellow", &t_back)) {
	gtk_tooltips_force_window(tip);
	if (gdk_color_alloc(gtk_widget_get_colormap(tip->tip_window), &t_back)) {
	    style = gtk_style_copy(gtk_widget_get_style(tip->tip_window));
	    style->bg[GTK_STATE_NORMAL] = t_back;
	    gtk_widget_set_style(tip->tip_window, style);
	} 
    } 
}

/* ........................................................... */

void show_toolbar (void)
{
    make_toolbar(mdata->w, main_vbox);
}

/* ........................................................... */

#ifdef G_OS_WIN32
extern int goto_url (const char *url);
#else
static void netscape_open (const char *url)
{
#ifdef USE_GNOME
    gnome_url_show(url);   
#else
    int err;
    char ns_cmd[128];

    sprintf(ns_cmd, "netscape -remote \"openURLNewWindow(%s)\"", url);
    err = system(ns_cmd);
    if (err) {
	pid_t pid = fork();

	if (pid == -1) {
	    errbox(_("Couldn't fork"));
	    perror("fork");
	    return;
	} else if (pid == 0) {
	    execlp("netscape", "netscape", url, NULL);
	    perror("execlp");
	    _exit(EXIT_FAILURE);
	}
    }
#endif /* USE_GNOME */
}
#endif /* G_OS_WIN32 */

static void gretl_website (void)
{
#ifdef G_OS_WIN32
    if (goto_url("http://gretl.sourceforge.net/"))
	errbox("Failed to open URL");
#else
    netscape_open("http://gretl.sourceforge.net/");
#endif
}

static void gretl_pdf (void)
{
#ifdef G_OS_WIN32
    if (goto_url("http://gretl.sourceforge.net/manual.pdf"))
	errbox(_("Failed to open URL"));
#else
    netscape_open("http://gretl.sourceforge.net/manual.pdf");
#endif
}

static void xy_graph (void)
{
    if (data_status)
	graph_dialog(NULL, GR_XY, NULL);
    else
	errbox(_("Please open a data file first"));
}

static void go_session (void)
{
    if (data_status)
	view_session();
    else
	errbox(_("Please open a data file first"));
}

/* ........................................................... */

static void make_toolbar (GtkWidget *w, GtkWidget *box)
{
    GtkWidget *iconw, *button;
    GdkPixmap *icon;
    GdkBitmap *mask;
    GdkColormap *colormap;
    int i;
    static char *toolstrings[] = {_("launch calculator"), 
				  _("launch editor"), 
				  _("open gretl console"),
				  _("session icon view"),
				  _("gretl website"), 
				  _("gretl manual (PDF)"),
				  _("show help"), 
				  _("X-Y graph"), 
				  _("Capture last graph for editing"),
				  _("open dataset"),
				  NULL
    };
    gchar **toolxpm = NULL;
    void (*toolfunc)() = NULL;

    colormap = gdk_colormap_get_system();
    toolbar_box = gtk_handle_box_new();
    gtk_handle_box_set_shadow_type(GTK_HANDLE_BOX(toolbar_box), NONE);
    gtk_box_pack_start(GTK_BOX(box), toolbar_box, FALSE, FALSE, 0);

    gretl_toolbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
				    GTK_TOOLBAR_ICONS);
    gtk_container_set_border_width(GTK_CONTAINER(gretl_toolbar), 0);
    gtk_toolbar_set_space_size(GTK_TOOLBAR(gretl_toolbar), 0);
    gtk_container_add(GTK_CONTAINER(toolbar_box), gretl_toolbar);

    colorize_tooltips(GTK_TOOLBAR(gretl_toolbar)->tooltips);

    for (i=0; toolstrings[i] != NULL; i++) {
	switch (i) {
	case 0:
	    toolxpm = mini_calc_xpm;
	    toolfunc = show_calc;
	    break;
	case 1:
	    toolxpm = mini_edit_xpm;
	    toolfunc = show_edit;
	    break;
	case 2:
	    toolxpm = mini_sh_xpm;
	    toolfunc = console;
	    break;
	case 3:
	    toolxpm = mini_session_xpm;
	    toolfunc = go_session;
	    break;
	case 4:
	    toolxpm = mini_netscape_xpm;
	    toolfunc = gretl_website;
	    break;  
	case 5:
	    toolxpm = mini_pdf_xpm;
	    toolfunc = gretl_pdf;
	    break;    
	case 6:
	    toolxpm = mini_manual_xpm;
	    toolfunc = do_gui_help;
	    break;
	case 7:
	    toolxpm = mini_plot_xpm;
	    toolfunc = xy_graph;
	    break;
	case 8:
	    toolxpm = mini_camera_xpm;
	    toolfunc = add_last_graph;
	    break;
	case 9:
	    toolxpm = mini_ofolder_xpm;
	    toolfunc = open_ramudata;
	    break;
	default:
	    break;
	}

	icon = gdk_pixmap_colormap_create_from_xpm_d(NULL, colormap, &mask, NULL, 
						     toolxpm);
	iconw = gtk_pixmap_new(icon, mask);
	button = gtk_toolbar_append_item(GTK_TOOLBAR(gretl_toolbar),
					 NULL, toolstrings[i], NULL,
					 iconw,
					 toolfunc, NULL);
    }
    gtk_widget_show(gretl_toolbar);
    gtk_widget_show(toolbar_box);
}

/* Icon handling for X */
#ifndef G_OS_WIN32
void set_wm_icon (GtkWidget *w, gpointer data)
{
    GdkPixmap *icon;

    icon = gdk_pixmap_create_from_xpm_d(w->window, NULL, NULL, gretl_xpm);
    gdk_window_set_icon(w->window, NULL, icon, NULL);
}
#endif

/* Drag 'n' drop */
static void  
drag_data_received  (GtkWidget *widget,
		     GdkDragContext *context,
		     gint x,
		     gint y,
		     GtkSelectionData *data,
		     guint info,
		     guint time,
		     gpointer p)
{
    gchar *dfname;
    char *suff = NULL, tmp[MAXLEN];
    int pos, skip = 5;

    /* ignore the wrong sort of data */
    if (data == NULL || (dfname = data->data) == NULL || 
	strlen(dfname) <= 5 || strncmp(dfname, "file:", 5))
	return;

    if (strncmp(dfname, "file://", 7)) skip = 7;

    /* there may be multiple files: we ignore all but the first */
    tmp[0] ='\0';
    if ((pos = haschar('\r', dfname)) > 0 || 
	(pos = haschar('\n', dfname) > 0)) {
	strncat(tmp, dfname + skip, pos - skip);
    } else
	strcat(tmp, dfname + skip);

#ifdef G_OS_WIN32
    if (unmangle(tmp, tryscript)) return;
    strcpy(tmp, tryscript);
    tryscript[0] = '\0';
#endif

    suff = strrchr(tmp, '.');
    if (suff && (!strncmp(suff, ".gretl", 6) || 
		 !strncmp(suff, ".inp", 4) ||
		 !strncmp(suff, ".GRE", 4) ||
		 !strncmp(suff, ".INP", 4))) {
	strcpy(tryscript, tmp);
	verify_open_session(NULL);
    } else {
	strcpy(trydatfile, tmp);
	verify_open_data(NULL);
    }	
}

/* ........................................................... */

static gint 
special_selection_get (GtkWidget *widget,
		       GtkSelectionData *selection_data,
		       guint info,
		       guint time)
{
    gchar *str;
    gint length;

    str = clipboard_buf;
    if (str == NULL) return TRUE;
    length = strlen(str);
  
    if (info == TARGET_STRING) {
	gtk_selection_data_set (selection_data,
				GDK_SELECTION_TYPE_STRING,
				8 * sizeof(gchar), 
				(guchar *) str, 
				length);
    } else if (info == TARGET_TEXT || info == TARGET_COMPOUND_TEXT) {
	guchar *text;
	gchar c;
	GdkAtom encoding;
	gint format;
	gint new_length;

	c = str[length];
	str[length] = '\0';
	gdk_string_to_compound_text(str, &encoding, &format, 
				    &text, &new_length);
	gtk_selection_data_set(selection_data, encoding, format, 
			       text, new_length);
	gdk_free_compound_text(text);
	str[length] = c;
    }
    g_free(str);
    clipboard_buf = NULL;
    return TRUE;
}

/* ........................................................... */

static void clip_init (GtkWidget *w)
{
    GdkAtom clipboard_atom = GDK_NONE;
    GtkTargetEntry targets[] = {
	{ "STRING", 0, TARGET_STRING },
	{ "TEXT",   0, TARGET_TEXT }, 
	{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
    };

    gint n_targets = sizeof(targets) / sizeof(targets[0]);
  
    clipboard_atom = gdk_atom_intern("CLIPBOARD", FALSE);
    gtk_selection_add_targets(w, GDK_SELECTION_PRIMARY,
			      targets, n_targets);
    gtk_selection_add_targets(w, clipboard_atom,
			      targets, n_targets);
    gtk_signal_connect (GTK_OBJECT(mdata->w), "selection_get",
			GTK_SIGNAL_FUNC(special_selection_get), NULL);    
}

/* ........................................................... */

static void auto_store (void)
{
    int opt = 0;

    if (make_default_storelist()) return;

    /* if there's already a datafile, and it's gzipped, then
       arrange for the new store to be gzipped too */
    if (strlen(paths.datfile) && is_gzipped(paths.datfile))
	opt = OPT_Z;

    if (data_status & USER_DATA)
	do_store(paths.datfile, opt, 1);
    else
	file_selector(_("Save data file"), SAVE_DATA, NULL);	
}

/* ........................................................... */

#ifdef G_OS_WIN32

static int old_windows (void) {
    OSVERSIONINFO *winver;
    static int old = 1;

    if (!old) return 0; /* do only one look up */

    winver = mymalloc(sizeof *winver);
    if (winver == NULL) return old;
    winver->dwOSVersionInfoSize = sizeof *winver;
    GetVersionEx(winver);
    switch (winver->dwPlatformId) {
    case VER_PLATFORM_WIN32_WINDOWS:
        if (winver->dwMinorVersion >= 10) /* win98 or higher */
	    old = 0;
        break;
    case VER_PLATFORM_WIN32_NT:
        if (winver->dwMajorVersion > 4) /* win2000 or higher */
	    old = 0;
        break;
    }
    free(winver);
    return old;
}

static int unmangle (const char *dosname, char *longname)
{
    if (old_windows()) {
	/* sorry but I really can't be bothered */
	strcpy(longname, dosname);
	return 0;
    } else {
	int err;
	void *handle;
	void (*real_unmangle)(const char *, char *, int, int *); 
	
	if (gui_open_plugin("longname", &handle)) return 1;

	real_unmangle = get_plugin_function("real_unmangle", handle);
	if (real_unmangle == NULL) return 1;
	(*real_unmangle)(dosname, longname, MAXLEN, &err);
	close_plugin(handle);
	return err;
    }
}

#endif

