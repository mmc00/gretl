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

#include "gretl.h"
#include "var.h"
#include "johansen.h"
#include "varprint.h"
#include "forecast.h"
#include "objstack.h"
#include "gretl_xml.h"
#include "gretl_func.h"
#include "system.h"
#include "matrix_extra.h"
#include "bootstrap.h"
#include "gretl_foreign.h"
#include "gretl_bundle.h"
#include "gretl_scalar.h"
#include "gretl_string_table.h"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "model_table.h"
#include "series_view.h"
#include "session.h"
#include "textbuf.h"
#include "textutil.h"
#include "cmdstack.h"
#include "filelists.h"
#include "menustate.h"
#include "dlgutils.h"
#include "ssheet.h"
#include "datafiles.h"
#include "gpt_control.h"
#include "fileselect.h"
#include "toolbar.h"
#include "winstack.h"
#include "fnsave.h"
#include "datawiz.h"
#include "selector.h"
#include "guiprint.h"

#ifdef G_OS_WIN32
# include <windows.h>
# include "gretlwin32.h"
#endif

char *storelist = NULL;

static void set_up_model_view_menu (GtkWidget *window, windata_t *vwin);
static void add_system_menu_items (windata_t *vwin, int vecm);
static void add_bundle_menu_items (windata_t *vwin);
static void add_x12_output_menu_item (windata_t *vwin);
static gint check_model_menu (GtkWidget *w, GdkEventButton *eb, 
			      gpointer data);
static gint check_VAR_menu (GtkWidget *w, GdkEventButton *eb, 
			    gpointer data);
static void model_copy_callback (GtkAction *action, gpointer p);
static int set_sample_from_model (MODEL *pmod);

static void close_model (GtkAction *action, gpointer data)
{
    windata_t *vwin = (windata_t *) data;

    if (window_is_busy(vwin)) {
	maybe_raise_dialog();
    } else {
	gtk_widget_destroy(vwin->main);
    }
}

static int arma_by_x12a (const MODEL *pmod)
{
    int ret = 0;

    if (pmod->ci == ARMA) {
	int acode = gretl_model_get_int(pmod, "arma_flags");

	if (acode & ARMA_X12A) {
	    ret = 1;
	}
    }

    return ret;
}

int latex_is_ok (void)
{
    static int latex_ok = -1; 
  
    if (latex_ok == -1) {
	latex_ok = check_for_prog(latex);
    }

    return latex_ok;
}

static void model_output_save (GtkAction *action, gpointer p)
{
    copy_format_dialog((windata_t *) p, W_SAVE);    
}

static gretlopt tex_eqn_opt;

static void set_tex_eqn_opt (GtkRadioAction *action)
{
    int v = gtk_radio_action_get_current_value(action);

    tex_eqn_opt = (v)? OPT_T : OPT_NONE;
}

gretlopt get_tex_eqn_opt (void)
{
    return tex_eqn_opt;
}

static void model_revise_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    
    selector_from_model(vwin->data, vwin->role);
}

#if 0
static void model_sample_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = (MODEL *) vwin->data;
    
    set_sample_from_model(pmod);
}
#endif

gchar *gretl_window_title (const char *s1, const char *s2)
{
    if (s1 != NULL && s2 != NULL) {
	return g_strdup_printf("gretl: %s %s", s1, s2);
    } else if (s1 != NULL) {
	return g_strdup_printf("gretl: %s", s1);
    } else {
	return g_strdup("gretl: untitled");
    }
}

static void text_eqn_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = vwin->data;
    PRN *prn;
    int err;

    if (bufopen(&prn)) {
	return;
    }
    
    err = text_print_equation(pmod, datainfo, OPT_NONE, prn);

    if (err) {
	gui_errmsg(err);
    } else {
	gchar *title = gretl_window_title(_("equation"), NULL);
	windata_t *viewer;

	viewer = view_buffer_with_parent(vwin, prn, 78, 200, 
					 title, PRINT, NULL);
	g_free(title);			   
    }
}

static GtkActionEntry model_items[] = {
    { "File", NULL, N_("_File"), NULL, NULL, NULL },
    { "SaveAs", GTK_STOCK_SAVE_AS, N_("_Save as..."), NULL, NULL, G_CALLBACK(model_output_save) },
    { "SaveAsIcon", NULL, N_("Save to session as _icon"), NULL, NULL, G_CALLBACK(model_add_as_icon) },
    { "SaveAndClose", NULL, N_("Save as icon and cl_ose"), NULL, NULL, G_CALLBACK(model_add_as_icon) },
#ifdef NATIVE_PRINTING
    { "Print", GTK_STOCK_PRINT, N_("_Print..."), NULL, NULL, G_CALLBACK(window_print) },
#endif
    { "TextEqn", NULL, N_("View as equation"), NULL, NULL, G_CALLBACK(text_eqn_callback) },
    { "Close", GTK_STOCK_CLOSE, N_("_Close"), NULL, NULL, G_CALLBACK(close_model) },
    { "Edit", NULL, N_("_Edit"), NULL, NULL, NULL },    
    { "Copy", GTK_STOCK_COPY, N_("_Copy"), NULL, NULL, G_CALLBACK(model_copy_callback) },
    { "Revise", GTK_STOCK_EDIT, N_("_Modify model..."), NULL, NULL, 
      G_CALLBACK(model_revise_callback) },
#if 0
    { "Restore", NULL, N_("_Restore model sample"), NULL, NULL, G_CALLBACK(model_sample_callback) },
#endif
    { "Tests", NULL, N_("_Tests"), NULL, NULL, NULL },    
    { "Save", NULL, N_("_Save"), NULL, NULL, NULL },    
    { "Graphs", NULL, N_("_Graphs"), NULL, NULL, NULL },    
    { "ResidPlot", NULL, N_("_Residual plot"), NULL, NULL, NULL },    
    { "FittedActualPlot", NULL, N_("_Fitted, actual plot"), NULL, NULL, NULL },    
    { "Analysis", NULL, N_("_Analysis"), NULL, NULL, NULL }, 
    { "DisplayAFR", NULL, N_("_Display actual, fitted, residual"), NULL, NULL, 
      G_CALLBACK(display_fit_resid) },    
    { "Forecasts", NULL, N_("_Forecasts..."), NULL, NULL, G_CALLBACK(gui_do_forecast) },    
    { "ConfIntervals", NULL, N_("_Confidence intervals for coefficients"), NULL, NULL, 
      G_CALLBACK(do_coeff_intervals) },    
    { "ConfEllipse", NULL, N_("Confidence _ellipse..."), NULL, NULL, G_CALLBACK(selector_callback) },    
    { "Covariance", NULL, N_("Coefficient covariance _matrix"), NULL, NULL, G_CALLBACK(do_outcovmx) },    
    { "ANOVA", NULL, N_("_ANOVA"), NULL, NULL, G_CALLBACK(do_anova) },    
    { "Bootstrap", NULL, N_("_Bootstrap..."), NULL, NULL, G_CALLBACK(do_bootstrap) }    
};

static GtkActionEntry model_test_items[] = {
    { "omit", NULL, N_("_Omit variables"), NULL, NULL, G_CALLBACK(selector_callback) },
    { "add", NULL, N_("_Add variables"), NULL, NULL, G_CALLBACK(selector_callback) },
    { "coeffsum", NULL, N_("_Sum of coefficients"), NULL, NULL, G_CALLBACK(selector_callback) },
    { "restrict", NULL, N_("_Linear restrictions"), NULL, NULL, G_CALLBACK(gretl_callback) },
    { "modtest:s", NULL, N_("Non-linearity (s_quares)"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "modtest:l", NULL, N_("Non-linearity (_logs)"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "reset", NULL, N_("_Ramsey's RESET"), NULL, NULL, G_CALLBACK(do_reset) },
    { "Hsk", NULL, N_("_Heteroskedasticity"), NULL, NULL, NULL },    
    { "modtest:n", NULL, N_("_Normality of residual"), NULL, NULL, G_CALLBACK(do_resid_freq) },
    { "leverage", NULL, N_("_Influential observations"), NULL, NULL, G_CALLBACK(do_leverage) },
    { "chow", NULL, N_("_Chow test"), NULL, NULL, G_CALLBACK(do_chow_cusum) },    
    { "vif", NULL, N_("_Collinearity"), NULL, NULL, G_CALLBACK(do_vif) },
    { "modtest:a", NULL, N_("_Autocorrelation"), NULL, NULL, G_CALLBACK(do_autocorr) },
    { "dwpval", NULL, N_("_Durbin-Watson p-value"), NULL, NULL, G_CALLBACK(do_dwpval) },
    { "modtest:h", NULL, N_("A_RCH"), NULL, NULL, G_CALLBACK(do_arch) },
    { "qlrtest", NULL, N_("_QLR test"), NULL, NULL, G_CALLBACK(do_chow_cusum) },
    { "cusum", NULL, N_("_CUSUM test"), NULL, NULL, G_CALLBACK(do_chow_cusum) },
    { "cusum:r", NULL, N_("CUSUM_SQ test"), NULL, NULL, G_CALLBACK(do_chow_cusum) },
    { "modtest:c", NULL, N_("_Common factor"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "hausman", NULL, N_("_Panel diagnostics"), NULL, NULL, G_CALLBACK(do_panel_tests) }
};

static GtkActionEntry base_hsk_items[] = {
    { "White", NULL, N_("White's test"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "WhiteSquares", NULL, N_("White's test (squares only)"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "BreuschPagan", NULL, "Breusch-Pagan", NULL, NULL, G_CALLBACK(do_modtest) },
    { "Koenker", NULL, "Koenker", NULL, NULL, G_CALLBACK(do_modtest) }
};

static GtkActionEntry panel_hsk_items[] = {
    { "White", NULL, N_("White's test"), NULL, NULL, G_CALLBACK(do_modtest) },
    { "Groupwise", NULL, N_("_groupwise"), NULL, NULL, G_CALLBACK(do_modtest) }
};

static GtkActionEntry ivreg_hsk_items[] = {
    { "White", NULL, N_("Pesaran-Taylor test"), NULL, NULL, G_CALLBACK(do_modtest) }
};

const gchar *model_tex_ui = 
    "<ui>"
    "  <menubar>"
    "    <menu action='LaTeX'>"
    "      <menu action='TeXView'>"
    "        <menuitem action='TabView'/>"
    "        <menuitem action='EqnView'/>"
    "      </menu>"
    "      <menu action='TeXCopy'>"
    "        <menuitem action='TabCopy'/>"
    "        <menuitem action='EqnCopy'/>"
    "      </menu>"
    "      <menu action='TeXSave'>"
    "        <menuitem action='TabSave'/>"
    "        <menuitem action='EqnSave'/>"
    "      </menu>"
    "      <menu action='EqnOpts'>"
    "        <menuitem action='TeXstderrs'/>"
    "        <menuitem action='TeXtratios'/>"
    "      </menu>"
    "      <menuitem action='TabOpts'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";

const gchar *missing_tex_ui =
    "<ui>"
    "  <menubar>"
    "    <menu action='LaTeX'>"
    "      <menuitem action='notex'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";

static GtkActionEntry model_tex_items[] = {
    { "LaTeX",   NULL, N_("_LaTeX"), NULL, NULL, NULL },      
    { "TeXView", NULL, N_("_View"), NULL, NULL, NULL },      
    { "TabView", NULL, N_("_Tabular"), NULL, NULL, G_CALLBACK(model_tex_view) },      
    { "EqnView", NULL, N_("_Equation"), NULL, NULL, G_CALLBACK(model_tex_view) },      
    { "TeXCopy", NULL, N_("_Copy"), NULL, NULL, NULL }, 
    { "TabCopy", NULL, N_("_Tabular"), NULL, NULL, G_CALLBACK(model_tex_copy) },      
    { "EqnCopy", NULL, N_("_Equation"), NULL, NULL, G_CALLBACK(model_tex_copy) },      
    { "TeXSave", NULL, N_("_Save"), NULL, NULL, NULL }, 
    { "TabSave", NULL, N_("_Tabular"), NULL, NULL, G_CALLBACK(model_tex_save) },      
    { "EqnSave", NULL, N_("_Equation"), NULL, NULL, G_CALLBACK(model_tex_save) },      
    { "EqnOpts", NULL, N_("_Equation options"), NULL, NULL, NULL }, 
    { "TabOpts", NULL, N_("_Tabular options..."), NULL, NULL, G_CALLBACK(tex_format_dialog) }
};

static GtkRadioActionEntry tex_eqn_items[] = {
    { "TeXstderrs", NULL, N_("Show _standard errors"), NULL, NULL, 0 },
    { "TeXtratios", NULL, N_("Show _t-ratios"), NULL, NULL, 1 },
};

static GtkActionEntry missing_tex_items[] = {
    { "LaTeX", NULL, N_("_LaTeX"), NULL, NULL, NULL },    
    { "notex", NULL, "No TeX", NULL, NULL, G_CALLBACK(dummy_call) }
};

static GtkActionEntry system_items[] = {
    { "File", NULL, N_("_File"), NULL, NULL, NULL },      
    { "SaveAs", GTK_STOCK_SAVE_AS, N_("_Save as..."), NULL, NULL, G_CALLBACK(model_output_save) },      
    { "SaveAsIcon", NULL, N_("Save to session as _icon"), NULL, NULL, G_CALLBACK(model_add_as_icon) },      
    { "SaveAndClose", NULL, N_("Save as icon and cl_ose"), NULL, NULL, G_CALLBACK(model_add_as_icon) },
#ifdef NATIVE_PRINTING
    { "Print", GTK_STOCK_PRINT, N_("_Print..."), NULL, NULL, G_CALLBACK(window_print) },
#endif
    { "Close", GTK_STOCK_CLOSE, N_("_Close"), NULL, NULL, G_CALLBACK(close_model) },
    { "Edit", NULL, N_("_Edit"), NULL, NULL, NULL },      
    { "Copy", GTK_STOCK_COPY, N_("_Copy"), NULL, NULL, G_CALLBACK(model_copy_callback) }, 
    { "Revise", GTK_STOCK_EDIT, N_("_Revise specification..."), NULL, NULL, 
      G_CALLBACK(model_revise_callback) }, 
    { "Save", NULL, N_("_Save"), NULL, NULL, NULL },    
    { "Tests", NULL, N_("_Tests"), NULL, NULL, NULL },    
    { "Graphs", NULL, N_("_Graphs"), NULL, NULL, NULL },    
    { "Analysis", NULL, N_("_Analysis"), NULL, NULL, NULL },  
    { "Forecasts", NULL, N_("_Forecasts"), NULL, NULL, NULL },  
};

static gint n_system_items = G_N_ELEMENTS(system_items);

static GtkActionEntry sys_tex_items[] = {
    { "LaTeX",   NULL, N_("_LaTeX"), NULL, NULL, NULL },  
    { "TeXView", NULL, N_("_View"),  NULL, NULL, G_CALLBACK(model_tex_view) },      
    { "TeXCopy", NULL, N_("_Copy"),  NULL, NULL, G_CALLBACK(model_tex_copy) },      
    { "TeXSave", NULL, N_("_Save"),  NULL, NULL, G_CALLBACK(model_tex_save) },
};

static const gchar *sys_ui =
    "<ui>"
    "  <menubar>"
    "    <menu action='File'>"
    "      <menuitem action='SaveAs'/>"
    "      <menuitem action='SaveAsIcon'/>"
    "      <menuitem action='SaveAndClose'/>"
#ifdef NATIVE_PRINTING
    "      <menuitem action='Print'/>"
#endif
    "      <menuitem action='Close'/>"
    "    </menu>"
    "    <menu action='Edit'>"
    "      <menuitem action='Copy'/>"
    "      <menuitem action='Revise'/>"
    "    </menu>"
    "    <menu action='Tests'/>"
    "    <menu action='Save'/>"
    "    <menu action='Graphs'/>"
    "    <menu action='Analysis'>"
    "      <menu action='Forecasts'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";

static GtkActionEntry bundle_items[] = {
    { "File", NULL, N_("_File"), NULL, NULL, NULL },  
    { "SaveAs", GTK_STOCK_SAVE_AS, N_("_Save text as..."), NULL, NULL, 
      G_CALLBACK(model_output_save) },      
#ifdef NATIVE_PRINTING
    { "Print", GTK_STOCK_PRINT, N_("_Print..."), NULL, NULL, G_CALLBACK(window_print) },
#endif
    { "Close", GTK_STOCK_CLOSE, N_("_Close"), NULL, NULL, G_CALLBACK(close_model) },
    { "Save", NULL, N_("_Save"), NULL, NULL, NULL },    
};

static gint n_bundle_items = G_N_ELEMENTS(bundle_items);

static const gchar *bundle_ui =
    "<ui>"
    "  <menubar>"
    "    <menu action='File'>"
    "      <menuitem action='SaveAs'/>"
#ifdef NATIVE_PRINTING
    "      <menuitem action='Print'/>"
#endif
    "      <menuitem action='Close'/>"
    "    </menu>"
    "    <menu action='Save'/>"
    "  </menubar>"
    "</ui>";

static void model_copy_callback (GtkAction *action, gpointer p)
{
    copy_format_dialog((windata_t *) p, W_COPY);
}

int copyfile (const char *src, const char *dest) 
{
    FILE *srcfd, *destfd;
    char buf[GRETL_BUFSIZE];
    size_t n;

    if (!strcmp(src, dest)) return 1;
   
    if ((srcfd = gretl_fopen(src, "rb")) == NULL) {
	file_read_errbox(src);
	return 1; 
    }

    if ((destfd = gretl_fopen(dest, "wb")) == NULL) {
	file_write_errbox(dest);
	fclose(srcfd);
	return 1;
    }

    while ((n = fread(buf, 1, sizeof buf, srcfd)) > 0) {
	fwrite(buf, 1, n, destfd);
    }

    fclose(srcfd);
    fclose(destfd);

    return 0;
}

FILE *gretl_tempfile_open (char *fname)
{
    FILE *fp = NULL;
    int fd;

    strcat(fname, ".XXXXXX");
#ifdef G_OS_WIN32
    fd = gretl_mkstemp(fname);
#else
    fd = mkstemp(fname);
#endif
    if (fd != -1) {
	fp = fdopen(fd, "w+");
	if (fp == NULL) {
	    file_write_errbox(fname);
	    close(fd);
	    gretl_remove(fname);
	}
    }

    return fp;
}

int gretl_tempname (char *fname)
{
    int fd, err = 0;

    strcat(fname, ".XXXXXX");
#ifdef G_OS_WIN32
    fd = gretl_mkstemp(fname);
#else
    fd = mkstemp(fname);
#endif
    if (fd == -1) {
	file_write_errbox(fname);
	err = 1;
    } else {
	close(fd);
	gretl_remove(fname);
    }

    return err;
}

static void delete_file (GtkWidget *widget, char *fname) 
{
    gretl_remove(fname);
    g_free(fname);
}

void delete_widget (GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy(GTK_WIDGET(data));
}

#if defined(HAVE_FLITE) || defined(G_OS_WIN32)

static int set_or_get_audio_stop (int set, int val)
{
    static int audio_quit;

    if (set) audio_quit = val;

    return audio_quit;
}

static int should_stop_talking (void)
{
    while (gtk_events_pending()) {
	gtk_main_iteration();
    }

    return set_or_get_audio_stop(0, 0);
}

void stop_talking (void)
{
    set_or_get_audio_stop(1, 1);
}

void audio_render_window (windata_t *vwin, int key)
{
    int (*read_window_text) (windata_t *, const DATAINFO *, int (*)());
    void *handle;

    if (vwin == NULL) {
	stop_talking();
	return;
    }

    read_window_text = gui_get_plugin_function("read_window_text", 
					       &handle);
    if (read_window_text == NULL) {
        return;
    }

    set_or_get_audio_stop(1, 0);

    if (key == AUDIO_LISTBOX) {
	(*read_window_text) (vwin, NULL, &should_stop_talking);
    } else {
	(*read_window_text) (vwin, datainfo, &should_stop_talking);
    }

    close_plugin(handle);
}

#endif

static int vwin_selection_present (gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GtkTextBuffer *buf;
    int ret = 0;

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));

    if (gtk_text_buffer_get_selection_bounds(buf, NULL, NULL)) {
	ret = 1;
    }

    return ret;
}

int vwin_is_editing (windata_t *vwin)
{
    if (vwin != NULL && vwin->text != NULL) {
	return gtk_text_view_get_editable(GTK_TEXT_VIEW(vwin->text));
    } else {
	return 0;
    }
}

gboolean vwin_copy_callback (GtkWidget *w, windata_t *vwin)
{
    if (vwin_selection_present(vwin)) {
	window_copy(vwin, GRETL_FORMAT_SELECTION);
    } else if (vwin_is_editing(vwin)) {
	window_copy(vwin, GRETL_FORMAT_TXT);
    } else {
	copy_format_dialog(vwin, W_COPY);
    } 
    
    return TRUE;
}

/* Signal attached to editor/viewer windows: note that @w is always
   the GtkWidget vwin->main.
*/

static gint catch_viewer_key (GtkWidget *w, GdkEventKey *key, windata_t *vwin)
{
    GdkModifierType mods = widget_get_pointer_mask(w);
    guint upkey = gdk_keyval_to_upper(key->keyval);
    int editing = vwin_is_editing(vwin);
    int Ctrl = (mods & GDK_CONTROL_MASK);

    if (Ctrl) {
	if (upkey == GDK_F) {
	    text_find(NULL, vwin);
	    return TRUE;
	} else if (upkey == GDK_C) {
	    /* Ctrl-C: copy */
	    if (vwin_is_editing(vwin)) {
		/* let GTK handle this */
		return FALSE;
	    } else {
		return vwin_copy_callback(NULL, vwin);
	    }
	} else if (editing) {
	    /* note that the standard Ctrl-key sequences for editing
	       are handled by GTK, so we only need to put our own
	       "specials" here
	    */
	    if (upkey == GDK_S) { 
		/* Ctrl-S: save */
		vwin_save_callback(NULL, vwin);
		return TRUE;
	    } else if (upkey == GDK_Q || upkey == GDK_W) {
		/* Ctrl-Q or Ctrl-W, quit */
		if (vwin_content_changed(vwin)) {
		    /* conditional: we have unsaved changes */
		    if (query_save_text(NULL, NULL, vwin) == FALSE) {
			gtk_widget_destroy(w);
		    }
		} else { 
		    /* unconditional */
		    gtk_widget_destroy(w);
		}
		return TRUE;
	    } 
	} 
    } else if (mods & GDK_MOD1_MASK) {
	/* Alt */
	if (upkey == GDK_W) {
	    window_list_popup(vwin->main);
	    return TRUE;
	}
    }

    if (editing) {
	/* we respond to plain keystrokes below: this won't do if we're
	   editing text */
	return FALSE;
    }

    if (upkey == GDK_Q || (upkey == GDK_W && Ctrl)) { 
        gtk_widget_destroy(w);
    } else if (upkey == GDK_S && Z != NULL && vwin->role == VIEW_MODEL) {
	model_add_as_icon(NULL, vwin);
    } 

#if defined(HAVE_FLITE) || defined(G_OS_WIN32)
    /* respond to 'a' and 'x', but not if Alt-modified */
    else if (!(mods & GDK_MOD1_MASK)) {
	if (upkey == GDK_A) {
	    audio_render_window(vwin, AUDIO_TEXT);
	} else if (upkey == GDK_X) {
	    stop_talking();
	}
    }
#endif

    return FALSE;
}

/* ........................................................... */

void nomem (void)
{
    errbox(_("Out of memory!"));
}

void *mymalloc (size_t size) 
{
    void *mem;
   
    if ((mem = malloc(size)) == NULL) {
	nomem();
    }

    return mem;
}

void *myrealloc (void *ptr, size_t size) 
{
    void *mem;

    if ((mem = realloc(ptr, size)) == NULL) {
	nomem();
    }

    return mem;
}

void mark_dataset_as_modified (void)
{
    data_status |= MODIFIED_DATA;
    set_sample_label(datainfo);

    if (session_file_is_open()) {
	mark_session_changed();
    }
}

static void gui_record_data_opening (const char *fname, const int *list)
{
    const char *recname = (fname != NULL)? fname : datafile;

    if (strchr(recname, ' ') != NULL) {
	gretl_command_sprintf("open \"%s\"", recname);
    } else {
	gretl_command_sprintf("open %s", recname);
    }

    if (list != NULL && list[0] == 3) {
	/* record spreadsheet parameters */
	char parm[32];

	if (list[1] > 1) {
	    sprintf(parm, " --sheet=%d", list[1]);
	    gretl_command_strcat(parm);
	}
	if (list[2] > 0) {
	    sprintf(parm, " --coloffset=%d", list[2]);
	    gretl_command_strcat(parm);
	}
	if (list[3] > 0) {
	    sprintf(parm, " --rowoffset=%d", list[3]);
	    gretl_command_strcat(parm);
	}
    }

    check_and_record_command();

    if (*datafile != '\0') {
	char tmp[FILENAME_MAX];

	strcpy(tmp, datafile);
	mkfilelist(FILE_LIST_DATA, tmp);
    }
}

#define file_opened(f) (f == DATAFILE_OPENED || \
	                f == OPENED_VIA_CLI || \
                        f == OPENED_VIA_SESSION)

static void real_register_data (int flag, const char *user_fname, 
				const int *list)
{    
    /* basic accounting */
    data_status |= HAVE_DATA;
    orig_vars = datainfo->v;

    /* set appropriate data_status bits */
    if (file_opened(flag)) {
	if (!(data_status & IMPORT_DATA)) {
	    /* we opened a native data file */
	    if (has_system_prefix(datafile, DATA_SEARCH)) {
		data_status |= BOOK_DATA;
		data_status &= ~USER_DATA;
	    } else {
		data_status &= ~BOOK_DATA;
		data_status |= USER_DATA; 
	    }
	    if (is_gzipped(datafile)) {
		data_status |= GZIPPED_DATA;
	    } else {
		data_status &= ~GZIPPED_DATA;
	    }
	    if (flag == OPENED_VIA_SESSION) {
		data_status |= SESSION_DATA;
	    } else {
		data_status &= ~SESSION_DATA;
	    }
	}
    } else {
	/* we modified the current dataset somehow */
	data_status |= GUI_DATA;
	mark_dataset_as_modified();
    }
    
    /* sync main window with datafile */
    if (mdata != NULL) {
	populate_varlist();
	set_sample_label(datainfo);
	main_menubar_state(TRUE);
	session_menu_state(TRUE);
    }

    /* Record the opening of the data file in the GUI recent files
       list and command log; note that we don't do this if the file
       was opened via script or console, or if it was opened as a
       side effect of re-opening a saved session.
    */
    if (mdata != NULL && flag == DATAFILE_OPENED) {
	gui_record_data_opening(user_fname, list);
    } 

    if (mdata != NULL) {
	/* focus the data window */
	gtk_widget_grab_focus(mdata->listbox);
	/* invalidate "remove extra obs" menu item */
	drop_obs_state(FALSE);
    }
}

void register_data (int flag)
{
    real_register_data(flag, NULL, NULL);
}

void register_startup_data (const char *fname)
{
    real_register_data(DATAFILE_OPENED, fname, NULL);
}

static void finalize_data_open (const char *fname, int ftype,
				int import, int append, 
				const int *plist)
{
    if (import) {
	if (ftype == GRETL_CSV || ftype == GRETL_DTA || 
	    ftype == GRETL_SAV || ftype == GRETL_SAS) {
	    maybe_display_string_table();
	}
	data_status |= IMPORT_DATA;
    }

    if (append) {
	register_data(DATA_APPENDED);
	return;
    } 

    if (fname != datafile) {
	strcpy(datafile, fname);
    }

    real_register_data(DATAFILE_OPENED, NULL, plist);

    if (import && !dataset_is_time_series(datainfo) && 
	!dataset_is_panel(datainfo) && mdata != NULL) {
	int resp;

	resp = yes_no_dialog(_("gretl: open data"),
			     _("The imported data have been interpreted as undated\n"
			       "(cross-sectional).  Do you want to give the data a\n"
			       "time-series or panel interpretation?"),
			     0);
	if (resp == GRETL_YES) {
	    data_structure_dialog();
	}
    }    
}

static int datafile_missing (const char *fname)
{
    FILE *fp = gretl_fopen(fname, "r");
    int err = 0;

    if (fp == NULL) {
	delete_from_filelist(FILE_LIST_DATA, fname);
	file_read_errbox(fname);
	err = E_FOPEN;
    } else {
	fclose(fp);
    }

    return err;
}

/* below: get data of a sort that requires an import plugin */

int get_imported_data (char *fname, int ftype, int append)
{
    void *handle;
    PRN *errprn;
    const char *errbuf;
    int list[4] = {3, 1, 0, 0};
    int *plist = NULL;
    int (*ss_importer) (const char *, int *, char *, double ***, DATAINFO *, 
			gretlopt, PRN *);
    int (*misc_importer) (const char *, double ***, DATAINFO *, 
			  gretlopt, PRN *);
    int err = 0;

    if (datafile_missing(fname)) {
	return E_FOPEN;
    }

    ss_importer = NULL;
    misc_importer = NULL;

    if (ftype == GRETL_XLS) {
	ss_importer = gui_get_plugin_function("xls_get_data",
					      &handle);
	plist = list;
    } else if (ftype == GRETL_GNUMERIC) {
	ss_importer = gui_get_plugin_function("gnumeric_get_data",
					      &handle);
	plist = list;
    } else if (ftype == GRETL_ODS) {
	ss_importer = gui_get_plugin_function("ods_get_data",
					      &handle);
	plist = list;
    } else if (ftype == GRETL_DTA) {
	misc_importer = gui_get_plugin_function("dta_get_data",
						&handle);
    } else if (ftype == GRETL_SAV) {
	misc_importer = gui_get_plugin_function("sav_get_data",
						&handle);
    } else if (ftype == GRETL_SAS) {
	misc_importer = gui_get_plugin_function("xport_get_data",
						&handle);
     } else if (ftype == GRETL_JMULTI) {
	misc_importer = gui_get_plugin_function("jmulti_get_data",
						&handle);
    } else if (ftype == GRETL_WF1) {
	misc_importer = gui_get_plugin_function("wf1_get_data",
						&handle);
    } else {
	errbox(_("Unrecognized data type"));
	return 1;
    }

    if (ss_importer == NULL && misc_importer == NULL) {
        return 1;
    }

    if (bufopen(&errprn)) {
	close_plugin(handle);
	return 1;
    }

    if (SPREADSHEET_IMPORT(ftype)) {
	err = (*ss_importer)(fname, plist, NULL, &Z, datainfo, 
			     OPT_G, errprn);
    } else {
	err = (*misc_importer)(fname, &Z, datainfo, OPT_G, errprn);
    }
	
    close_plugin(handle);

    if (err == -1) {
	fprintf(stderr, "data import canceled\n");
	gretl_print_destroy(errprn);
	return E_CANCEL;
    }

    errbuf = gretl_print_get_buffer(errprn);

    if (err) {
	if (errbuf != NULL && *errbuf != '\0') {
	    errbox(errbuf);
	} else {
	    gui_errmsg(err);
	} 
	delete_from_filelist(FILE_LIST_DATA, fname);
    } else if (errbuf != NULL && *errbuf != '\0') {
	infobox(errbuf);
    }

    gretl_print_destroy(errprn);

    if (!err) {
	finalize_data_open(fname, ftype, 1, append, plist);
    }

    return err;
}

/* get "CSV" (or more generally, ASCII) data or GNU octave data:
   plugin is not required
*/

static int get_csv_data (char *fname, int ftype, int append)
{
    PRN *prn;
    gchar *title;
    int err = 0;

    if (datafile_missing(fname)) {
	return E_FOPEN;
    }

    if (bufopen(&prn)) {
	return 1;
    }

    if (ftype == GRETL_OCTAVE) {
	err = import_other(fname, ftype, &Z, datainfo, OPT_NONE, prn);
	title = g_strdup_printf(_("gretl: import %s data"), "Octave");
    } else {
	err = import_csv(fname, &Z, datainfo, OPT_NONE, prn);
	title = g_strdup_printf(_("gretl: import %s data"), "CSV");
    }

    /* show details regarding the import */
    view_buffer(prn, 78, 350, title, IMPORT, NULL); 
    g_free(title);

    if (err) {
	delete_from_filelist(FILE_LIST_DATA, fname);
    } else {
	finalize_data_open(fname, ftype, 1, append, NULL);
    }

    return err;
}

static int get_native_data (char *fname, int ftype, int append, 
			    windata_t *fwin)
{
    PRN *prn = NULL;
    char *buf = NULL;
    int err;

    if (bufopen(&prn)) {
	return 1;
    }

    if (ftype == GRETL_XML_DATA) {
	err = gretl_read_gdt(fname, &Z, datainfo, OPT_B, prn);
    } else {
	err = gretl_get_data(fname, &Z, datainfo, OPT_NONE, prn);
    }

    buf = gretl_print_steal_buffer(prn);
    gretl_print_destroy(prn);

    if (fwin != NULL) {
	/* close the files browser window that launched the query */
	gtk_widget_destroy(fwin->main);
    }    

    if (err) {
	if (err == E_FOPEN) {
	    file_read_errbox(tryfile);
	} else if (buf != NULL && *buf != '\0') {
	    errbox(buf);
	} else {
	    gui_errmsg(err);
	}
	delete_from_filelist(FILE_LIST_DATA, fname);
    } else {
	finalize_data_open(fname, ftype, 0, append, NULL);
	fputs(buf, stderr);
    } 

    free(buf);

    return err;
}

#define APPENDING(action) (action == APPEND_DATA || \
                           action == APPEND_CSV || \
                           action == APPEND_GNUMERIC || \
                           action == APPEND_XLS || \
                           action == APPEND_ODS || \
                           action == APPEND_WF1 || \
                           action == APPEND_DTA || \
	                   action == APPEND_SAV || \
			   action == APPEND_SAS || \
                           action == APPEND_JMULTI)

void do_open_data (windata_t *fwin, int code)
{
    int append = APPENDING(code);
    gint ftype;

    gretl_error_clear();

    if (code == OPEN_DATA || code == APPEND_DATA) {
	/* native .gdt files */
	ftype = GRETL_XML_DATA;
    } else if (code == OPEN_CSV || code == APPEND_CSV) {
	ftype = GRETL_CSV;
    } else if (code == OPEN_GNUMERIC || code == APPEND_GNUMERIC) {
	ftype = GRETL_GNUMERIC;
    } else if (code == OPEN_ODS || code == APPEND_ODS) {
	ftype = GRETL_ODS;
    } else if (code == OPEN_XLS || code == APPEND_XLS) {
	ftype = GRETL_XLS;
    } else if (code == OPEN_OCTAVE || code == APPEND_OCTAVE) {
	ftype = GRETL_OCTAVE;
    } else if (code == OPEN_WF1 || code == APPEND_WF1) {
	ftype = GRETL_WF1;
    } else if (code == OPEN_DTA || code == APPEND_DTA) {
	ftype = GRETL_DTA;
    } else if (code == OPEN_SAV || code == APPEND_SAV) {
	ftype = GRETL_SAV;
    } else if (code == OPEN_SAS || code == APPEND_SAS) {
	ftype = GRETL_SAS;
    } else if (code == OPEN_JMULTI || code == APPEND_JMULTI) {
	ftype = GRETL_JMULTI;
    } else {
	/* no filetype specified: have to guess */
	ftype = detect_filetype(tryfile, OPT_P);
    }

    /* destroy the current data set, etc., unless we're explicitly appending */
    if (!append) {
	close_session(NULL, &Z, datainfo, OPT_NONE); /* FIXME opt? */
    }

    if (ftype == GRETL_CSV || ftype == GRETL_OCTAVE) {
	get_csv_data(tryfile, ftype, append);
    } else if (SPREADSHEET_IMPORT(ftype) || OTHER_IMPORT(ftype)) {
	get_imported_data(tryfile, ftype, append);
    } else { 
	get_native_data(tryfile, ftype, append, fwin);
    }
}

/* give user choice of not opening selected datafile, if there's
   already a datafile open */

void verify_open_data (windata_t *vwin, int code)
{
    if (dataset_locked()) {
	return;
    }

    if (data_status) {
	int resp = 
	    yes_no_dialog (_("gretl: open data"), 
			   _("Opening a new data file will automatically\n"
			     "close the current one.  Any unsaved work\n"
			     "will be lost.  Proceed to open data file?"), 0);

	if (resp != GRETL_YES) return;
    } 

    do_open_data(vwin, code);
}

/* give user choice of not opening session file, if there's already a
   datafile open */

void verify_open_session (void)
{
    if (!gretl_is_pkzip_file(tryfile)) {
	/* not a new-style zipped session file */
	do_open_script(EDIT_SCRIPT);
	return;
    }

    if (data_status) {
	int resp = 
	    yes_no_dialog (_("gretl: open session"), 
			   _("Opening a new session file will automatically\n"
			     "close the current session.  Any unsaved work\n"
			     "will be lost.  Proceed to open session file?"), 0);

	if (resp != GRETL_YES) return;
    }

    do_open_session();
}

void mark_vwin_content_changed (windata_t *vwin) 
{
    if (vwin->active_var == 0) {
	GtkWidget *w = g_object_get_data(G_OBJECT(vwin->mbar), "save_button");

	if (w != NULL) {
	    gtk_widget_set_sensitive(w, TRUE);
	}
	vwin->flags |= VWIN_CONTENT_CHANGED;
    }
}

void mark_vwin_content_saved (windata_t *vwin) 
{
    GtkWidget *w = g_object_get_data(G_OBJECT(vwin->mbar), "save_button");

    if (w != NULL) {
	gtk_widget_set_sensitive(w, FALSE);
    }

    vwin->flags &= ~VWIN_CONTENT_CHANGED;

    w = g_object_get_data(G_OBJECT(vwin->mbar), "save_as_button");
    if (w != NULL) {
	gtk_widget_set_sensitive(w, TRUE);
    }
}

/* save content function for an editor window that is hooked
   up to a given text buffer rather than in the business of
   saving to file
*/

static void buf_edit_save (GtkWidget *w, windata_t *vwin)
{
    char **pbuf = (char **) vwin->data;
    gchar *text;

    text = textview_get_text(vwin->text);

    if (text == NULL || *text == '\0') {
	errbox(_("Buffer is empty"));
	g_free(text);
	return;
    }

    /* swap the edited text into the buffer */
    free(*pbuf); 
    *pbuf = text;

    if (vwin->role == EDIT_HEADER) {
	mark_vwin_content_saved(vwin);
	mark_dataset_as_modified();
    } else if (vwin->role == EDIT_NOTES) {
	mark_vwin_content_saved(vwin);
	mark_session_changed();
    }
}

static void file_edit_save (GtkWidget *w, windata_t *vwin)
{
    if (vwin->role == EDIT_PKG_SAMPLE) {
	/* function package editor, sample script window */
	update_sample_script(vwin);
    } else if (vwin->role == EDIT_PKG_CODE) {
	/* function package editor, function code window */
	update_func_code(vwin);
    } else if (*vwin->fname == '\0' || strstr(vwin->fname, "script_tmp")) {
	/* no real filename is available yet */
	if (vwin->role == EDIT_SCRIPT) {
	    file_selector(SAVE_SCRIPT, FSEL_DATA_VWIN, vwin);
	} else if (vwin->role == EDIT_GP) {
	    file_selector(SAVE_GP_CMDS, FSEL_DATA_VWIN, vwin);
	} else if (vwin->role == EDIT_R) {
	    file_selector(SAVE_R_CMDS, FSEL_DATA_VWIN, vwin);
	} else if (vwin->role == EDIT_OX) {
	    file_selector(SAVE_OX_CMDS, FSEL_DATA_VWIN, vwin);
	} else if (vwin->role == EDIT_OCTAVE) {
	    file_selector(SAVE_OCTAVE_CMDS, FSEL_DATA_VWIN, vwin);
	} else if (vwin->role == CONSOLE) {
	    file_selector(SAVE_CONSOLE, FSEL_DATA_VWIN, vwin);
	}	    
    } else if ((vwin->flags & VWIN_SESSION_GRAPH) &&
	       vwin->role == EDIT_GP) {
	/* "auto-save" of session graph file */
	gchar *text = textview_get_text(vwin->text);

	dump_plot_buffer(text, vwin->fname, 0);
	g_free(text);
	mark_vwin_content_saved(vwin);
	mark_session_changed();
    } else {
	FILE *fp;

	fp = gretl_fopen(vwin->fname, "w");

	if (fp == NULL) {
	    file_write_errbox(vwin->fname);
	} else {
	    gchar *text = textview_get_text(vwin->text);

	    system_print_buf(text, fp);
	    fclose(fp);
	    g_free(text);
	    mark_vwin_content_saved(vwin);
	}
    }
}

void vwin_save_callback (GtkWidget *w, windata_t *vwin)
{
    if (vwin_editing_buffer(vwin->role)) {
	buf_edit_save(w, vwin);
    } else {
	file_edit_save(w, vwin);
    }
}

/* Hook up child and parent viewers: this is used to help organize the
   window list menu (with, e.g., model-related output windows being
   marked as children of the model window itself).  It's also used for
   some more specialized cases, such as marking a script output window
   as child of the originating script window.
*/

void vwin_add_child (windata_t *parent, windata_t *child)
{
    int n = parent->n_gretl_children;
    int i, done = 0, err = 0;

    for (i=0; i<n; i++) {
	if (parent->gretl_children[i] == NULL) {
	    /* reuse a vacant slot */
	    parent->gretl_children[i] = child;
	    done = 1;
	    break;
	}
    }

    if (!done) {
	windata_t **children;

	children = myrealloc(parent->gretl_children, (n + 1) * sizeof *children);

	if (children != NULL) {
	    parent->gretl_children = children;
	    parent->gretl_children[n] = child;
	    parent->n_gretl_children += 1;
	} else {
	    err = 1;
	}
    }
    
    if (!err) {
	child->gretl_parent = parent;
    }
}

static void vwin_nullify_child (windata_t *parent, windata_t *child)
{
    int i;

    for (i=0; i<parent->n_gretl_children; i++) {
	if (child == parent->gretl_children[i]) {
	    parent->gretl_children[i] = NULL;
	}
    }
}

windata_t *vwin_first_child (windata_t *vwin)
{
    int i;

    for (i=0; i<vwin->n_gretl_children; i++) {
	if (vwin->gretl_children[i] != NULL) {
	    return vwin->gretl_children[i];
	}
    }

    return NULL;
}

void free_windata (GtkWidget *w, gpointer data)
{
    windata_t *vwin = (windata_t *) data;

    if (vwin != NULL) {
	if (vwin->text != NULL) { 
	    gchar *undo = g_object_steal_data(G_OBJECT(vwin->text), "undo");
	    
	    if (undo != NULL) {
		g_free(undo);
	    }
	}

	/* notify parent, if any, that child is gone */
	if (vwin->gretl_parent != NULL) {
	    vwin_nullify_child(vwin->gretl_parent, vwin);
	}

	/* notify children, if any, that parent is gone */
	if (vwin->n_gretl_children > 0) {
	    int i;

	    for (i=0; i<vwin->n_gretl_children; i++) {
		if (vwin->gretl_children[i] != NULL) {
		    vwin->gretl_children[i]->gretl_parent = NULL;
		}
	    }
	    free(vwin->gretl_children);
	}

	/* menu stuff */
	if (vwin->popup != NULL) {
	    gtk_widget_destroy(GTK_WIDGET(vwin->popup));
	}
	if (vwin->ui != NULL) {
	    g_object_unref(G_OBJECT(vwin->ui));
	}

	/* data specific to certain windows */
	if (vwin->role == SUMMARY) {
	    free_summary(vwin->data); 
	} else if (vwin->role == CORR || vwin->role == PCA || 
		   vwin->role == COVAR) {
	    free_vmatrix(vwin->data);
	} else if (vwin->role == FCAST || vwin->role == AFR) {
	    free_fit_resid(vwin->data);
	} else if (vwin->role == COEFFINT) {
	    free_coeff_intervals(vwin->data);
	} else if (vwin->role == VIEW_SERIES) {
	    free_series_view(vwin->data);
	} else if (vwin->role == VIEW_MODEL) {
	    gretl_object_unref(vwin->data, GRETL_OBJ_EQN);
	} else if (vwin->role == VAR || vwin->role == VECM) { 
	    gretl_object_unref(vwin->data, GRETL_OBJ_VAR);
	} else if (vwin->role == LEVERAGE) {
	    gretl_matrix_free(vwin->data);
	} else if (vwin->role == MAHAL) {
	    free_mahal_dist(vwin->data);
	} else if (vwin->role == XTAB) {
	    free_xtab(vwin->data);
	} else if (vwin->role == COINT2) {
	    gretl_VAR_free(vwin->data);
	} else if (vwin->role == SYSTEM) {
	    gretl_object_unref(vwin->data, GRETL_OBJ_SYS);
	} else if (vwin->role == PRINT && vwin->data != NULL) {
	    free_series_view(vwin->data);
	} else if (vwin->role == GUI_HELP || vwin->role == GUI_HELP_EN) {
	    free(vwin->data); /* help file text */
	} else if (vwin->role == VIEW_BUNDLE) {
	    if (!gretl_bundle_is_stacked(vwin->data)) {
		gretl_bundle_destroy(vwin->data);
	    }
	}

	if (window_delete_filename(vwin)) {
	    /* there's a temporary file associated */
	    gretl_remove(vwin->fname);
	}

	winstack_remove(vwin->main);
	free(vwin);
    }
}

gboolean text_popup_handler (GtkWidget *w, GdkEventButton *event, gpointer p)
{
    GdkModifierType mods = widget_get_pointer_mask(w);

    if (RIGHT_CLICK(mods)) {
	windata_t *vwin = (windata_t *) p;

	if (vwin->popup) {
	    gtk_widget_destroy(vwin->popup);
	    vwin->popup = NULL;
	}

	vwin->popup = build_text_popup(vwin);

	if (vwin->popup != NULL) {
	    gtk_menu_popup(GTK_MENU(vwin->popup), NULL, NULL, NULL, NULL,
			   event->button, event->time);
	    g_signal_connect(G_OBJECT(vwin->popup), "destroy",
			     G_CALLBACK(gtk_widget_destroyed), 
			     &vwin->popup);
	}

	return TRUE;
    }

    return FALSE;
}

gchar *title_from_filename (const char *fname)
{
    const char *p = strrchr(fname, SLASH);
    gchar *trfname, *title = NULL;

    if (p != NULL) {
	trfname = my_filename_to_utf8(p + 1);
    } else {
	trfname = my_filename_to_utf8(fname);
    }

    title = g_strdup_printf("gretl: %s", trfname);

    g_free(trfname);

    return title;
}

void vwin_set_filename (windata_t *vwin, const char *fname)
{
    gchar *title = title_from_filename(fname);

    gtk_window_set_title(GTK_WINDOW(vwin->main), title);
    g_free(title);
    strcpy(vwin->fname, fname);
}

static gchar *make_viewer_title (int role, const char *fname)
{
    gchar *title = NULL;

    switch (role) {
    case GUI_HELP: 
	title = g_strdup(_("gretl: help")); break;
    case FUNCS_HELP:
	title = g_strdup(_("gretl: function reference")); break;
    case CLI_HELP:
	title = g_strdup(_("gretl: command reference")); break;
    case GUI_HELP_EN: 
	title = g_strdup("gretl: help"); break;
    case CLI_HELP_EN:
	title = g_strdup("gretl: command reference"); break;
    case VIEW_LOG:
	title = g_strdup(_("gretl: command log")); break;
    case EDIT_SCRIPT:
    case VIEW_SCRIPT:	
    case VIEW_FILE:
    case VIEW_CODEBOOK:
	if (strstr(fname, "script_tmp") || strstr(fname, "session.inp")) {
	    title = g_strdup(_("gretl: command script"));
	} else {
	    title = title_from_filename(fname);
	} 
	break;
    case EDIT_NOTES:
	title = g_strdup(_("gretl: session notes")); break;
    case EDIT_GP:
	title = g_strdup(_("gretl: edit plot commands")); break;
    case EDIT_R:
	title = g_strdup(_("gretl: edit R commands")); break;
    case EDIT_OX:
	title = g_strdup(_("gretl: edit Ox program")); break;
    case EDIT_OCTAVE:
	title = g_strdup(_("gretl: edit Octave script")); break;
    case SCRIPT_OUT:
	title = g_strdup(_("gretl: script output")); break;
    case VIEW_DATA:
	title = g_strdup(_("gretl: display data")); break;
    default:
	break;
    }

    return title;
}

static void content_changed (GtkWidget *w, windata_t *vwin)
{
    mark_vwin_content_changed(vwin);
}

static void attach_content_changed_signal (windata_t *vwin)
{
    GtkTextBuffer *tbuf;

    tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));
    g_signal_connect(G_OBJECT(tbuf), "changed", 
		     G_CALLBACK(content_changed), vwin);
}

static void viewer_box_config (windata_t *vwin)
{
    vwin->vbox = gtk_vbox_new(FALSE, 1);
    gtk_box_set_spacing(GTK_BOX(vwin->vbox), 4);
    gtk_container_set_border_width(GTK_CONTAINER(vwin->vbox), 4);
    gtk_container_add(GTK_CONTAINER(vwin->main), vwin->vbox);
    
#ifndef G_OS_WIN32
    set_wm_icon(vwin->main);
#endif
}

#define viewing_source(r) (r == VIEW_PKG_CODE || \
			   r == EDIT_PKG_CODE || \
			   r == EDIT_PKG_SAMPLE)

static void view_buffer_insert_text (windata_t *vwin, PRN *prn)
{
    if (prn != NULL) {
	const char *buf = gretl_print_get_trimmed_buffer(prn);

	if (viewing_source(vwin->role)) {
	    sourceview_insert_buffer(vwin, buf);
	} else if (vwin->role == SCRIPT_OUT) {
	    textview_set_text_colorized(vwin->text, buf);
	} else {
	    textview_set_text(vwin->text, buf);
	}
    }
}

static windata_t *reuse_script_out (windata_t *vwin, PRN *prn)
{
    int sticky = (vwin->flags & VWIN_STICKY);
    GtkTextBuffer *buf;
    const char *newtext;

    newtext = gretl_print_get_buffer(prn);
    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));

    if (sticky) {
	/* append to previous content */
	GtkTextMark *mark;
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter(buf, &iter);
	mark = gtk_text_buffer_create_mark(buf, NULL, &iter, TRUE);
	textview_append_text_colorized(vwin->text, newtext, 1);
	gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(vwin->text), 
				     mark, 0.0, TRUE, 0, 0.05);
	gtk_text_buffer_delete_mark(buf, mark);
    } else {
	/* replace previous content */
	gtk_text_buffer_set_text(buf, "", -1);
	textview_set_text_colorized(vwin->text, newtext);
	cursor_to_top(vwin);
    }

    gretl_print_destroy(prn);

    gtk_window_present(GTK_WINDOW(vwin->main));

    return vwin;
}

static void model_save_state (GtkUIManager *ui, gboolean s)
{
    flip(ui, "/menubar/File/SaveAsIcon", s);
    flip(ui, "/menubar/File/SaveAndClose", s);
}

static gboolean nullify_script_out (GtkWidget *w, windata_t **pvwin)
{
    *pvwin = NULL;
    return FALSE;
}

#define view_buffer_record(r) (r != SCRIPT_OUT && \
	                       r != EDIT_PKG_CODE && \
	                       r != EDIT_PKG_SAMPLE)

windata_t *
view_buffer_with_parent (windata_t *parent, PRN *prn, 
			 int hsize, int vsize, 
			 const char *title, int role, 
			 gpointer data) 
{
    static windata_t *script_out;
    windata_t *vwin;
    int record, w, nlines;

    if (role == SCRIPT_OUT && script_out != NULL) {
	return reuse_script_out(script_out, prn);
    }

    record = view_buffer_record(role);

    if (title != NULL) {
	vwin = gretl_viewer_new_with_parent(parent, role, title, 
					    data, record);
    } else {
	gchar *tmp = make_viewer_title(role, NULL);

	vwin = gretl_viewer_new_with_parent(parent, role, tmp, 
					    data, record);
	g_free(tmp);
    }

    if (vwin == NULL) {
	return NULL;
    }

    viewer_box_config(vwin);

    if (role == VAR || role == VECM || role == SYSTEM) {
	/* special case: use a text-based menu bar */
	vwin_add_ui(vwin, system_items, n_system_items, sys_ui);
	model_save_state(vwin->ui, !is_session_model(vwin->data));
	add_system_menu_items(vwin, role);
	gtk_box_pack_start(GTK_BOX(vwin->vbox), vwin->mbar, FALSE, TRUE, 0);
	if (role == VAR || role == VECM) {
	    g_signal_connect(G_OBJECT(vwin->mbar), "button-press-event", 
			     G_CALLBACK(check_VAR_menu), vwin);
	}
	gtk_widget_show(vwin->mbar);
	gretl_object_ref(data, (role == SYSTEM)? GRETL_OBJ_SYS : GRETL_OBJ_VAR);
    } else if (role == VIEW_BUNDLE) {
	vwin_add_ui(vwin, bundle_items, n_bundle_items, bundle_ui);
	add_bundle_menu_items(vwin);
	gtk_box_pack_start(GTK_BOX(vwin->vbox), vwin->mbar, FALSE, TRUE, 0);
    } else if (role == VIEW_PKG_CODE || role == VIEW_MODELTABLE) {
	vwin_add_viewbar(vwin, 0);
    } else if (role == EDIT_PKG_CODE ||
	       role == EDIT_PKG_SAMPLE) {
	vwin_add_viewbar(vwin, VIEWBAR_EDITABLE);
    } else if (role != IMPORT) {
	vwin_add_viewbar(vwin, VIEWBAR_HAS_TEXT);
    }

    gretl_print_get_size(prn, &w, &nlines);
#if 1
    if (role != SCRIPT_OUT && w > 0 && w + 4 < hsize) {
	hsize = w + 4;
    }
#endif

    if (role == VIEW_PKG_CODE) {
	create_source(vwin, hsize, vsize, FALSE);
    } else if (role == EDIT_PKG_CODE || role == EDIT_PKG_SAMPLE) {
	create_source(vwin, hsize, vsize, TRUE);
    } else {
	create_text(vwin, hsize, vsize, nlines, FALSE);
	if (role == PRINT || role == SCRIPT_OUT ||
	    role == VIEW_MODELTABLE) {
	    text_set_word_wrap(vwin->text, 0);
	}
    }

    text_table_setup(vwin->vbox, vwin->text);

    if (role == SCRIPT_OUT) {
	if (data != NULL) {
	    /* partial output window for script */
	    vwin_add_child((windata_t *) data, vwin);
	}
	/* register destruction of script output viewer */
	g_signal_connect(G_OBJECT(vwin->main), "destroy", 
			 G_CALLBACK(nullify_script_out), &script_out);
	script_out = vwin;
    }

    /* insert and then free the text buffer */
    view_buffer_insert_text(vwin, prn);
    gretl_print_destroy(prn);

    g_signal_connect(G_OBJECT(vwin->main), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);

    gtk_widget_show(vwin->vbox);
    gtk_widget_show(vwin->main);

    if (role == EDIT_PKG_CODE || role == EDIT_PKG_SAMPLE) {
	attach_content_changed_signal(vwin);
	g_signal_connect(G_OBJECT(vwin->main), "delete-event", 
			 G_CALLBACK(query_save_text), vwin);
    } 

    g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
		     G_CALLBACK(text_popup_handler), vwin);
    cursor_to_top(vwin);

    return vwin;
}

windata_t *view_buffer (PRN *prn, int hsize, int vsize, 
			const char *title, int role, 
			gpointer data)
{
    return view_buffer_with_parent(NULL, prn, hsize, 
				   vsize, title,
				   role, data);
} 

#define view_file_use_sourceview(r) (vwin_editing_script(r) || \
                                     r == VIEW_SCRIPT || \
                                     r == VIEW_LOG)

#define record_on_winstack(r) (!vwin_editing_script(r) && \
                               r != VIEW_LOG && \
                               r != VIEW_SCRIPT)

#define text_out_ok(r) (r == VIEW_DATA || r == VIEW_FILE)

windata_t *view_file (const char *filename, int editable, int del_file, 
		      int hsize, int vsize, int role)
{
    windata_t *vwin;
    ViewbarFlags vflags = 0;
    FILE *fp;
    gchar *title = NULL;

    /* first check that we can open the specified file */
    fp = gretl_fopen(filename, "r");
    if (fp == NULL) {
	errbox(_("Can't open %s for reading"), filename);
	return NULL;
    } else {
	fclose(fp);
    }

    /* then start building the file viewer */
    title = make_viewer_title(role, filename);
    vwin = gretl_viewer_new(role, (title != NULL)? title : filename, 
			    NULL, record_on_winstack(role));
    g_free(title);

    if (vwin == NULL) {
	return NULL;
    }

    strcpy(vwin->fname, filename);

    viewer_box_config(vwin);

    if (editable) {
	vflags = VIEWBAR_EDITABLE;
    }

    if (text_out_ok(role)) {
	vflags |= VIEWBAR_HAS_TEXT;
    }

    vwin_add_viewbar(vwin, vflags);

    if (view_file_use_sourceview(role)) {
	create_source(vwin, hsize, vsize, editable);
    } else {
	create_text(vwin, hsize, vsize, 0, editable);
    }

    text_table_setup(vwin->vbox, vwin->text);

    if (view_file_use_sourceview(role)) {
	sourceview_insert_file(vwin, filename);
    } else {
	textview_insert_file(vwin, filename);
    }

    /* catch some special keystrokes */
    g_signal_connect(G_OBJECT(vwin->main), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);

    /* editing script or graph commands: grab the "changed" signal and
       set up alert for unsaved changes on exit */
    if (vwin_editing_script(role)) {
	attach_content_changed_signal(vwin);
	g_signal_connect(G_OBJECT(vwin->main), "delete-event", 
			 G_CALLBACK(query_save_text), vwin);
    }

    /* clean up when dialog is destroyed */
    if (del_file) {
	gchar *fname = g_strdup(filename);

	g_signal_connect(G_OBJECT(vwin->main), "destroy", 
			 G_CALLBACK(delete_file), (gpointer) fname);
    }

    gtk_widget_show(vwin->vbox);
    gtk_widget_show(vwin->main);

    g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
		     G_CALLBACK(text_popup_handler), vwin);

    cursor_to_top(vwin);
    gtk_widget_grab_focus(vwin->text);

    return vwin;
}

windata_t *console_window (int hsize, int vsize)
{
    windata_t *vwin;

    vwin = gretl_viewer_new(CONSOLE, _("gretl console"), NULL, 0);
    if (vwin == NULL) {
	return NULL;
    }

    viewer_box_config(vwin);
    vwin_add_viewbar(vwin, VIEWBAR_EDITABLE);
    create_text(vwin, hsize, vsize, 0, 1);
    text_table_setup(vwin->vbox, vwin->text);

    /* catch some special keystrokes */
    g_signal_connect(G_OBJECT(vwin->main), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);

    gtk_widget_show(vwin->vbox);
    gtk_widget_show(vwin->main);

    g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
		     G_CALLBACK(text_popup_handler), vwin);

    return vwin;
}

void help_panes_setup (windata_t *vwin, GtkWidget *text)
{
    GtkWidget *hp = gtk_hpaned_new();
    GtkWidget *sw;

    gtk_container_add(GTK_CONTAINER(vwin->vbox), hp);

    add_help_navigator(vwin, hp);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_paned_pack2(GTK_PANED(hp), sw, TRUE, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
					GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), text); 

    gtk_widget_show_all(hp);
}

windata_t *view_help_file (const char *filename, int role)
{
    windata_t *vwin;
    gchar *fbuf = NULL;
    gchar *title = NULL;
    int hsize = 80, vsize = 400;

    /* grab content of the appropriate help file into a buffer */
    gretl_file_get_contents(filename, &fbuf, NULL);
    if (fbuf == NULL) {
	return NULL;
    }

    title = make_viewer_title(role, NULL);
    vwin = gretl_viewer_new(role, title, NULL, 0);
    g_free(title);

    if (vwin == NULL) return NULL;

    strcpy(vwin->fname, filename);
    vwin->data = fbuf;

    viewer_box_config(vwin);
    set_up_helpview_menu(vwin);

    if (role == FUNCS_HELP) {
	hsize = 82;
	vsize = 500;
    }

    create_text(vwin, hsize, vsize, 0, FALSE);

    if (role != GUI_HELP && role != GUI_HELP_EN) {
	help_panes_setup(vwin, vwin->text);
    } else {
	text_table_setup(vwin->vbox, vwin->text);
    }

    g_signal_connect(G_OBJECT(vwin->text), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);

    if (vwin->role == CLI_HELP || vwin->role == CLI_HELP_EN ||
	vwin->role == FUNCS_HELP) {
	g_signal_connect(G_OBJECT(vwin->text), "button-press-event",
			 G_CALLBACK(help_popup_handler), 
			 vwin);
    } else {
	g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
			 G_CALLBACK(text_popup_handler), vwin);
    }	

    gtk_widget_show(vwin->vbox);
    gtk_widget_show(vwin->main);

    /* make the helpfile variant discernible via vwin->text */
    g_object_set_data(G_OBJECT(vwin->text), "role", GINT_TO_POINTER(vwin->role));

    gtk_widget_grab_focus(vwin->text);

    return vwin;
}

static gboolean enter_close_button (GtkWidget *button,
				    GdkEventCrossing *event,
				    gpointer p)
{
    /* remove text cursor: looks broken over a button */
    gdk_window_set_cursor(gtk_widget_get_window(button), NULL);
    return FALSE;
}

static gboolean leave_close_button (GtkWidget *button,
				    GdkEventCrossing *event,
				    gpointer p)
{
    GdkCursor *cursor = gdk_cursor_new(GDK_XTERM);

    /* replace text cursor */
    gdk_window_set_cursor(gtk_widget_get_window(button), cursor);
    gdk_cursor_unref(cursor);
    return FALSE;
}

static GtkWidget *small_close_button (GtkWidget *targ)
{
    GtkWidget *img = gtk_image_new_from_stock(GTK_STOCK_CLOSE, 
					      GTK_ICON_SIZE_MENU);
    GtkWidget *button = gtk_button_new();

    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(button), img);

    gtk_widget_add_events(button, GDK_ENTER_NOTIFY_MASK |
			  GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(button, "enter-notify-event",
		     G_CALLBACK(enter_close_button), NULL);
    g_signal_connect(button, "leave-notify-event",
		     G_CALLBACK(leave_close_button), NULL);
    g_signal_connect_swapped(button, "clicked",
			     G_CALLBACK(gtk_widget_destroy),
			     targ);
    gtk_widget_show_all(button);

    return button;
}

/* Stick a little "close" button into a GtkTextBuffer */

static void add_text_closer (windata_t *vwin)
{
    GtkTextBuffer *tbuf;
    GtkTextIter iter, iend;
    GtkTextTag *tag;
    GtkTextChildAnchor *anchor;
    GtkWidget *button;

    tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));
    gtk_text_buffer_get_start_iter(tbuf, &iter);
    tag = gtk_text_buffer_create_tag(tbuf, NULL, "justification",
				     GTK_JUSTIFY_RIGHT, NULL);
    anchor = gtk_text_buffer_create_child_anchor(tbuf, &iter);
    button = small_close_button(vwin->main);
    gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(vwin->text),
				      button, anchor);
    gtk_text_buffer_get_iter_at_child_anchor(tbuf, &iend, anchor);
    gtk_text_iter_forward_char(&iend);
    gtk_text_buffer_insert(tbuf, &iend, "\n", -1);
    gtk_text_buffer_get_start_iter(tbuf, &iter);
    gtk_text_buffer_apply_tag(tbuf, tag, &iter, &iend);
}

/* For use when we want to display a piece of formatted text -- such
   as help for a gretl function package or a help bibliography entry
   -- in a window of its own, without any menu apparatus on the
   window. Note that if the @title is NULL we assume that this should
   be a minimal window with no decorations and a simple "closer"
   button embedded in the GtkTextView; we use this for bibliographical
   popups.
*/

windata_t *view_formatted_text_buffer (const gchar *title, const char *buf, 
				       int width, int height)
{
    int minimal = (title == NULL);
    windata_t *vwin;

    vwin = gretl_viewer_new_with_parent(NULL, PRINT, title,
					NULL, 0);
    if (vwin == NULL) return NULL;

    vwin->vbox = gtk_vbox_new(FALSE, 1);
    gtk_box_set_spacing(GTK_BOX(vwin->vbox), 4);
    gtk_container_set_border_width(GTK_CONTAINER(vwin->vbox), 4);
    gtk_container_add(GTK_CONTAINER(vwin->main), vwin->vbox);

    create_text(vwin, width, height, 0, FALSE);

    if (minimal) {
	/* no scrolling apparatus */
	gtk_container_add(GTK_CONTAINER(vwin->vbox), vwin->text);
	gtk_widget_show(vwin->text);
	gtk_window_set_decorated(GTK_WINDOW(vwin->main), FALSE);
    } else {
	text_table_setup(vwin->vbox, vwin->text);
    }

    gretl_viewer_set_formatted_buffer(vwin, buf);

    if (minimal) {
	add_text_closer(vwin);
    }

    gtk_widget_show(vwin->vbox);

    if (!minimal) {
	gtk_widget_show(vwin->main);
	gtk_widget_grab_focus(vwin->text);
    }

    return vwin;
}

void view_window_set_editable (windata_t *vwin)
{
    gtk_text_view_set_editable(GTK_TEXT_VIEW(vwin->text), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(vwin->text), TRUE);
    g_signal_connect(G_OBJECT(vwin->main), "delete-event", 
		     G_CALLBACK(query_save_text), vwin);
    vwin->role = EDIT_SCRIPT;
    viewbar_add_edit_items(vwin);
    attach_content_changed_signal(vwin);
}

/* Called on destroying an editing window: give the user a chance
   to save if the content is changed, or to cancel the close.
*/

gint query_save_text (GtkWidget *w, GdkEvent *event, windata_t *vwin)
{
    if (vwin_content_changed(vwin)) {
	int resp = yes_no_dialog("gretl", _("Save changes?"), 1);

	if (resp == GRETL_CANCEL) {
	    /* cancel -> don't save, but also don't close */
	    return TRUE;
	} else if (resp == GRETL_YES) {
	    vwin_save_callback(NULL, vwin);
	}
    }

    return FALSE;
}

windata_t *edit_buffer (char **pbuf, int hsize, int vsize, 
			char *title, int role) 
{
    windata_t *vwin;

    vwin = gretl_viewer_new(role, title, pbuf, 1);
    if (vwin == NULL) {
	return NULL;
    }

    viewer_box_config(vwin); 

    /* add a tool bar */
    vwin_add_viewbar(vwin, VIEWBAR_EDITABLE);

    create_text(vwin, hsize, vsize, 0, TRUE);
    text_table_setup(vwin->vbox, vwin->text);
    
    /* insert the buffer text */
    if (*pbuf) {
	GtkTextBuffer *tbuf = 
	    gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));

	gtk_text_buffer_set_text(tbuf, *pbuf, -1);
    }
    g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
		     G_CALLBACK(text_popup_handler), vwin);
    g_signal_connect(G_OBJECT(vwin->main), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);

    attach_content_changed_signal(vwin);

    /* alert for unsaved changes on exit */
    g_signal_connect(G_OBJECT(vwin->main), "delete-event",
		     G_CALLBACK(query_save_text), vwin);

    gtk_widget_show(vwin->vbox);
    gtk_widget_show(vwin->main);

    cursor_to_top(vwin);

    return vwin;
}

static gint 
check_delete_model_window (GtkWidget *w, GdkEvent *e, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    gint ret = FALSE;

    if (window_is_busy(vwin)) {
	maybe_raise_dialog();
	ret = TRUE;
    }

    return ret;
}

int view_model (PRN *prn, MODEL *pmod, int hsize, int vsize, 
		char *title) 
{
    windata_t *vwin;
    const char *buf;
    int w, nlines;

    vwin = gretl_viewer_new(VIEW_MODEL, title, pmod, 1);
    if (vwin == NULL) {
	return 1;
    }

    /* Take responsibility for one reference to this model */
    gretl_object_ref(pmod, GRETL_OBJ_EQN);

    viewer_box_config(vwin);

    set_up_model_view_menu(vwin->main, vwin);

    g_signal_connect(G_OBJECT(vwin->mbar), "button-press-event", 
		     G_CALLBACK(check_model_menu), vwin);

    gtk_box_pack_start(GTK_BOX(vwin->vbox), vwin->mbar, FALSE, TRUE, 0);
    gtk_widget_show(vwin->mbar);

    gretl_print_get_size(prn, &w, &nlines);
    if (w + 2 < hsize) {
	hsize = w + 2;
    }

    create_text(vwin, hsize, vsize, nlines, FALSE);
    text_table_setup(vwin->vbox, vwin->text);

    /* insert and then free the model results buffer */
    buf = gretl_print_get_trimmed_buffer(prn);
    textview_set_text(vwin->text, buf);
    gretl_print_destroy(prn);

    /* sync number of model tests */
    vwin->n_model_tests = pmod->ntests;

    /* attach shortcuts */
    g_signal_connect(G_OBJECT(vwin->main), "key-press-event", 
		     G_CALLBACK(catch_viewer_key), vwin);
    g_signal_connect(G_OBJECT(vwin->text), "button-press-event", 
		     G_CALLBACK(text_popup_handler), vwin);

    /* don't allow deletion of model window when a model
       test dialog is active */
    g_signal_connect(G_OBJECT(vwin->main), "delete-event", 
		     G_CALLBACK(check_delete_model_window), 
		     vwin);

    gtk_widget_show(vwin->vbox);
    gtk_widget_show_all(vwin->main);

    cursor_to_top(vwin);

    gtk_window_present(GTK_WINDOW(vwin->main));

    return 0;
}

#define dw_pval_ok(m) ((m->ci == OLS || m->ci == PANEL) && !na(pmod->dw))

static void get_ci_and_opt (const gchar *s, int *ci, gretlopt *opt)
{
    char c, word[9];

    sscanf(s, "%8[^:]:%c", word, &c);
    *ci = gretl_command_number(word);
    *opt = opt_from_flag((unsigned char) c);
}

static void set_tests_menu_state (GtkUIManager *ui, const MODEL *pmod)
{
    gretlopt opt;
    char path[128];
    const gchar *s;
    int i, n, ci;

    if (pmod->ci == MPOLS) {
	/* can we relax this? */
	flip(ui, "/menubar/Tests", FALSE);
	return;
    }

    n = G_N_ELEMENTS(model_test_items);

    for (i=0; i<n; i++) {
	opt = OPT_NONE;
	s = model_test_items[i].name;
	if (strchr(s, ':')) {
	    get_ci_and_opt(s, &ci, &opt);
	} else if (!strcmp(s, "dwpval")) {
	    sprintf(path, "/menubar/Tests/%s", s);
	    flip(ui, path, dw_pval_ok(pmod));
	    continue;
	} else if (!strcmp(s, "Hsk")) {
	    ci = MODTEST;
	    opt = (dataset_is_panel(datainfo))? OPT_P : OPT_W;
	} else {
	    ci = gretl_command_number(s);
	}
	sprintf(path, "/menubar/Tests/%s", s);
	flip(ui, path, model_test_ok(ci, opt, pmod, datainfo));
    }
}

static void arma_x12_menu_mod (windata_t *vwin)
{
    flip(vwin->ui, "/menubar/Analysis/Covariance", FALSE);
    add_x12_output_menu_item(vwin);
}

static void rq_coeff_intervals_mod (windata_t *vwin)
{
    flip(vwin->ui, "/menubar/Analysis/ConfIntervals", FALSE);
}

static void mnl_probs_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = vwin->data;
    gretl_matrix *P = NULL;
    int err = 0;

    if (pmod == NULL) return;

    P = mn_logit_probabilities(pmod, (const double **) Z, datainfo, &err);
 
    if (err) {
	gui_errmsg(err);
    } else {
	PRN *prn = NULL;

	if (bufopen(&prn)) {
	    gretl_matrix_free(P);
	} else {
	    const int *yvals = gretl_model_get_data(pmod, "yvals");
	    int obslen = max_obs_label_length(datainfo);
	    int i, j, t = P->t1;
	    char obslabel[OBSLEN];
	    double x;

	    pprintf(prn, "\nEstimated outcome probabilities for %s\n\n",
		    gretl_model_get_depvar_name(pmod, datainfo));

	    /* case values */
	    bufspace(obslen, prn);
	    for (j=1; j<=yvals[0]; j++) {
		pprintf(prn, "%9d", yvals[j]);
	    }
	    pputc(prn, '\n');

	    /* format the matrix content nicely, prepending
	       observation strings */

	    for (i=0; i<P->rows; i++) {
		int thislen = obslen;

		if (dataset_has_markers(datainfo)) {
		    strcpy(obslabel, datainfo->S[t]);
		    thislen = get_utf_width(obslabel, obslen);
		} else {
		    ntodate(obslabel, t, datainfo);
		}
		pprintf(prn, "%*s", thislen, obslabel);
		for (j=0; j<P->cols; j++) {
		    x = gretl_matrix_get(P, i, j);
		    if (xna(x)) {
			pprintf(prn, "%9s", " ");
		    } else {
			pprintf(prn, "%9.4f", x);
		    }
		}
		pputc(prn, '\n');
		t++;
	    }

	    view_buffer(prn, 78, 400, _("Outcome probabilities"), PRINT, NULL);
	    gretl_matrix_free(P);
	}
    }
}

static void add_multinomial_probs_item (windata_t *vwin)
{
    const gchar *mpath = "/menubar/Analysis";
    GtkActionEntry entry;

    action_entry_init(&entry);
    entry.name = "mnlprobs";
    entry.label = _("Outcome probabilities");
    entry.callback = G_CALLBACK(mnl_probs_callback);
    vwin_menu_add_item(vwin, mpath, &entry);
}

#define intervals_model(m) (m->ci == LAD && \
			    gretl_model_get_data(m, "coeff_intervals"))

static void adjust_model_menu_state (windata_t *vwin, const MODEL *pmod)
{
    set_tests_menu_state(vwin->ui, pmod);

    /* disallow saving an already-saved model */
    if (pmod->name != NULL) {
	model_save_state(vwin->ui, FALSE);
    }

    if (RQ_SPECIAL_MODEL(pmod)) {
	/* can we relax this later? */
	flip(vwin->ui, "/menubar/Tests", FALSE);
	flip(vwin->ui, "/menubar/Save", FALSE);
	flip(vwin->ui, "/menubar/Analysis", FALSE);
	return;
    } 

    if (intervals_model(pmod)) {
	rq_coeff_intervals_mod(vwin);
    }

    if (pmod->ci == MLE || pmod->ci == GMM || pmod->ci == BIPROBIT) {
	/* can we relax some of this later? */
	flip(vwin->ui, "/menubar/Analysis/DisplayAFR", FALSE);
	flip(vwin->ui, "/menubar/Analysis/Forecasts", FALSE);
	flip(vwin->ui, "/menubar/Graphs", FALSE);
    } else if (pmod->ci == LOGIT && gretl_model_get_int(pmod, "multinom")) {
	/* relax this? */
	flip(vwin->ui, "/menubar/Analysis/Forecasts", FALSE);
	add_multinomial_probs_item(vwin);
    } else if (pmod->ci == ARMA && arma_by_x12a(pmod)) {
	arma_x12_menu_mod(vwin);
    } 

    if (pmod->ci == GMM || pmod->ci == BIPROBIT) {
	/* FIXME? */
	flip(vwin->ui, "/menubar/Save", FALSE);
    }

    if (pmod->ncoeff == 1) {
	flip(vwin->ui, "/menubar/Analysis/ConfEllipse", FALSE);
    }

    if (pmod->ci == ARBOND || pmod->ci == DPANEL ||
	(pmod->ci == PANEL && !(pmod->opt & OPT_P))) {
	flip(vwin->ui, "/menubar/Analysis/Forecasts", FALSE);
    } else if (pmod->ci == GARCH) {
	flip(vwin->ui, "/menubar/Tests/Hsk", FALSE);
    }

    if (pmod->ci != OLS || !pmod->ifc || na(pmod->ess) || na(pmod->tss)) {
	flip(vwin->ui, "/menubar/Analysis/ANOVA", FALSE);
    }

    if (!bootstrap_ok(pmod->ci)) {
	flip(vwin->ui, "/menubar/Analysis/Bootstrap", FALSE);
    }
}

static GtkActionEntry model_data_base_items[] = {
    { "yhat", NULL, N_("_Fitted values"), NULL, NULL, 
      G_CALLBACK(fit_resid_callback) },
    { "uhat", NULL, N_("_Residuals"), NULL, NULL, 
      G_CALLBACK(fit_resid_callback) },
    { "uhat2", NULL, N_("_Squared residuals"), NULL, NULL, 
      G_CALLBACK(fit_resid_callback) }
};

static GtkActionEntry ess_items[] = {
    { "ess", NULL, N_("_Error sum of squares"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) },
    { "se", NULL, N_("_Standard error of the regression"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) }
}; 

static GtkActionEntry r_squared_items[] = {
    { "rsq", NULL, N_("_R-squared"), NULL, NULL, G_CALLBACK(model_stat_callback) },
    { "trsq", NULL, N_("_T*R-squared"), NULL, NULL, G_CALLBACK(model_stat_callback) }
}; 

static GtkActionEntry lnl_data_items[] = {
    { "lnL", NULL, N_("_Log likelihood"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) }
};

static GtkActionEntry criteria_items[] = {
    { "AIC", NULL, N_("_Akaike Information Criterion"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) },
    { "BIC", NULL, N_("_Bayesian Information Criterion"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) },
    { "HQC", NULL, N_("_Hannan-Quinn Information Criterion"), NULL, NULL, 
      G_CALLBACK(model_stat_callback) }
};

static GtkActionEntry garch_data_items[] = {
    { "h", NULL, N_("_Predicted error variance"), NULL, NULL, 
      G_CALLBACK(fit_resid_callback)
    }
};

static GtkActionEntry fixed_effects_data_items[] = {
    { "ahat", NULL, N_("Per-unit _constants"), NULL, NULL, 
      G_CALLBACK(fit_resid_callback)
    }
};

static GtkActionEntry define_var_items[] = {
    /* Under Save; Sep wanted */
    { "NewVar", NULL, N_("Define _new variable..."), NULL, NULL,
      G_CALLBACK(model_genr_callback) }
};

static int criteria_available (const MODEL *pmod)
{
    int i;

    for (i=0; i<C_MAX; i++) {
	if (na(pmod->criterion[i])) {
	    return 0;
	}
    }

    return 1;
}

static void add_model_dataset_items (windata_t *vwin)
{
    const gchar *path = "/menubar/Save";
    MODEL *pmod = vwin->data;

    vwin_menu_add_items(vwin, path, model_data_base_items,
			G_N_ELEMENTS(model_data_base_items));
			
    if (gretl_model_get_data(pmod, "ahat") != NULL) {
	vwin_menu_add_items(vwin, path, fixed_effects_data_items,
			    G_N_ELEMENTS(fixed_effects_data_items));
    }

    if (pmod->ci != GARCH && !(pmod->ci == LOGIT && (pmod->opt & OPT_M))) {
	vwin_menu_add_items(vwin, path, ess_items,
			    G_N_ELEMENTS(ess_items));
    }

    if (!ML_ESTIMATOR(pmod->ci) && pmod->ci != LAD && !na(pmod->rsq)) {
	vwin_menu_add_items(vwin, path, r_squared_items,
			    G_N_ELEMENTS(r_squared_items));
    }

    if (!na(pmod->lnL)) {
	vwin_menu_add_items(vwin, path, lnl_data_items,
			    G_N_ELEMENTS(lnl_data_items));
    }

    if (criteria_available(pmod)) {
	vwin_menu_add_items(vwin, path, criteria_items,
			    G_N_ELEMENTS(criteria_items));
    }

    if (pmod->ci == GARCH) {
	vwin_menu_add_items(vwin, path, garch_data_items,
			    G_N_ELEMENTS(garch_data_items));
    }

    vwin_menu_add_separator(vwin, path);

    vwin_menu_add_items(vwin, path, define_var_items,
			G_N_ELEMENTS(define_var_items));
}

static void add_model_tex_items (windata_t *vwin)
{
    MODEL *pmod = (MODEL *) vwin->data;
    int eqn_ok = command_ok_for_model(EQNPRINT, 0, pmod->ci);
    GtkActionGroup *actions;
    GError *err = NULL;
    int imod = 0;

    gtk_ui_manager_add_ui_from_string(vwin->ui, model_tex_ui, -1, &err);

    if (err != NULL) {
	g_message("building LaTeX menu failed: %s", err->message);
	g_error_free(err);
	return;
    }	

    actions = gtk_action_group_new("ModelTeX");
    gtk_action_group_set_translation_domain(actions, "gretl");
    gtk_action_group_add_actions(actions, model_tex_items, 
				 G_N_ELEMENTS(model_tex_items),
				 vwin);
    gtk_action_group_add_radio_actions(actions, tex_eqn_items, 
				       G_N_ELEMENTS(tex_eqn_items),
				       (get_tex_eqn_opt() == OPT_T),
				       G_CALLBACK(set_tex_eqn_opt),
				       vwin);
    gtk_ui_manager_insert_action_group(vwin->ui, actions, 0);
    g_object_unref(actions);

    if (intervals_model(pmod)) {
	eqn_ok = 0;
	imod = 1;
    }

    if (!eqn_ok || pmod->errcode) {
	flip(vwin->ui, "/menubar/LaTeX/TeXView/EqnView", FALSE);
	flip(vwin->ui, "/menubar/LaTeX/TeXCopy/EqnCopy", FALSE);
	flip(vwin->ui, "/menubar/LaTeX/TeXSave/EqnSave", FALSE);
	flip(vwin->ui, "/menubar/LaTeX/EqnOpts", FALSE);
    }

    if (imod) {
	flip(vwin->ui, "/menubar/LaTeX/TabOpts", FALSE);
    }
}

/* dummy placeholder, for when TeX is not supported */

static void add_missing_tex_items (windata_t *vwin)
{
    GtkActionGroup *actions;
    GError *err = NULL;

    gtk_ui_manager_add_ui_from_string(vwin->ui, missing_tex_ui, -1, &err);
    if (err != NULL) {
	g_message("building menus failed: %s", err->message);
	g_error_free(err);
	return;
    }	

    actions = gtk_action_group_new("MissingTeX");
    gtk_action_group_add_actions(actions, missing_tex_items, 
				 G_N_ELEMENTS(missing_tex_items),
				 vwin);
    gtk_ui_manager_insert_action_group(vwin->ui, actions, 0);
    g_object_unref(actions);

    flip(vwin->ui, "/menubar/LaTeX", FALSE);
}

#define VNAMELEN2 32

static void add_vars_to_plot_menu (windata_t *vwin)
{
    GtkActionEntry entry;
    const gchar *mpath[] = {
	"/menubar/Graphs/ResidPlot", 
	"/menubar/Graphs/FittedActualPlot"
    };
    MODEL *pmod = vwin->data;
    char tmp[VNAMELEN2], aname[16];
    gchar *alabel;
    int *xlist;
    int v1, v2;
    int i, j;

    action_entry_init(&entry);

    xlist = gretl_model_get_x_list(pmod);
    
    for (i=0; i<2; i++) {
	/* plot against time/obs number */
	if (dataset_is_time_series(datainfo)) {
	    entry.label = _("_Against time");
	} else {
	    entry.label = _("By _observation number");
	}
	entry.name = (i == 0)? "r:byobs" : "f:byobs";
	entry.callback = (i == 0)? G_CALLBACK(resid_plot) : 
	    G_CALLBACK(fit_actual_plot);
	vwin_menu_add_item(vwin, mpath[i], &entry);

	if (pmod->ci == NLS || 
	    pmod->ci == MLE || 
	    pmod->ci == GMM ||
	    pmod->ci == PANEL) {
	    continue;
	}

	/* if doing resid plot, put dependent var in menu */
	if (i == 0) {
	    v1 = gretl_model_get_depvar(pmod);
	    if (v1 > 0) {
		sprintf(aname, "r:xvar %d", v1); /* FIXME */
		double_underscores(tmp, datainfo->varname[v1]);
		alabel = g_strdup_printf(_("_Against %s"), tmp);
		entry.name = aname;
		entry.label = alabel;
		entry.callback = G_CALLBACK(resid_plot);
		vwin_menu_add_item(vwin, mpath[0], &entry);
		g_free(alabel);
	    }
	}

	if (xlist != NULL) {
	    /* put the independent vars on the menu list */
	    for (j=1; j<=xlist[0]; j++) {
		v1 = xlist[j];
		if (v1 == 0) {
		    continue;
		}
		if (!strcmp(datainfo->varname[v1], "time")) {
		    continue;
		}
		sprintf(aname, "%c:xvar %d", (i == 0)? 'r' : 'f', v1);
		double_underscores(tmp, datainfo->varname[v1]);
		alabel = g_strdup_printf(_("_Against %s"), tmp);
		entry.name = aname;
		entry.label = alabel;
		entry.callback = (i == 0)? G_CALLBACK(resid_plot) : 
		    G_CALLBACK(fit_actual_plot);
		vwin_menu_add_item(vwin, mpath[i], &entry);
		g_free(alabel);
	    }
	}

	if (i == 1) {
	    /* fitted values: offer Theil-type scatterplot */
	    entry.name = "f:theil";
	    entry.label = _("Actual vs. Fitted");
	    entry.callback = G_CALLBACK(fit_actual_plot);
	    vwin_menu_add_item(vwin, mpath[i], &entry);
	}
    }

    /* time series models: residual correlogram, spectrum */
    if (dataset_is_time_series(datainfo)) {
	vwin_menu_add_separator(vwin, "/menubar/Graphs");
	entry.name = "Correlogram";
	entry.label = _("Residual _correlogram");
	entry.callback = G_CALLBACK(residual_correlogram);
	vwin_menu_add_item(vwin, "/menubar/Graphs", &entry);
	entry.name = "Spectrum";
	entry.label = _("Residual _periodogram");
	entry.callback = G_CALLBACK(residual_periodogram);
	vwin_menu_add_item(vwin, "/menubar/Graphs", &entry);
    } else {
	vwin_menu_add_separator(vwin, "/menubar/Graphs");
    }

    /* residual Q-Q plot */
    entry.name = "QQPlot";
    entry.label = _("Residual _Q-Q plot");
    entry.callback = G_CALLBACK(residual_qq_plot);
    vwin_menu_add_item(vwin, "/menubar/Graphs", &entry);

    /* 3-D fitted versus actual plot? */
    if (xlist != NULL) {
	v1 = v2 = 0;
	if (pmod->ifc && xlist[0] == 3) {
	    v1 = xlist[2];
	    v2 = xlist[3];
	} else if (!pmod->ifc && xlist[0] == 2) {
	    v1 = xlist[1];
	    v2 = xlist[2];
	}
	if (v1 > 0 && v2 > 0) {
	    char tmp2[VNAMELEN2];

	    vwin_menu_add_separator(vwin, mpath[1]);
	    double_underscores(tmp, datainfo->varname[v1]);
	    double_underscores(tmp2, datainfo->varname[v2]);
	    alabel = g_strdup_printf(_("_Against %s and %s"), tmp, tmp2);	
	    entry.name = "splot";
	    entry.label = alabel;
	    entry.callback = G_CALLBACK(fit_actual_splot);
	    vwin_menu_add_item(vwin, mpath[1], &entry);
	    g_free(alabel);
	}
    }

    free(xlist);
}

static void plot_dummy_call (GtkRadioAction *action, 
			     GtkRadioAction *current,
			     windata_t *vwin)
{
    vwin->active_var = gtk_radio_action_get_current_value(action);
}

static void radio_action_init (GtkRadioActionEntry *a)
{
    a->stock_id = NULL;
    a->accelerator = NULL;
    a->tooltip = NULL;
}

static void add_dummies_to_plot_menu (windata_t *vwin)
{
    GtkActionEntry item;
    GtkRadioActionEntry *items;
    MODEL *pmod = vwin->data;
    const gchar *gpath = "/menubar/Graphs/ResidPlot";
    const gchar *spath = "/menubar/Graphs/ResidPlot/Separation";
    char tmp[VNAMELEN2];
    int *dlist = NULL;
    int i, vi, ndums;

    /* make a list of dummy independent variables */
    for (i=2; i<=pmod->list[0]; i++) {
	vi = pmod->list[i];
	if (vi == LISTSEP) {
	    break;
	} else if (vi > 0 && vi < datainfo->v && 
	    gretl_isdummy(datainfo->t1, datainfo->t2, Z[vi])) {
	    gretl_list_append_term(&dlist, vi);
	}
    }

    if (dlist == NULL) {
	return;
    }

    ndums = dlist[0];
    items = malloc((ndums + 1) * sizeof *items);
    if (items == NULL) {
	free(dlist);
	return;
    }

    /* add separator */
    vwin_menu_add_separator(vwin, gpath);

    /* add menu branch */
    action_entry_init(&item);
    item.name = "Separation";
    item.label = _("Separation");
    vwin_menu_add_menu(vwin, gpath, &item);

    /* configure "none" radio option */
    radio_action_init(&items[0]);
    items[0].name = "none";
    items[0].label = _("none");
    items[0].value = 0;

    /* put the dummy independent vars on the menu list */
    for (i=1; i<=dlist[0]; i++) {
	vi = dlist[i];
	radio_action_init(&items[i]);
	double_underscores(tmp, datainfo->varname[vi]);
	items[i].name = g_strdup_printf("dum %d", vi);
	items[i].label = g_strdup_printf(_("By %s"), tmp);
	items[i].value = vi;
    }

    vwin_menu_add_radios(vwin, spath, items, ndums + 1, 0,
			 G_CALLBACK(plot_dummy_call));

    for (i=1; i<=dlist[0]; i++) {
	g_free((gchar *) items[i].name);
	g_free((gchar *) items[i].label);
    }

    free(items);
    free(dlist);
}

static void varnum_from_action (GtkAction *action, int *i)
{
    const gchar *s = gtk_action_get_name(action);

    sscanf(s, "%*s %d", i);
}

static void tau_plot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = (MODEL *) vwin->data;
    int v, err;

    varnum_from_action(action, &v);
    err = plot_tau_sequence(pmod, datainfo, v);

    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }    
}

static void add_tau_plot_menu (windata_t *vwin)
{
    GtkActionEntry item;
    MODEL *pmod = vwin->data;
    char tmp[VNAMELEN2], aname[16];
    int i;

    action_entry_init(&item);
    item.name = "TauMenu";
    item.label = _("tau sequence");
    vwin_menu_add_menu(vwin, "/menubar/Graphs", &item);
    
    item.callback = G_CALLBACK(tau_plot_call);

    /* put the independent vars on the menu list */
    for (i=2; i<=pmod->list[0]; i++) {
	sprintf(aname, "tauseq %d", i - 2);
	double_underscores(tmp, datainfo->varname[pmod->list[i]]);
	item.name = aname;
	item.label = tmp;
	vwin_menu_add_item(vwin, "/menubar/Graphs/TauMenu", &item);
    }
}

static void x12_output_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = vwin->data;
    char *fname;

    if (pmod == NULL) return;

    fname = gretl_model_get_data(pmod, "x12a_output");
    if (fname != NULL) {
	char *p = strrchr(fname, '.');

	if (p != NULL && strlen(p) == 7) {
	    gchar *tmp = g_strdup(fname);

	    sprintf(p, ".%d", pmod->ID);
	    gretl_rename(tmp, fname);
	    g_free(tmp);
	}
	view_file(fname, 0, 0, 78, 350, VIEW_FILE);
    }
}

static const gchar *model_ui =
    "<ui>"
    " <menubar>"
    "  <menu action='File'>"
    "   <menuitem action='SaveAs'/>"
    "   <menuitem action='SaveAsIcon'/>"
    "   <menuitem action='SaveAndClose'/>"
#ifdef NATIVE_PRINTING
    "   <menuitem action='Print'/>"
#endif
    "   <menuitem action='TextEqn'/>"
    "   <menuitem action='Close'/>"
    "  </menu>"    
    "  <menu action='Edit'>"
    "   <menuitem action='Copy'/>"
    "   <menuitem action='Revise'/>"
#if 0
    "   <menuitem action='Restore'/>"
#endif
    "  </menu> "     
    "  <menu action='Tests'>"
    "   <menuitem action='omit'/>"
    "   <menuitem action='add'/>"
    "   <menuitem action='coeffsum'/>"
    "   <menuitem action='restrict'/>"
    "   <separator/>"
    "   <menuitem action='modtest:s'/>"
    "   <menuitem action='modtest:l'/>"
    "   <menuitem action='reset'/>"
    "   <separator/>"
    "   <menu action='Hsk'/>"
    "   <menuitem action='modtest:n'/>"
    "   <menuitem action='leverage'/>"
    "   <menuitem action='vif'/>"
    "   <menuitem action='chow'/>"
    "   <separator/>"
    "   <menuitem action='modtest:a'/>"
    "   <menuitem action='dwpval'/>"
    "   <menuitem action='modtest:h'/>"
    "   <menuitem action='qlrtest'/>"
    "   <menuitem action='cusum'/>"
    "   <menuitem action='cusum:r'/>"
    "   <menuitem action='modtest:c'/>"
    "   <separator/>"
    "   <menuitem action='hausman'/>"
    "  </menu>"      
    "  <menu action='Save'/>"
    "  <menu action='Graphs'>"
    "   <menu action='ResidPlot'/>"
    "   <menu action='FittedActualPlot'/>"
    "  </menu>"    
    "  <menu action='Analysis'>"
    "   <menuitem action='DisplayAFR'/>"
    "   <menuitem action='Forecasts'/>"
    "   <menuitem action='ConfIntervals'/>"
    "   <menuitem action='ConfEllipse'/>"
    "   <menuitem action='Covariance'/>"
    "   <menuitem action='ANOVA'/>"
    "   <menuitem action='Bootstrap'/>"
    "  </menu>"     
    " </menubar>"
    "</ui>";

static void 
set_up_model_view_menu (GtkWidget *window, windata_t *vwin) 
{
    MODEL *pmod = (MODEL *) vwin->data;
    GtkActionGroup *actions;
    GError *err = NULL;

    actions = gtk_action_group_new("ModelActions");
    gtk_action_group_set_translation_domain(actions, "gretl");

    gtk_action_group_add_actions(actions, model_items, 
				 G_N_ELEMENTS(model_items), 
				 vwin);
    gtk_action_group_add_actions(actions, model_test_items, 
				 G_N_ELEMENTS(model_test_items),
				 vwin);

    vwin->ui = gtk_ui_manager_new();
    gtk_ui_manager_insert_action_group(vwin->ui, actions, 0);
    g_object_unref(actions);

    gtk_ui_manager_add_ui_from_string(vwin->ui, model_ui, -1, &err);
    if (err != NULL) {
	g_message("building menus failed: %s", err->message);
	g_error_free(err);
    }

    if (pmod->ci != MLE && pmod->ci != GMM && pmod->ci != BIPROBIT) {
	if (RQ_SPECIAL_MODEL(pmod)) {
	    add_tau_plot_menu(vwin);
	} else {
	    add_vars_to_plot_menu(vwin);
	}
	add_model_dataset_items(vwin);
    }

    /* heteroskedasticity tests: the permissible options vary
       depending on the nature of the model
    */

    if (dataset_is_panel(datainfo)) {
	if (pmod->ci == OLS) {
	    vwin_menu_add_items(vwin, "/menubar/Tests/Hsk", 
				panel_hsk_items, 
				G_N_ELEMENTS(panel_hsk_items));
	} else if (pmod->ci == PANEL && (pmod->opt & OPT_F)) {
	    vwin_menu_add_items(vwin, "/menubar/Tests/Hsk", 
				panel_hsk_items + 1, 1);
	}	    
    } else if (pmod->ci == IVREG) {
	vwin_menu_add_items(vwin, "/menubar/Tests/Hsk", 
			    ivreg_hsk_items, 
			    G_N_ELEMENTS(ivreg_hsk_items));
    } else if (model_test_ok(MODTEST, OPT_W, pmod, datainfo)) {
	vwin_menu_add_items(vwin, "/menubar/Tests/Hsk", 
			    base_hsk_items, 
			    G_N_ELEMENTS(base_hsk_items));
	if (pmod->ncoeff == 1 || (pmod->ifc && pmod->ncoeff == 2)) {
	    flip(vwin->ui, "/menubar/Tests/Hsk/WhiteSquares", FALSE);
	}
    } 

    if (latex_is_ok() && !pmod->errcode && !RQ_SPECIAL_MODEL(pmod)) {
	add_model_tex_items(vwin);
    } else {
	add_missing_tex_items(vwin);
    }

    if (!text_equation_ok(pmod)) {
	flip(vwin->ui, "/menubar/File/TextEqn", FALSE);
    }

    if (pmod->ci != ARMA && pmod->ci != GARCH && 
	pmod->ci != NLS && pmod->ci != MLE && pmod->ci != GMM &&
	pmod->ci != PANEL && pmod->ci != ARBOND &&
	pmod->ci != DPANEL && pmod->ci != BIPROBIT) {
	add_dummies_to_plot_menu(vwin);
    }

    if (vwin->main != NULL) {
	gtk_window_add_accel_group(GTK_WINDOW(vwin->main), 
				   gtk_ui_manager_get_accel_group(vwin->ui));
    }

    vwin->mbar = gtk_ui_manager_get_widget(vwin->ui, "/menubar");

    /* disable some menu items if need be */
    adjust_model_menu_state(vwin, pmod);
}

enum {
    SYS_DATA_RESIDS,
    SYS_DATA_FITTED,
    SYS_DATA_SIGMA
};

static int sys_data_code (GtkAction *action)
{
    const gchar *s = gtk_action_get_name(action);

    if (!strcmp(s, "uhat")) {
	return SYS_DATA_RESIDS;
    } else if (!strcmp(s, "yhat")) {
	return SYS_DATA_FITTED;	
    } else if (!strcmp(s, "sigma")) {
	return SYS_DATA_SIGMA;
    } else {
	return SYS_DATA_RESIDS;
    }
}

static void system_data_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = NULL;
    equation_system *sys = NULL;
    const gretl_matrix *M = NULL;
    gchar *wtitle = NULL;
    PRN *prn;
    int code, k = 0;
    int err = 0;

    if (vwin->role == SYSTEM) {
	sys = (equation_system *) vwin->data;
    } else {
	var = (GRETL_VAR *) vwin->data;
    } 

    if ((var == NULL && sys == NULL) || bufopen(&prn)) {
	return;
    }

    code = sys_data_code(action);

    if (code == SYS_DATA_SIGMA) {
	if (var != NULL) {
	    wtitle = g_strdup(_("gretl: VAR covariance matrix"));
	    err = gretl_VAR_print_sigma(var, prn);
	} else {
	    wtitle = g_strdup(_("gretl: system covariance matrix"));
	    err = system_print_sigma(sys, prn);
	}
    } else if (code == SYS_DATA_RESIDS || code == SYS_DATA_FITTED) {
	const char *titles[] = {
	    N_("System residuals"),
	    N_("System fitted values")
	};
	const char *title;
	const char **heads = NULL;

	if (var != NULL) {
	    /* fitted values matrix not currently available */
	    M = (code == SYS_DATA_RESIDS)? gretl_VAR_get_residual_matrix(var) :
		NULL;
	} else {
	    M = (code == SYS_DATA_RESIDS)? sys->E : sys->yhat;
	}

	if (M == NULL) {
	    err = E_DATA;
	} else {
	    k = gretl_matrix_cols(M);
	    heads = malloc(k * sizeof *heads);
	    if (heads == NULL) {
		err = E_ALLOC;
	    }
	}

	if (!err) {
	    int i, v;

	    for (i=0; i<k && !err; i++) {
		v = (var != NULL)? gretl_VAR_get_variable_number(var, i) :
		    sys->lists[i][1];
		if (v < 0 || v >= datainfo->v) {
		    err = E_DATA;
		} else {
		    heads[i] = datainfo->varname[v];
		}
	    }
	}

	if (!err) {
	    title = (code == SYS_DATA_RESIDS)? titles[0] : titles[1];
	    wtitle = g_strdup_printf("gretl: %s", _(title));
	    gretl_matrix_print_with_col_heads(M, _(title), heads, prn);
	}

	free(heads);
    }

    if (err) {
	gui_errmsg(err);
	gretl_print_destroy(prn);
    } else {
	/* FIXME: add matrix as saveable data */
	view_buffer(prn, 80, 400, wtitle, PRINT, NULL);
    }

    g_free(wtitle);
}

static void add_x12_output_menu_item (windata_t *vwin)
{
    const gchar *mpath = "/menubar/Analysis";
    GtkActionEntry entry;

    vwin_menu_add_separator(vwin, mpath);

    action_entry_init(&entry);
    entry.name = "x12aout";
    entry.label = _("View X-12-ARIMA output");
    entry.callback = G_CALLBACK(x12_output_callback);
    vwin_menu_add_item(vwin, mpath, &entry);
}

#include "up_down.h" /* arrows for buttons below */

static GtkWidget *up_down_button (int up)
{
    GdkPixbuf *pbuf;
    GtkWidget *img, *w;

    if (up) {
	pbuf = gdk_pixbuf_new_from_inline(-1, up_pixbuf, FALSE, NULL);
    } else {
	pbuf = gdk_pixbuf_new_from_inline(-1, down_pixbuf, FALSE, NULL);
    }

    img = gtk_image_new_from_pixbuf(pbuf);
    w = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(w), img);
    g_object_unref(pbuf);

    return w;
}

static void set_order_vec (GtkWidget *view)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gretl_matrix *m;
    int v, i = 0;

    m = g_object_get_data(G_OBJECT(view), "ordvec");
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

    gtk_tree_model_get_iter_first(model, &iter);
    gtk_tree_model_get(model, &iter, 1, &v, -1);
    m->val[0] = v;

    while (gtk_tree_model_iter_next(model, &iter)) {
	gtk_tree_model_get(model, &iter, 1, &v, -1);
	m->val[++i] = v;
    }
}

static void sensitize_up_down (GtkTreeSelection *selection,
			       GtkWidget *view)
{
    gretl_matrix *v;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkWidget *up, *down;
    gboolean s;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    v = g_object_get_data(G_OBJECT(view), "ordvec");
    up = g_object_get_data(G_OBJECT(view), "up-button");
    down = g_object_get_data(G_OBJECT(view), "down-button");

    gtk_tree_model_get_iter_first(model, &iter);
    s = gtk_tree_selection_iter_is_selected(selection, &iter);
    gtk_widget_set_sensitive(up, !s);

    path = gtk_tree_path_new_from_indices(gretl_vector_get_length(v) - 1,
					  -1);
    s = gtk_tree_selection_path_is_selected(selection, path);
    gtk_widget_set_sensitive(down, !s);
    gtk_tree_path_free(path);
}

static void shift_var_up (GtkButton *b, GtkWidget *view)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter seliter, previter;
    GtkTreePath *path;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_get_selected(selection, &model, &seliter); 
    path = gtk_tree_model_get_path(model, &seliter);
    if (gtk_tree_path_prev(path)) {
	gtk_tree_model_get_iter(model, &previter, path);
	gtk_list_store_swap(GTK_LIST_STORE(model), &seliter, &previter);
	set_order_vec(view);
	sensitize_up_down(selection, view);
    }
    gtk_tree_path_free(path);
}

static void shift_var_down (GtkButton *b, GtkWidget *view)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter seliter, nextiter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_get_selected(selection, &model, &seliter); 
    nextiter = seliter;
    if (gtk_tree_model_iter_next(model, &nextiter)) {
	gtk_list_store_swap(GTK_LIST_STORE(model), &seliter, &nextiter);
	set_order_vec(view);
	sensitize_up_down(selection, view);
    }
}

static void dialog_add_order_selector (GtkWidget *dlg, GRETL_VAR *var,
				       gretl_matrix *ordvec)
{
    GtkWidget *b1, *b2;
    GtkWidget *vbox, *hbox;
    GtkWidget *bbox, *scroller;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeSelection *select;
    GtkListStore *store;
    GtkWidget *view, *lbl;
    GtkTreeIter iter;
    const char *vname;
    int i, j, v;

    vbox = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    hbox = gtk_hbox_new(FALSE, 5);
    lbl = gtk_label_new(_("Cholesky ordering:"));
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_set_data(G_OBJECT(view), "ordvec", ordvec);
    
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(NULL,
						      renderer,
						      "text", 0,
						      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);	

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_tree_view_set_reorderable(GTK_TREE_VIEW(view), FALSE);

    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    for (i=0; i<var->neqns; i++) {
	j = gretl_vector_get(ordvec, i);
	v = var->ylist[j+1];
	vname = datainfo->varname[v];
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, vname,
			   1, j, -1);
    }

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    gtk_tree_selection_select_iter(select, &iter);

    gtk_widget_set_size_request(view, 120 * gui_scale, -1);

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scroller),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW (scroller),
					GTK_SHADOW_IN);    
    gtk_container_add(GTK_CONTAINER(scroller), view);

    bbox = gtk_vbox_new(FALSE, 5);
    b1 = up_down_button(1);
    b2 = up_down_button(0);
    gtk_box_pack_start(GTK_BOX(bbox), b1, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(bbox), b2, FALSE, FALSE, 5);
    g_signal_connect(G_OBJECT(b1), "clicked",
		     G_CALLBACK(shift_var_up), view);
    g_signal_connect(G_OBJECT(b2), "clicked",
		     G_CALLBACK(shift_var_down), view);

    gtk_widget_set_sensitive(b1, FALSE);
    g_object_set_data(G_OBJECT(view), "up-button", b1);
    g_object_set_data(G_OBJECT(view), "down-button", b2);
    g_signal_connect(G_OBJECT(select), "changed",
		     G_CALLBACK(sensitize_up_down), view);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), scroller, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), bbox, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    g_object_unref(G_OBJECT(store));
}

static int 
impulse_response_setup (GRETL_VAR *var, gretl_matrix *ordvec, int *horizon, 
			int *bootstrap, double *alpha, gretlopt *gopt)
{
    gchar *title;
    int h = default_VAR_horizon(datainfo);
    const char *impulse_opts[] = {
	N_("include bootstrap confidence interval")
    };
    static int active[] = { 0 };
    GtkWidget *dlg;
    double conf = 1 - *alpha;
    int resp = 0;

    if (restricted_VECM(var)) {
	active[0] = -1;
    }

    title = g_strdup_printf("gretl: %s", _("impulse responses"));

    dlg = build_checks_dialog(title, NULL,
			      impulse_opts, 1, active, /* check */
			      0, NULL, /* no radios */
			      &h, _("forecast horizon (periods):"),
			      2, datainfo->n / 2, IRF_BOOT,
			      &resp);

    g_free(title);

    if (dlg == NULL) {
	return -1;
    }

    dialog_add_confidence_selector(dlg, &conf, gopt);

    if (ordvec != NULL) {
	dialog_add_order_selector(dlg, var, ordvec);
    }

    gtk_widget_show_all(dlg);

    if (resp < 0) {
	/* cancelled */
	*horizon = 0;
    } else {
	*horizon = h;
	*bootstrap = (active[0] > 0);
	*alpha = 1 - conf;
    }

    return resp;
}

static void impulse_params_from_action (GtkAction *action, 
					int *targ,
					int *shock)
{
    const gchar *s = gtk_action_get_name(action);

    sscanf(s, "Imp:%d:%d", targ, shock);
}

static int ordvec_default (gretl_matrix *v)
{
    int i, n = gretl_vector_get_length(v);

    for (i=0; i<n; i++) {
	if (v->val[i] != i) {
	    return 0;
	}
    }

    return 1;
}

static gretl_matrix *cholesky_order_vector (GRETL_VAR *var)
{
    gretl_matrix *v = NULL;

    if (var->ord != NULL) {
	v = gretl_matrix_copy(var->ord);
    } else if (var->neqns > 1) {
	int i;

	v = gretl_vector_alloc(var->neqns);
	if (v != NULL) {
	    for (i=0; i<var->neqns; i++) {
		v->val[i] = i;
	    }
	    
	}
    }

    return v;
}

static void impulse_plot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = (GRETL_VAR *) vwin->data;
    int horizon, bootstrap;
    gint shock, targ;
    static double alpha = 0.10;
    static gretlopt gopt = OPT_NONE;
    const double **vZ = NULL;
    gretl_matrix *ordvec = NULL;
    int resp, err;

    impulse_params_from_action(action, &targ, &shock);
    ordvec = cholesky_order_vector(var);

    resp = impulse_response_setup(var, ordvec, &horizon, &bootstrap, 
				  &alpha, &gopt);

    if (resp < 0) {
	/* canceled */
	gretl_matrix_free(ordvec);
	return;
    }

    if (bootstrap) {
	vZ = (const double **) Z;
    }

    if (ordvec != NULL) {
	if (ordvec_default(ordvec)) {
	    gretl_matrix_free(ordvec);
	    gretl_VAR_set_ordering(var, NULL);
	} else {
	    gretl_VAR_set_ordering(var, ordvec);
	}
    }

    err = gretl_VAR_plot_impulse_response(var, targ, shock, 
					  horizon, alpha, 
					  vZ, datainfo,
					  gopt);

    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }
}

static void multiple_irf_plot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = (GRETL_VAR *) vwin->data;
    const double **vZ = NULL;
    int horizon, bootstrap;
    static double alpha = 0.10;
    static gretlopt gopt = OPT_NONE;
    gretl_matrix *ordvec = NULL;
    int resp, err;

    ordvec = cholesky_order_vector(var);

    resp = impulse_response_setup(var, ordvec, &horizon, 
				  &bootstrap, &alpha, &gopt);

    if (resp < 0) {
	/* canceled */
	gretl_matrix_free(ordvec);
	return;
    }

    if (bootstrap) {
	vZ = (const double **) Z;
    }   

    if (ordvec != NULL) {
	if (ordvec_default(ordvec)) {
	    gretl_matrix_free(ordvec);
	    gretl_VAR_set_ordering(var, NULL);
	} else {
	    gretl_VAR_set_ordering(var, ordvec);
	}
    }    

    err = gretl_VAR_plot_multiple_irf(var, horizon, alpha, 
				      vZ, datainfo, gopt);

    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }
}

static int VAR_model_data_code (GtkAction *action)
{
    const gchar *s = gtk_action_get_name(action);

    if (!strcmp(s, "VarIrf")) {
	return VAR_IRF;
    } else if (!strcmp(s, "VarDecomp")) {
	return VAR_DECOMP;
    } else {
	return VAR_IRF;
    }
}

static void VAR_model_data_callback (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = vwin->data;
    gretl_matrix *ordvec;
    GtkWidget *dlg;
    gchar *title;
    PRN *prn = NULL;
    int code, h = 0;
    int resp = 0;
    int err;

    if (var == NULL) {
	return;
    }

    code = VAR_model_data_code(action);
    h = default_VAR_horizon(datainfo);
    ordvec = cholesky_order_vector(var);

    title = g_strdup_printf("gretl: %s", 
			    (code == VAR_IRF)? _("impulse responses") :
			    _("variance decompositions"));

    dlg = build_checks_dialog(title, NULL, NULL, 0, 
			      NULL, 0, NULL,
			      &h, _("forecast horizon (periods):"),
			      2, datainfo->n / 2, 0,
			      &resp);

    if (dlg == NULL) {
	goto bailout;
    }

    if (ordvec != NULL) {
	dialog_add_order_selector(dlg, var, ordvec);
    }

    /* blocks till response is selected */
    gtk_widget_show_all(dlg);

    if (resp < 0) {
	/* canceled */
	goto bailout;
    }

    if (bufopen(&prn)) {
	goto bailout;
    } 

    if (ordvec != NULL) {
	if (ordvec_default(ordvec)) {
	    gretl_matrix_free(ordvec);
	    ordvec = NULL;
	} 
	gretl_VAR_set_ordering(var, ordvec);
	ordvec = NULL;
    }    

    if (code == VAR_IRF) {
	err = gretl_VAR_print_all_impulse_responses(var, datainfo, h, prn);
    } else if (code == VAR_DECOMP) {
	err = gretl_VAR_print_all_fcast_decomps(var, datainfo, h, prn);
    } else {
	err = 1;
    }

    if (err) {
	gui_errmsg(err);
	gretl_print_destroy(prn);
    } else {
	windata_t *viewer;

	viewer = view_buffer_with_parent(vwin, prn, 80, 400, title, 
					 code, NULL);
	/* for use when printing in other formats */
	viewer->active_var = h;
    }

 bailout:

    g_free(title);
    gretl_matrix_free(ordvec);
}

static void system_forecast_callback (GtkAction *action, gpointer p)
{
    static gretlopt gopt = OPT_P;
    windata_t *vwin = (windata_t *) p;
    int ci = vwin->role;
    GRETL_VAR *var = NULL;
    equation_system *sys = NULL;
    FITRESID *fr;
    int t1, t2, t2est, resp;
    int premax, pre_n, dyn_ok;
    int static_model = 0;
    gretlopt opt = OPT_NONE;
    double conf = 0.95;
    int i, err = 0;

    varnum_from_action(action, &i);

    if (ci == VAR || ci == VECM) {
	var = (GRETL_VAR *) vwin->data;
	t2est = gretl_VAR_get_t2(var);
    } else if (ci == SYSTEM) {
	sys = (equation_system *) vwin->data;
	t2est = sys->t2;
	static_model = (sys->order == 0);
    } else {
	return;
    }

    t2 = datainfo->n - 1;

    /* if no out-of-sample obs are available, alert the user */
    if (t2 == t2est) {
	err = out_of_sample_info(1, &t2);
	if (err) {
	    return;
	}
	t2 = datainfo->n - 1;
    }

    /* max number of pre-forecast obs in "best case" */
    premax = datainfo->n - 1;

    /* if there are spare obs available, default to an
       out-of-sample forecast */
    if (t2 > t2est) {
	t1 = t2est + 1;
	pre_n = t2est / 2;
	if (pre_n > 100) {
	    pre_n = 100;
	}
	dyn_ok = !static_model;
    } else {
	if (var != NULL) {
	    t1 = effective_order(var);
	} else {
	    t1 = sys->order;
	}
	pre_n = 0;
	dyn_ok = 0;
    }

    /* FIXME pre_n with static fcast? */

    resp = forecast_dialog(t1, t1, &t1,
			   t1, t2, &t2, NULL,
			   0, premax, &pre_n,
			   dyn_ok, &gopt, &conf, 
			   NULL);
    if (resp < 0) {
	return;
    }

    if (resp == 1) {
	opt = OPT_D;
    } else if (resp == 2) {
	opt = OPT_S;
    }

    fr = get_system_forecast(vwin->data, ci, i, t1, t2, pre_n,
			     (const double **) Z, datainfo, 
			     opt, &err);

    if (err) {
	gui_errmsg(err);
    } else {
	int width = 78;
	PRN *prn;

	if (bufopen(&prn)) {
	    return;
	}

	fr->alpha = 1 - conf;
	err = text_print_forecast(fr, datainfo, gopt, prn);
	if (!err) {
	    register_graph(NULL);
	}
	if (fr->sderr == NULL) {
	    width = 50;
	}
	view_buffer(prn, width, 400, _("gretl: forecasts"), FCAST, fr);
    }
}

enum {
    SYS_AUTOCORR_TEST,
    SYS_ARCH_TEST,
    SYS_NORMALITY_TEST,
    SYS_RESTRICT
};

static int sys_test_code (GtkAction *action)
{
    const gchar *s = gtk_action_get_name(action);

    if (!strcmp(s, "autocorr")) {
	return SYS_AUTOCORR_TEST;
    } else if (!strcmp(s, "ARCH")) {
	return SYS_ARCH_TEST;
    } else if (!strcmp(s, "normtest")) {
	return SYS_NORMALITY_TEST;
    } else if (!strcmp(s, "restrict")) {
	return SYS_RESTRICT;
    } else {
	return SYS_NORMALITY_TEST;
    }
}

static void system_test_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = NULL;
    equation_system *sys = NULL;
    gchar *title = NULL;
    gchar *cstr = NULL;
    PRN *prn;
    int code, order = 0;
    int err = 0;

    if (bufopen(&prn)) {
	return;
    }

    code = sys_test_code(action);

    if (vwin->role == SYSTEM) {
	sys = (equation_system *) vwin->data;
    } else {
	var = (GRETL_VAR *) vwin->data;
    }

    if (code == SYS_AUTOCORR_TEST || code == SYS_ARCH_TEST) {
	order = default_lag_order(datainfo);
	set_window_busy(vwin);
	err = spin_dialog((code == SYS_AUTOCORR_TEST)?
			  _("gretl: autocorrelation") :
			  _("gretl: ARCH test"), NULL,
			  &order, _("Lag order for test:"),
			  1, datainfo->n / 2, 0);
	unset_window_busy(vwin);
	if (err < 0) {
	    gretl_print_destroy(prn);
	    return;
	}
    }	

    if (code == SYS_AUTOCORR_TEST) {
	title = g_strdup(_("gretl: autocorrelation"));
	cstr = g_strdup_printf("modtest %d --autocorr", order);
	if (var != NULL) {
	    err = gretl_VAR_autocorrelation_test(var, order, 
						 &Z, datainfo, 
						 prn);
	} else {
	    err = system_autocorrelation_test(sys, order, prn);
	}
    } else if (code == SYS_ARCH_TEST) {
	title = g_strdup(_("gretl: ARCH test"));
	cstr = g_strdup_printf("modtest %d --arch", order);
	if (var != NULL) {
	    err = gretl_VAR_arch_test(var, order, datainfo, prn);
	} else {
	    err = system_arch_test(sys, order, prn);
	}
    } else if (code == SYS_NORMALITY_TEST) {
	title = g_strdup_printf("gretl: %s", _("Test for normality of residual"));
	cstr = g_strdup("normtest");
	if (var != NULL) {
	    err = gretl_VAR_normality_test(var, prn);
	} else {
	    err = system_normality_test(sys, prn);
	}
    } else {
	err = 1;
    }

    if (err) {
	gui_errmsg(err);
	gretl_print_destroy(prn);
    } else {
	add_command_to_stack(cstr);
	view_buffer(prn, 78, 400, title, PRINT, NULL); 
    }

    g_free(title);
    g_free(cstr);
}

static void VAR_roots_plot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = (GRETL_VAR *) vwin->data;
    int err;

    err = gretl_VAR_roots_plot(var);
    
    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }
}

static int sys_ci_from_action (GtkAction *action)
{
    const gchar *s = gtk_action_get_name(action);
    char cmdword[9];

    sscanf(s, "%*s %8s", cmdword);
    return gretl_command_number(cmdword);
}

static void system_resid_plot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    int ci = sys_ci_from_action(action);
    int err;

    err = gretl_system_residual_plot(vwin->data, ci, datainfo);
    
    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }
}

static void system_resid_mplot_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    int ci = sys_ci_from_action(action);
    int err;

    err = gretl_system_residual_mplot(vwin->data, ci, datainfo);
    
    if (err) {
	gui_errmsg(err);
    } else {
	register_graph(NULL);
    }
}

static void add_system_menu_items (windata_t *vwin, int ci)
{
    GtkActionEntry item;
    const gchar *top = "/menubar";
    const gchar *tests = "/menubar/Tests";
    const gchar *save = "/menubar/Save";
    const gchar *graphs = "/menubar/Graphs";
    const gchar *analysis = "/menubar/Analysis";
    GRETL_VAR *var = NULL;
    equation_system *sys = NULL;
    int neqns, nfc, vtarg, vshock;
    char tmp[VNAMELEN2], istr[16];
    char maj[64], min[32];
    const char *cmdword;
    int i, j;

    if (ci == SYSTEM) {
	sys = (equation_system *) vwin->data;
	neqns = sys->neqns;
	nfc = sys->neqns + sys->nidents;
    } else {
	var = (GRETL_VAR *) vwin->data;
	nfc = neqns = gretl_VAR_get_n_equations(var);
    }

    cmdword = gretl_command_word(ci);   
    action_entry_init(&item);

    /* FIXME: the following two tests should really be multivariate */

    if (dataset_is_time_series(datainfo)) {
	/* univariate autocorrelation tests */
	item.name = "autocorr";
	item.label = N_("_Autocorrelation");
	item.callback = G_CALLBACK(system_test_call);
	vwin_menu_add_item(vwin, tests, &item);

	/* univariate ARCH tests */
	item.name = "ARCH";
	item.label = N_("A_RCH");
	vwin_menu_add_item(vwin, tests, &item);
    }

    /* multivariate normality test */
    item.name = "normtest";
    item.label = N_("_Normality of residuals");
    vwin_menu_add_item(vwin, tests, &item);

    if (ci == VECM || ci == SYSTEM) {
	/* linear restrictions */
	item.name = "restrict";
	item.label = N_("Linear restrictions");
	item.callback = G_CALLBACK(gretl_callback);
	vwin_menu_add_item(vwin, tests, &item);
    } else if (ci == VAR) {
	/* regular VAR: omit exogenous variables test */
	if (gretl_VAR_get_exo_list(var) != NULL) {
	    item.name = "VarOmit";
	    item.label = N_("Omit exogenous variables...");
	    item.callback = G_CALLBACK(selector_callback);
	    vwin_menu_add_item(vwin, tests, &item);
	}	    
    }

    /* Save residuals */
    for (i=0; i<neqns; i++) {
	sprintf(istr, "resid %d", i);
	sprintf(maj, "%s %d", _("Residuals from equation"), i + 1);
	item.name = istr;
	item.label = maj;
	item.callback = G_CALLBACK(add_system_resid);
	vwin_menu_add_item(vwin, save, &item);
    }

    /* Display residual matrix */
    item.name = "uhat";
    item.label = N_("Display residuals, all equations");
    item.callback = G_CALLBACK(system_data_callback);
    vwin_menu_add_item(vwin, analysis, &item);

    if (ci == SYSTEM) {
	/* Display fitted values matrix */
	item.name = "yhat";
	item.label = N_("Display fitted values, all equations");
	vwin_menu_add_item(vwin, analysis, &item);
    }  

    if (neqns > 1) {
	/* Display VCV matrix */
	item.name = "sigma";
	item.label = N_("Cross-equation covariance matrix");
	vwin_menu_add_item(vwin, analysis, &item);
    }

    if (ci == VAR || ci == VECM) {
	/* impulse response printout */
	item.name = "VarIrf";
	item.label = N_("Impulse responses");
	item.callback = G_CALLBACK(VAR_model_data_callback);
	vwin_menu_add_item(vwin, analysis, &item);

	/* variance decomp printout */
	item.name = "VarDecomp";
	item.label = N_("Forecast variance decomposition");
	vwin_menu_add_item(vwin, analysis, &item);
    }

    if (neqns > 1 && neqns <= 6) {
	/* multiple residual plots */
	sprintf(min, "multiresid %s", cmdword);
	item.name = min;
	item.label = N_("Residual plots");
	item.callback = G_CALLBACK(system_resid_mplot_call);
	vwin_menu_add_item(vwin, graphs, &item);
    }

    /* combined residual plot */
    if (neqns > 1) {
	sprintf(min, "comboresid %s", cmdword);
	item.name = min;
	item.label = N_("Combined residual plot");
	item.callback = G_CALLBACK(system_resid_plot_call);
	vwin_menu_add_item(vwin, graphs, &item);
    }

    if (ci != SYSTEM) {
	/* VAR inverse roots */
	item.name = "VarRoots";
	item.label = N_("VAR inverse roots");
	item.callback = G_CALLBACK(VAR_roots_plot_call);
	vwin_menu_add_item(vwin, graphs, &item);
    }

    if (ci != SYSTEM && neqns > 1 && neqns <= 4) {
	/* Multiple IRFs */
	item.name = "MultiIrf";
	item.label = N_("Impulse responses (combined)");
	item.callback = G_CALLBACK(multiple_irf_plot_call);
	vwin_menu_add_item(vwin, graphs, &item);
    }

    for (i=0; i<nfc; i++) {
	char newpath[64];
	int dv;

	/* forecast items */
	if (var != NULL) {
	    dv = gretl_VAR_get_variable_number(var, i);
	} else {
	    dv = sys->ylist[i+1];
	}
	double_underscores(tmp, datainfo->varname[dv]);
	sprintf(istr, "fcast %d", i);
	item.name = istr;
	item.label = tmp;
	item.callback = G_CALLBACK(system_forecast_callback);
	vwin_menu_add_item(vwin, "/menubar/Analysis/Forecasts", &item);

	if (var == NULL) {
	    continue;
	}

	/* impulse response plots: make menu for target */
	vtarg = gretl_VAR_get_variable_number(var, i);
	double_underscores(tmp, datainfo->varname[vtarg]);
	sprintf(istr, "targ_%d", i);
	sprintf(maj, _("Response of %s"), tmp);
	item.name = istr;
	item.label = maj;
	item.callback = NULL;
	vwin_menu_add_menu(vwin, graphs, &item);

	/* path under which to add shocks */
	sprintf(newpath, "/menubar/Graphs/targ_%d", i);
	
	for (j=0; j<neqns; j++) {
	    /* impulse responses: subitems for shocks */
	    vshock = gretl_VAR_get_variable_number(var, j);
	    double_underscores(tmp, datainfo->varname[vshock]);
	    sprintf(istr, "Imp:%d:%d", i, j);
	    sprintf(min, _("to %s"), tmp);
	    item.name = istr;
	    item.label = min;
	    item.callback = G_CALLBACK(impulse_plot_call);
	    vwin_menu_add_item(vwin, newpath, &item);
	}
    }

    if (ci == VECM) {
	/* save ECs items */
	for (i=0; i<jrank(var); i++) {
	    sprintf(istr, "EC %d", i);
	    sprintf(maj, "%s %d", _("EC term"), i+1);
	    item.name = istr;
	    item.label = maj;
	    item.callback = G_CALLBACK(VECM_add_EC_data);
	    vwin_menu_add_item(vwin, save, &item);
	}
    }

    if (latex_is_ok()) {
	int n = G_N_ELEMENTS(sys_tex_items);

	vwin_menu_add_menu(vwin, top, &sys_tex_items[0]);
	vwin_menu_add_items(vwin, "/menubar/LaTeX",
			    sys_tex_items + 1, n - 1);
    }
}

static void save_bundled_item_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    gretl_bundle *bundle = vwin->data;
    const gchar *key = gtk_action_get_name(action);
    const char *note;
    GretlType type;
    void *val;
    int size = 0;
    int err = 0;

    val = gretl_bundle_get_data(bundle, key, &type, &size, &err);
    if (err) {
	gui_errmsg(err);
	return;
    }

    note = gretl_bundle_get_note(bundle, key);

    if (type == GRETL_TYPE_SERIES && size == datainfo->n) {
	const double *x = (double *) val;

	save_bundled_series(x, key, note);
    } else {
	char vname[VNAMELEN];
	gchar *blurb;
	int cancel = 0;

	strcpy(vname, key);
	blurb = g_strdup_printf("%s (%s) from bundle\n"
				"Name (max. 15 characters):",
				key, gretl_arg_type_name(type));
	object_name_entry_dialog(vname, type, blurb, &cancel);
	g_free(blurb);

	if (!cancel) {
	    if (type == GRETL_TYPE_DOUBLE) {
		double x = *(double *) val;
		
		err = gretl_scalar_add(vname, x);
	    } else if (type == GRETL_TYPE_MATRIX) {
		gretl_matrix *m = (gretl_matrix *) val;

		err = user_matrix_add(gretl_matrix_copy(m), vname);
	    } else if (type == GRETL_TYPE_SERIES) {
		double *x = (double *) val;
		gretl_matrix *m;

		m = gretl_vector_from_array(x, size, GRETL_MOD_NONE);
		err = user_matrix_add(m, vname);
	    } else if (type == GRETL_TYPE_STRING) {
		char *s = (char *) val;

		err = save_named_string(vname, s, NULL);
	    }
	}
    }

    if (err) {
	gui_errmsg(err);
    }
}

static void save_bundle_call (GtkAction *action, gpointer p)
{
    windata_t *vwin = (windata_t *) p;
    gretl_bundle *bundle = vwin->data;
    int nb = n_user_bundles();
    char vname[VNAMELEN];
    const char *blurb;
    int cancel = 0;

    sprintf(vname, "bundle%d", nb + 1);
    blurb = "Save bundle\nName (max. 15 characters):";
    object_name_entry_dialog(vname, GRETL_TYPE_BUNDLE, blurb, &cancel);

    if (!cancel) {    
	int err = gretl_bundle_add_or_replace(bundle, vname);

	if (err) {
	    gui_errmsg(err);
	} 
    }   
}

static void add_bundled_item_to_menu (gpointer key, 
				      gpointer value, 
				      gpointer data)
{
    windata_t *vwin = data;
    GtkActionEntry item;
    gchar *label;
    const char *typestr = "?";
    const char *note;
    GretlType type;
    void *val;
    int size = 0;

    val = bundled_item_get_data((bundled_item *) value, &type, &size);
    if (val == NULL) {
	/* shouldn't be possible */
	return;
    }

    if (type == GRETL_TYPE_STRING || type == GRETL_TYPE_MATRIX_REF) {
	/* not very useful in GUI? */
	return;
    }

    if (type == GRETL_TYPE_SERIES && size != datainfo->n) {
	type = GRETL_TYPE_MATRIX;
    }

    typestr = gretl_arg_type_name(type);
    note = bundled_item_get_note((bundled_item *) value);

    if (note != NULL) {
	label = g_strdup_printf("%s (%s: %s)", (gchar *) key, 
				typestr, note);
    } else {
	label = g_strdup_printf("%s (%s)", (gchar *) key, typestr);
    }

    action_entry_init(&item);    
    item.name = (gchar *) key;
    item.label = label;
    item.callback = G_CALLBACK(save_bundled_item_call);
    vwin_menu_add_item(vwin, "/menubar/Save", &item);

    g_free(label);
}

static void add_bundle_menu_items (windata_t *vwin)
{
    gretl_bundle *bundle = vwin->data;
    int n = gretl_bundle_n_keys(bundle);

    if (n > 0) {
	GHashTable *ht = (GHashTable *) gretl_bundle_get_content(bundle);

	if (!gretl_bundle_is_stacked(bundle)) {
	    GtkActionEntry item;
    
	    action_entry_init(&item);    
	    item.name = "bundle";
	    item.label = _("Save entire bundle");
	    item.callback = G_CALLBACK(save_bundle_call);
	    vwin_menu_add_item(vwin, "/menubar/Save", &item);
	    vwin_menu_add_separator(vwin, "/menubar/Save");
	}

	g_hash_table_foreach(ht, add_bundled_item_to_menu, vwin);
    }
}

static int set_sample_from_model (MODEL *pmod)
{
    int full = (pmod->submask == NULL);
    int err = 0;

    /* first restore the full dataset */
    err = restore_full_sample(&Z, datainfo, NULL);

    /* then, if the model was subsampled, restore the subsample */
    if (!err && !full) {
	err = restrict_sample_from_mask(pmod->submask, &Z, datainfo, OPT_NONE);
    }

    if (err) {
	gui_errmsg(err);
    } else {
	if (full) {
	    restore_sample_state(FALSE);
	    gretl_command_strcpy("smpl --full");
	    check_and_record_command();
	} else {
	    char comment[64];

	    restore_sample_state(TRUE);
	    sprintf(comment, "# restored sample from model %d\n", pmod->ID);
	    add_command_to_stack(comment);
	}

	mark_session_changed();
	set_sample_label(datainfo);
    }

    return err;
}

/* maybe_set_sample_from_model: return TRUE if the problem situation
   (sample mismatch) is successfully handled, else FALSE.
*/

static gboolean maybe_set_sample_from_model (MODEL *pmod)
{
    const char *msg = N_("The model sample differs from the dataset sample,\n"
			 "so some menu options will be disabled.\n\n"
			 "Do you want to restore the sample on which\n"
			 "this model was estimated?");
    int resp, err = 0;

    resp = yes_no_dialog(NULL, _(msg), 0);

    if (resp == GRETL_NO) {
	return FALSE;
    }

    err = set_sample_from_model(pmod);

    return (err == 0);
}

static gint check_model_menu (GtkWidget *w, GdkEventButton *eb, 
			      gpointer data)
{
    windata_t *vwin = (windata_t *) data;
    MODEL *pmod = vwin->data;
    GtkAction *action;
    gboolean s, ok = TRUE;

    if (RQ_SPECIAL_MODEL(pmod)) {
	return FALSE;
    }

    if (Z == NULL) {
	flip(vwin->ui, "/menubar/File/SaveAsIcon", FALSE);
	flip(vwin->ui, "/menubar/File/SaveAndClose", FALSE);
	flip(vwin->ui, "/menubar/Edit/Copy", FALSE);
	flip(vwin->ui, "/menubar/Tests", FALSE);
	flip(vwin->ui, "/menubar/Graphs", FALSE);
	flip(vwin->ui, "/menubar/Analysis", FALSE);
	return FALSE;
    }

    if (pmod->ci == MLE || pmod->ci == GMM || 
	pmod->ci == MPOLS || pmod->ci == BIPROBIT) {
	return FALSE;
    }

    if (model_sample_problem(pmod, datainfo)) { 
	ok = FALSE;
    }

    action = gtk_ui_manager_get_action(vwin->ui, "/menubar/Save/uhat");
    s = gtk_action_is_sensitive(action);

    if (s == ok) {
	/* no need to flip state */
	return FALSE;
    }    

    if (s && !ok) {
	ok = maybe_set_sample_from_model(pmod);
	if (ok) {
	    return FALSE;
	}
    } 

    flip(vwin->ui, "/menubar/Analysis/Forecasts", ok);
    flip(vwin->ui, "/menubar/Save/yhat", ok);
    flip(vwin->ui, "/menubar/Save/uhat", ok);
    flip(vwin->ui, "/menubar/Save/uhat2", ok);
    flip(vwin->ui, "/menubar/Save/NewVar", ok);

    return FALSE;
}

static gint check_VAR_menu (GtkWidget *w, GdkEventButton *eb, 
			    gpointer data)
{
    windata_t *vwin = (windata_t *) data;
    GtkAction *action;
    gboolean s, ok = TRUE;

    if (complex_subsampled()) { 
	ok = FALSE;
    }

    action = gtk_ui_manager_get_action(vwin->ui, "/menubar/Tests");
    s = gtk_action_is_sensitive(action);

    if (s == ok) {
	/* no need to flip state */
	return FALSE;
    }

    flip(vwin->ui, "/menubar/Edit/Revise", ok);
    flip(vwin->ui, "/menubar/Tests", ok);
    flip(vwin->ui, "/menubar/Save", ok);
    flip(vwin->ui, "/menubar/Graphs", ok);
    flip(vwin->ui, "/menubar/Analysis/Forecasts", ok);
    flip(vwin->ui, "/menubar/Analysis/VarIrf", ok);
    flip(vwin->ui, "/menubar/Analysis/VarDecomp", ok);

    if (!ok) {
	warnbox(_("dataset is subsampled"));
    }

    return FALSE;
}

static gchar *exists_string (const char *name, GretlType t)
{
    gchar *s = NULL;

    if (t == GRETL_TYPE_SERIES) {
	s = g_strdup_printf(_("A series named %s already exists"), name);
    } else if (t == GRETL_TYPE_MATRIX) {
	s = g_strdup_printf(_("A matrix named %s already exists"), name);
    } else if (t == GRETL_TYPE_DOUBLE) {
	s = g_strdup_printf(_("A scalar named %s already exists"), name);
    } else if (t == GRETL_TYPE_LIST) {
	s = g_strdup_printf(_("A list named %s already exists"), name);
    } else if (t == GRETL_TYPE_STRING) {
	s = g_strdup_printf(_("A string named %s already exists"), name);
    }

    return s;
}

static int object_overwrite_ok (const char *name, GretlType t)
{
    gchar *info = exists_string(name, t);
    gchar *msg = g_strdup_printf("%s\n%s", info, _("OK to overwrite it?"));
    int resp;

    resp = yes_no_dialog("gretl", msg, 0);
    g_free(info);
    g_free(msg);

    return (resp == GRETL_YES);
}	    

int real_gui_validate_varname (const char *name, GretlType t,
			       int allow_overwrite)
{
    int i, n = strlen(name);
    char namebit[VNAMELEN];
    unsigned char c;
    int err = 0;

    *namebit = 0;
    
    if (n > VNAMELEN - 1) {
	strncat(namebit, name, VNAMELEN - 1);
	errbox(_("Variable name %s... is too long\n"
		 "(the max is %d characters)"), namebit,
	       VNAMELEN - 1);
	err = 1;
    } else if (!(isalpha(*name))) {
	errbox(_("First char of name ('%c') is bad\n"
		 "(first must be alphabetical)"), *name);
	err = 1;
    } else {
	for (i=1; i<n && !err; i++) {
	    c = (unsigned char) name[i];
	
	    if ((!(isalpha(c)) && !(isdigit(c)) && c != '_') || c > 127) {
		errbox(_("Name contains an illegal char (in place %d)\n"
			 "Use only unaccented letters, digits and underscore"), i + 1);
		err = 1;
	    }
	}
    }

    if (!err && t != GRETL_TYPE_NONE) {
	/* check for collisions */
	GretlType t0 = gretl_type_from_name(name, datainfo);

	if (t0 != GRETL_TYPE_NONE) {
	    if (t == t0 && allow_overwrite) {
		err = !object_overwrite_ok(name, t);
	    } else {
		/* won't work */
		gchar *msg = exists_string(name, t0);
		
		errbox(msg);
		g_free(msg);
		err = 1;
	    }
	}
    }
	

    return err;
}

int gui_validate_varname (const char *name, GretlType t)
{
    return real_gui_validate_varname(name, t, 1);
}

int gui_validate_varname_strict (const char *name, GretlType t)
{
    return real_gui_validate_varname(name, t, 0);
}

gint popup_menu_handler (GtkWidget *widget, GdkEvent *event,
			 gpointer data)
{
    GdkModifierType mods = widget_get_pointer_mask(widget);

    if (RIGHT_CLICK(mods) && event->type == GDK_BUTTON_PRESS) {
	GdkEventButton *bevent = (GdkEventButton *) event; 

	gtk_menu_popup (GTK_MENU(data), NULL, NULL, NULL, NULL,
			bevent->button, bevent->time);
	return TRUE;
    }
    return FALSE;
}

void add_popup_item (const gchar *label, GtkWidget *menu,
		     GCallback callback, 
		     gpointer data)
{
    GtkWidget *item;

    item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect (G_OBJECT(item), "activate",
		      G_CALLBACK(callback), data);
    gtk_widget_show(item);
}

void *gui_get_plugin_function (const char *funcname, 
			       void **phandle)
{
    void *func;

    func = get_plugin_function(funcname, phandle);
    if (func == NULL) {
	errbox(gretl_errmsg_get());
    }

    return func;
}

char *double_underscores (char *targ, const char *src)
{
    char *p = targ;

    while (*src) {
	if (*src == '_') {
	    *p++ = '_';
	    *p++ = '_';
	} else {
	    *p++ = *src;
	}
	src++;
    }
    *p = '\0';

    return targ;
}

#ifdef G_OS_WIN32

/* MS Windows variants of functions to exec some third-party
   programs */

static void run_R_sync (void)
{
    gchar *cmd;
    int err;

    /* should we use Rlib instead of Rterm? */

    cmd = g_strdup_printf("\"%s\" --no-save --no-init-file --no-restore-data "
			  "--slave", gretl_rbin_path());

    err = win_run_sync(cmd, NULL);

    if (err) {
	gui_errmsg(err);
    } else {
	gchar *Rout = g_strdup_printf("%sR.out", gretl_dotdir());

	view_file(Rout, 0, 1, 78, 350, VIEW_FILE);
	g_free(Rout);
    }

    g_free(cmd);
}

static void run_ox_or_octave (gchar *buf, int ox)
{
    const char *fname;
    int err;

    if (ox) {
	err = write_gretl_ox_file(buf, OPT_G, &fname);
    } else {
	err = write_gretl_octave_file(buf, OPT_G, &fname);
    }

    if (err) {
	gui_errmsg(err);
    } else {
	char *sout = NULL;
	gchar *cmd;

	if (ox) {
	    cmd = g_strdup_printf("\"%s\" \"%s\"", gretl_oxl_path(), fname);
	} else {
	    cmd = g_strdup_printf("\"%s\" -q \"%s\"", gretl_octave_path(), fname);
	}

	err = gretl_win32_grab_output(cmd, &sout);
	g_free(cmd);

	if (sout == NULL) {
	    warnbox("Got no output");
	} else {
	    PRN *prn = gretl_print_new_with_buffer(sout);
    
	    if (prn != NULL) {
		view_buffer(prn, 78, 350, _("gretl: script output"), PRINT, NULL);
	    } else {
		free(sout);
	    }
	}
    }
}

void run_ox_script (gchar *buf)
{
    run_ox_or_octave(buf, 1);
}

void run_octave_script (gchar *buf)
{
    run_ox_or_octave(buf, 0);
}

#else /* some non-Windows functions follow */

# if GTK_MAJOR_VERSION > 2 || GTK_MINOR_VERSION >= 14
static int alt_show (const char *uri)
{
    GError *err = NULL;
    int ret;

    ret = gtk_show_uri(NULL, uri, GDK_CURRENT_TIME, &err);

    if (err) {
	errbox(err->message);
	g_error_free(err);
    }

    return ret;
}
# endif

int browser_open (const char *url)
{
# if defined(OSX_BUILD)
    return osx_open_url(url);
# else
    gchar *urlcmd;
    int err;

#  if GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 14
    if (getenv("ALTSHOW") != NULL) {
	return alt_show(url);
    }
#  endif
    
    urlcmd = g_strdup_printf("%s -remote \"openURLNewWindow(%s)\"", Browser, url);
    err = gretl_spawn(urlcmd);
    g_free(urlcmd);

    if (err) {
	gretl_fork("Browser", url);
    }

    return 0;
# endif /* !OSX */
}

#include <signal.h>

/* Start an R session in asynchronous (interactive) mode.
   Note that there's a separate win32 function for this
   in gretlwin32.c.
*/

static void start_R_async (void)
{
    const char *supp1 = "--no-init-file";
    const char *supp2 = "--no-restore-data";
    char *s0 = NULL, *s1 = NULL, *s2 = NULL;
    pid_t pid;
    int n;

    s0 = mymalloc(64);
    s1 = mymalloc(32);
    s2 = mymalloc(32);

    if (s0 == NULL || s1 == NULL || s2 == NULL) {
	goto bailout;
    }

    *s0 = *s1 = *s2 = '\0';

    /* probably "xterm -e R" or similar */
    n = sscanf(Rcommand, "%63s %31s %31s", s0, s1, s2);

    if (n == 0) {
	errbox(_("No command was supplied to start R"));
	goto bailout;
    }

    signal(SIGCHLD, SIG_IGN); 
    pid = fork();

    if (pid == -1) {
	errbox(_("Couldn't fork"));
	perror("fork");
	return;
    } else if (pid == 0) {  
	if (n == 1) {
	    execlp(s0, s0, supp1, supp2, NULL);
	} else if (n == 2) {
	    execlp(s0, s0, s1, supp1, supp2, NULL);
	} else if (n == 3) {
	    execlp(s0, s0, s1, s2, supp1, supp2, NULL);
	}
	perror("execlp");
	_exit(EXIT_FAILURE);
    }

 bailout:

    free(s0); 
    free(s1); 
    free(s2);
}

/* run R or Ox in synchronous (batch) mode and display the results
   in a gretl window: non-Windows variant
*/

static void run_prog_sync (char **argv)
{
    gchar *sout = NULL;
    gchar *errout = NULL;
    gint status = 0;
    GError *gerr = NULL;
    PRN *prn = NULL;

    signal(SIGCHLD, SIG_DFL);

    g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		 NULL, NULL, &sout, &errout,
		 &status, &gerr);

    if (gerr != NULL) {
	errbox(gerr->message);
	g_error_free(gerr);
    } else if (status != 0) {
	if (errout != NULL) {
	    if (*errout == '\0') {
		errbox("%s exited with status %d", argv[0], status);
	    } else if (strlen(errout) < MAXLEN) {
		errbox(errout);
	    } else {
		bufopen(&prn);
		pputs(prn, errout);
	    }
	}
    } else if (sout != NULL) {
	bufopen(&prn);
	pputs(prn, sout);
    } else {
	warnbox("Got no output");
    }

    if (prn != NULL) {
	view_buffer(prn, 78, 350, _("gretl: script output"), PRINT, NULL);
    }

    g_free(sout);
    g_free(errout);
}

static void run_R_sync (void)
{
    gchar *argv[] = {
	"R",
	"--no-save",
	"--no-init-file",
	"--no-restore-data",
	"--slave",
	NULL
    };

    run_prog_sync(argv);
}

void run_ox_script (gchar *buf)
{
    const char *fname;
    int err;

    err = write_gretl_ox_file(buf, OPT_G, &fname);

    if (err) {
	gui_errmsg(err);
    } else {
	gchar *argv[3];

	argv[0] = (gchar *) gretl_oxl_path();
	argv[1] = (gchar *) fname;
	argv[2] = NULL;

	run_prog_sync(argv);
    }
}

void run_octave_script (gchar *buf)
{
    const char *fname;
    int err;

    err = write_gretl_octave_file(buf, OPT_G, &fname);

    if (err) {
	gui_errmsg(err);
    } else {
	gchar *argv[4];

	argv[0] = (gchar *) gretl_octave_path();
	argv[1] = (gchar *) "-q";
	argv[2] = (gchar *) fname;
	argv[3] = NULL;

	run_prog_sync(argv);
    }
}

#endif /* !G_OS_WIN32 */

/* driver for starting R, either interactive or in batch mode */

void start_R (const char *buf, int send_data, int interactive)
{
    gretlopt Ropt = OPT_G;
    int err;

    if (send_data && !data_status) {
	warnbox(_("Please open a data file first"));
	return;
    }

    if (interactive) {
	Ropt |= OPT_I;
    }

    if (send_data) {
	Ropt |= OPT_D;
    }

     err = write_gretl_R_files(buf, (const double **) Z,
			       datainfo, Ropt);

    if (err) {
	gui_errmsg(err);
	delete_gretl_R_files();
    } else if (interactive) {
#ifdef G_OS_WIN32
	win32_start_R_async();
#else
	start_R_async();
#endif
    } else {
	run_R_sync();
    }
}

void verbose_gerror_report (GError *gerr, const char *src)
{
    fprintf(stderr, "GError details from %s\n"
	    " message: '%s'\n domain = %d, code = %d\n",
	    src, gerr->message, gerr->domain, gerr->code);
}

int gretl_file_get_contents (const gchar *fname, gchar **contents, gsize *size)
{
    GError *gerr = NULL;
    gboolean ok;

    ok = g_file_get_contents(fname, contents, size, &gerr);

    if (gerr != NULL) {
	verbose_gerror_report(gerr, "g_file_get_contents");
	if (g_error_matches(gerr, G_FILE_ERROR, G_FILE_ERROR_INVAL)) {
	    gchar *trfname = NULL;
	    gsize bytes;

	    verbose_gerror_report(gerr, "g_file_get_contents");
	    g_error_free(gerr);
	    gerr = NULL;

	    if (!g_utf8_validate(fname, -1, NULL)) {
		fprintf(stderr, "Trying g_locale_to_utf8 on filename\n");
		trfname = g_locale_to_utf8(fname, -1, NULL, &bytes, &gerr);
		if (trfname == NULL) {
		    verbose_gerror_report(gerr, "g_locale_to_utf8");
		}
	    } else {
		fprintf(stderr, "Trying g_locale_from_utf8 on filename\n");
		trfname = g_locale_from_utf8(fname, -1, NULL, &bytes, &gerr);
		if (trfname == NULL) {
		    verbose_gerror_report(gerr, "g_locale_from_utf8");
		}
	    }

	    if (trfname != NULL) {
		ok = g_file_get_contents(trfname, contents, NULL, &gerr);
		g_free(trfname);
		if (!ok) {
		    verbose_gerror_report(gerr, "g_file_get_contents");
		}
	    }
	}
	if (gerr != NULL) {
	    errbox(gerr->message);
	    g_error_free(gerr);
	}
    }

    return !ok;
}

const char *print_today (void)
{
    static char timestr[16];
    struct tm *local;
    time_t t;

    t = time(NULL);
    local = localtime(&t);
    strftime(timestr, 15, "%Y-%m-%d", local);

    return timestr;
}
