#ifndef SETTINGS_H
#define SETTINGS_H

void set_rcfile (void);

void write_rc (void);

void mkfilelist (int filetype, const char *newfile);

void delete_from_filelist (int filetype, const char *fname);

void add_files_to_menu (int filetype);

void options_dialog (gpointer data);

void font_selector (void);

void load_fixed_font (void);

void gnuplot_color_selector (GtkWidget *w, gpointer p);

GtkWidget *color_patch_button (int colnum);

char *endbit (char *dest, char *src, int addscore);

void get_default_dir (char *s);

void filesel_set_path_callback (const char *setting, char *strvar);

void set_datapage (const char *str);
void set_scriptpage (const char *str);
const char *get_datapage (void);
const char *get_scriptpage (void);

#ifdef HAVE_TRAMO
void set_tramo_ok (int set);
#endif

#ifdef HAVE_X12A
void set_x12a_ok (int set);
#endif

void first_time_set_user_dir (void);

#endif /* SETTINGS_H */
