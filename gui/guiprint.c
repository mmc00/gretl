/*
 *  Copyright (c) by Ramu Ramanathan and Allin Cottrell
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
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 *
 */

/*  guiprint.c - RTF and LaTeX generation for gretl, plus native
    printing */ 

#include "gretl.h"
#include "selector.h"
#include "textutil.h"

#if defined(USE_GNOME)

#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-printer-dialog.h>

#define GRETL_PBM_TMP           "gretltmp.pbm"

static GdkPixbuf *png_mono_pixbuf (const char *fname);

gchar *user_string (void)
{
    uid_t uid = getuid();
    struct passwd *pwd;
    const gchar *username, *realname;
    gchar *ret = NULL;

    pwd = getpwuid(uid);

    username = pwd->pw_name;
    realname = pwd->pw_gecos;

    if (realname != NULL && *realname != '\0') {
	ret = g_strdup_printf("%s %s", _("for"), realname);
    } else if (username != NULL && *username != '\0') {
	ret = g_strdup_printf("%s %s", _("for"), username);
    }

    return ret;
}

static char *header_string (void)
{
    gchar *hdr, *ustr;
    time_t prntime = time(NULL);

    ustr = user_string();
    if (ustr != NULL) {
	hdr = g_strdup_printf("%s %s %s", _("gretl output"), ustr,
			      print_time(&prntime));
	g_free(ustr);
    } else {
	hdr = g_strdup_printf("%s %s", _("gretl output"), print_time(&prntime));
    }

    return hdr;
}

void winprint (char *fullbuf, char *selbuf)
{
    GnomePrinter *printer;
    GnomePrintContext *pc;    
    GnomeFont *font;
    gchar *hdrstart;
    char *p, linebuf[90], hdr[90];
    int page_lines = 47;
    int x, y, line, page;
    size_t len;

    char paper[12];

    if (doing_nls()) {
	strcpy(paper, "A4");
    } else {
	strcpy(paper, "US-Letter");
    }

    printer = gnome_printer_dialog_new_modal();

    if (!printer) {
	free(fullbuf);
	free(selbuf);
	return;
    }

    pc = gnome_print_context_new_with_paper_size(printer, paper);

    gnome_print_beginpage (pc, _("gretl output"));

    /* could use GNOME_FONT_MEDIUM below */
    /* font = gnome_font_new_closest("Courier", GNOME_FONT_BOOK, FALSE, 10); */
    font = gnome_font_new("Courier", 10.0);
    gnome_print_setfont(pc, font);
    gnome_print_setrgbcolor(pc, 0, 0, 0);

    if (selbuf != NULL) p = selbuf;
    else p = fullbuf;
    page = 1;
    x = 72;
    hdrstart = header_string();
    while (*p) { /* pages loop */
	line = 0;
	y = 756;
	if (page > 1) 
	    gnome_print_beginpage (pc, _("gretl output"));
	sprintf(hdr, _("%s, page %d"), hdrstart, page++);
	gnome_print_moveto(pc, x, y);
	gnome_print_show(pc, hdr);
	y = 720;
	while (*p && line < page_lines) { /* lines loop */
	    len = strcspn(p, "\n");
	    *linebuf = '\0';
	    strncat(linebuf, p, len);
	    gnome_print_moveto(pc, x, y);
	    gnome_print_show(pc, linebuf);
	    p += len + 1;
	    y -= 14; /* line spacing */
	    line++;
	}
	gnome_print_showpage(pc);
    }

    g_free(hdrstart);

    /* clean up */
    gnome_print_context_close(pc);
    gtk_object_unref(GTK_OBJECT(font));
    gtk_object_unref(GTK_OBJECT(printer));
    free(fullbuf);
    if (selbuf) 
	free(selbuf);
}

void gnome_print_graph (const char *fname)
{
    GnomePrinter *printer;
    GnomePrintContext *pc; 
    GdkPixbuf *pbuf;
    int image_left_x = 530, image_bottom_y = 50;
    int width, height;

    printer = gnome_printer_dialog_new_modal();
    if (!printer) return;

    pbuf = png_mono_pixbuf(fname); 
    if (pbuf == NULL) {
	errbox(_("Failed to generate graph"));
	gtk_object_unref(GTK_OBJECT(printer));
	return;
    }   

    width = gdk_pixbuf_get_width(pbuf);
    height = gdk_pixbuf_get_height(pbuf);

    pc = gnome_print_context_new_with_paper_size(printer, "US-Letter");

    gnome_print_beginpage(pc, _("gretl output"));
    gnome_print_gsave(pc);
    gnome_print_translate(pc, image_left_x, image_bottom_y);
    gnome_print_rotate(pc, 90);
    gnome_print_scale(pc, width, height);
    gnome_print_pixbuf(pc, pbuf);
    gnome_print_grestore(pc);
    gnome_print_showpage(pc);

    /* clean up */
    gnome_print_context_close(pc);
    gtk_object_unref(GTK_OBJECT(printer));
}

#endif /* USE_GNOME */

#include "guiprint_common.c"

