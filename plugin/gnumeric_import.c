/*
 *  Copyright (c) by Allin Cottrell 2002
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

#include "libgretl.h"

#include <gtk/gtk.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#define UTF const xmlChar *

/* from gnumeric's value.h */
typedef enum {
    VALUE_EMPTY     = 10,
    VALUE_BOOLEAN   = 20, 
    VALUE_INTEGER   = 30,
    VALUE_FLOAT     = 40,
    VALUE_ERROR     = 50,
    VALUE_STRING    = 60,
    VALUE_CELLRANGE = 70,
    VALUE_ARRAY     = 80
} ValueType;

typedef struct {
    int nsheets;
    int selected;
    int startcol, startrow;
    char **sheetnames;
    GtkWidget *colspin, *rowspin;
} gbook;

typedef struct {
    int maxcol, maxrow;
    int text_cols, text_rows;
    int startcol, startrow;
    int ID;
    char *name;
    double **Z;
    char **varname;
    char **label;
} gsheet;

char *errbuf;

static void gsheet_init (gsheet *sheet)
{
    sheet->maxcol = sheet->maxrow = 0;
    sheet->text_cols = sheet->text_rows = 0;
    sheet->Z = NULL;
    sheet->varname = NULL;
    sheet->label = NULL;
}

static void gsheet_free (gsheet *sheet)
{
    int i;

    for (i=0; i<=sheet->maxcol; i++) {
	if (sheet->varname) 
	    free(sheet->varname[i]);
	if (sheet->Z)
	    free(sheet->Z[i]);
    }

    if (sheet->label) { 
	for (i=0; i<=sheet->maxrow; i++) 
	    free(sheet->label[i]);
	free(sheet->label);
    }

    free(sheet->varname);
    free(sheet->Z);

    gsheet_init(sheet);

    free(sheet->name);
    sheet->name = NULL;
}

static void gsheet_print_info (gsheet *sheet)
{
    int i;
#ifdef notdef 
    int t;
#endif

    fprintf(stderr, "maxcol = %d\n", sheet->maxcol);
    fprintf(stderr, "maxrow = %d\n", sheet->maxrow);
    fprintf(stderr, "text_cols = %d\n", sheet->text_cols);
    fprintf(stderr, "text rows = %d\n", sheet->text_rows);
    fprintf(stderr, "startcol = %d\n", sheet->startcol);
    fprintf(stderr, "startrow = %d\n", sheet->startrow);

    for (i=sheet->text_cols; i<=sheet->maxcol; i++) 
	fprintf(stderr, "%s%s", sheet->varname[i],
		    (i == sheet->maxcol)? "\n" : " ");	

#ifdef notdef
    for (t=sheet->text_rows; t<=sheet->maxrow; t++) {
	if (sheet->text_cols)
	    fprintf(stderr, "%s ", sheet->label[t]);
	for (i=sheet->text_cols; i<=sheet->maxcol; i++) {
	    fprintf(stderr, "%g%s", sheet->Z[i][t],
		    (i == sheet->maxcol)? "\n" : " ");
	}
    }
#endif
}

#define VTYPE_IS_NUMERIC(v) (v) == VALUE_BOOLEAN || \
                            (v) == VALUE_INTEGER || \
                            (v) == VALUE_FLOAT


static int gsheet_allocate (gsheet *sheet, int cols, int rows)
{
    int i, t;

    sheet->Z = malloc(cols * sizeof *(sheet->Z));
    if (sheet->Z == NULL) return 1;
    for (i=0; i<cols; i++) {
	sheet->Z[i] = malloc(rows * sizeof **(sheet->Z));
	if (sheet->Z[i] == NULL) return 1;
	for (t=0; t<rows; t++)
	    sheet->Z[i][t] = -999.0;
    }

    sheet->varname = malloc(cols * sizeof *(sheet->varname));
    for (i=0; i<cols; i++) {
	sheet->varname[i] = malloc(9 * sizeof **(sheet->varname));
	if (sheet->varname[i] == NULL) return 1;
	sheet->varname[i][0] = '\0';
    }

    sheet->label = malloc(rows * sizeof *(sheet->label));
    for (t=0; t<rows; t++) {
	sheet->label[t] = malloc(9 * sizeof **(sheet->label));
	if (sheet->label[t] == NULL) return 1;
	sheet->label[t][0] = '\0';
    }

    return 0;
}

int gsheet_parse_cells (xmlNodePtr node, gsheet *sheet)
{
    xmlNodePtr p = node->xmlChildrenNode;
    char *tmp;
    double x;
    int i, t, vtype = 0;
    char *toprows, *leftcols;
    int cols, rows;
    int colmin, rowmin;
    int err = 0;

    cols = sheet->maxcol + 2 - sheet->startcol;
    rows = sheet->maxrow + 2 - sheet->startrow;

    if (gsheet_allocate(sheet, cols, rows)) return 1;

    leftcols = calloc(cols, 1);
    toprows = calloc(rows, 1);

    if (toprows == NULL || leftcols == NULL) {
	gsheet_free(sheet);
	return 1;
    }

    colmin = sheet->startcol - 1;
    rowmin = sheet->startrow - 1;

    while (p && !err) {
	if (!xmlStrcmp(p->name, (UTF) "Cell")) {
	    int i_real = 0, t_real = 0;

	    x = -999.0;
	    i = 0; t = 0;

	    tmp = xmlGetProp(p, (UTF) "Col");
	    if (tmp) {
		i = atoi(tmp);
		i_real = i - colmin;
		free(tmp);
	    }
	    tmp = xmlGetProp(p, (UTF) "Row");
	    if (tmp) {
		t = atoi(tmp);
		t_real = t - rowmin;
		free(tmp);
	    }
	    if (i_real >= 0 && t_real >= 0) {
		tmp = xmlGetProp(p, (UTF) "ValueType");
		if (tmp) {
		    vtype = atoi(tmp);
		    free(tmp);
		}
		/* check the top-left cell */
		if (i_real == 0 && t_real == 0) {
		    if (VTYPE_IS_NUMERIC(vtype)) {
			sprintf(errbuf, "Expected to find a variable name");
			err = 1;
		    }
		}
		else if (i_real >= 1 && t_real == 0 && 
			 !(vtype == VALUE_STRING)) {
		    /* ought to be a varname here */
		    sprintf(errbuf, "Expected to find a variable name");
		    err = 1;
		}
		if (!err && (tmp = xmlNodeGetContent(p))) {
		    if (VTYPE_IS_NUMERIC(vtype) ||vtype == VALUE_STRING) {
			if (i_real == 0) 
			    strncat(sheet->label[t_real], tmp, 8);
		    }
		    if (VTYPE_IS_NUMERIC(vtype)) {
			x = atof(tmp);
			sheet->Z[i_real][t_real] = x;
			toprows[t_real] = leftcols[i_real] = 0;
		    }
		    else if (vtype == VALUE_STRING) {
			if (t_real == 0)
			    strncat(sheet->varname[i_real], tmp, 8);
			toprows[t_real] = leftcols[i_real] = 1;
		    }
		    free(tmp);
		}
	    }
	}
	p = p->next;
    }

    for (i=0; i<cols; i++)
	if (leftcols[i]) sheet->text_cols += 1;
    for (t=0; t<rows; t++)
	if (toprows[t]) sheet->text_rows += 1;

    if (sheet->text_rows > 1) {
	sprintf(errbuf, "Found an extraneous row of text");
	err = 1;
    }
    if (sheet->text_cols > 1) {
	sprintf(errbuf, "Found an extraneous column of text");
	err = 1;
    }

    free(toprows);
    free(leftcols);

    return err;
}

static int gsheet_get_data (const char *fname, gsheet *sheet) 
{
    xmlDocPtr doc;
    xmlNodePtr cur, sub;
    char *tmp = NULL;
    int err = 0, got_sheet = 0;

    LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);

    doc = xmlParseFile(fname);
    if (doc == NULL) {
	sprintf(errbuf, "xmlParseFile failed on %s", fname);
	return 1;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        sprintf(errbuf, "%s: empty document", fname);
	xmlFreeDoc(doc);
	return 1;
    }

    if (xmlStrcmp(cur->name, (UTF) "Workbook")) {
        sprintf(errbuf, "File of the wrong type, root node not Workbook");
	xmlFreeDoc(doc);
	return 1;
    }

    gsheet_init(sheet);

    /* Now walk the tree */
    cur = cur->xmlChildrenNode;
    while (cur != NULL && !got_sheet) {
	if (!xmlStrcmp(cur->name, (UTF) "Sheets")) {
	    int sheetcount = 0;

	    sub = cur->xmlChildrenNode;
	    while (sub != NULL && !got_sheet) {
		if (!xmlStrcmp(sub->name, (UTF) "Sheet")) {
		    xmlNodePtr snode = sub->xmlChildrenNode;

		    while (snode != NULL) {
			if (!xmlStrcmp(snode->name, (UTF) "Name")) {
			    sheetcount++;
			    tmp = xmlNodeGetContent(snode);
			    if (tmp) {
				if (!strcmp(tmp, sheet->name) &&
				    sheetcount == sheet->ID + 1) {
				    got_sheet = 1;
				}
				free(tmp);
			    }
			}
			else if (got_sheet &&
				 !xmlStrcmp(snode->name, (UTF) "MaxCol")) {
			    tmp = xmlNodeGetContent(snode);
			    if (tmp) {
				sheet->maxcol = atoi(tmp);
				free(tmp);
			    }
			}
			else if (got_sheet &&
				 !xmlStrcmp(snode->name, (UTF) "MaxRow")) {
			    tmp = xmlNodeGetContent(snode);
			    if (tmp) {
				sheet->maxrow = atoi(tmp);
				free(tmp);
			    }
			}
			else if (got_sheet &&
				 !xmlStrcmp(snode->name, (UTF) "Cells")) {
			    /* error handling needed */
			    gsheet_parse_cells(snode, sheet);
			}
			snode = snode->next;
		    }
		}
		sub = sub->next;
	    }
	}
	cur = cur->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    if (!got_sheet) err = 1;

    return err;
}

static int gbook_record_name (char *name, gbook *book)
{
    book->nsheets += 1;
    book->sheetnames = realloc(book->sheetnames, 
			       book->nsheets * sizeof (char *));
    if (book->sheetnames == NULL) 
	return 1;
    book->sheetnames[book->nsheets - 1] = name;
    return 0;
}

static void gbook_init (gbook *book)
{
    book->nsheets = 0;
    book->startcol = book->startrow = 1;
    book->sheetnames = NULL;
}

static void gbook_free (gbook *book)
{
    int i;

    for (i=0; i<book->nsheets; i++)
	free(book->sheetnames[i]);
    free(book->sheetnames);    
}

static void gbook_print_info (gbook *book) 
{
    int i;

    fprintf(stderr, "Found %d sheet%s\n", book->nsheets,
	    (book->nsheets > 1)? "s" : "");
    
    for (i=0; i<book->nsheets; i++)
	fprintf(stderr, "%d: '%s'\n", i, book->sheetnames[i]);
}

static int gbook_get_info (const char *fname, gbook *book) 
{
    xmlDocPtr doc;
    xmlNodePtr cur, sub;
    char *tmp = NULL;
    int got_index = 0, err = 0;

    LIBXML_TEST_VERSION 
	xmlKeepBlanksDefault(0);

    gbook_init(book);

    doc = xmlParseFile(fname);
    if (doc == NULL) {
	sprintf(errbuf, "xmlParseFile failed on %s", fname);
	return 1;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        sprintf(errbuf, "%s: empty document", fname);
	xmlFreeDoc(doc);
	return 1;
    }

    if (xmlStrcmp(cur->name, (UTF) "Workbook")) {
        sprintf(errbuf, "File of the wrong type, root node not Workbook");
	xmlFreeDoc(doc);
	return 1;
    }

    /* Now walk the tree */
    cur = cur->xmlChildrenNode;
    while (cur != NULL && !got_index && !err) {
        if (!xmlStrcmp(cur->name, (UTF) "SheetNameIndex")) {
	    got_index = 1;
	    sub = cur->xmlChildrenNode;
	    while (sub != NULL && !err) {
		if (!xmlStrcmp(sub->name, (UTF) "SheetName")) {
		    tmp = xmlNodeGetContent(sub);
		    if (tmp) {
			if (gbook_record_name(tmp, book)) {
			    err = 1;
			    free(tmp);
			}
		    }
		}
		sub = sub->next;
	    }
        }
	cur = cur->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return err;
}

static 
int gsheet_setup (gsheet *sheet, gbook *book, int n)
{
    size_t len = strlen(book->sheetnames[n]) + 1;
    
    sheet->name = malloc(len);
    if (sheet->name == NULL) return 1;

    sheet->ID = n;
    strcpy(sheet->name, book->sheetnames[n]);

    sheet->startcol = book->startcol;
    sheet->startrow = book->startrow;    
    
    return 0;
}

static
void gsheet_menu_select_row (GtkCList *clist, gint row, gint column, 
			     GdkEventButton *event, gbook *book) 
{
    book->selected = row;
}

static 
void gsheet_menu_make_list (GtkWidget *widget, gbook *book)
{
    gchar *row[1];
    int i;

    gtk_clist_clear(GTK_CLIST(widget));
    for (i=0; i<book->nsheets; i++) {
	row[0] = book->sheetnames[i];
        gtk_clist_append(GTK_CLIST (widget), row);
    }

    gtk_clist_select_row(GTK_CLIST(widget), 0, 0);  
}

static 
void gsheet_menu_cancel (GtkWidget *w, gbook *book)
{
    book->selected = -1;
}

static 
void gsheet_menu_quit (GtkWidget *w, gbook *book)
{
    gtk_main_quit();
}

static 
void gbook_get_startcol (GtkWidget *w, gbook *book)
{
    book->startcol = gtk_spin_button_get_value_as_int
	(GTK_SPIN_BUTTON(book->colspin));
}

static 
void gbook_get_startrow (GtkWidget *w, gbook *book)
{
    book->startrow = gtk_spin_button_get_value_as_int
	(GTK_SPIN_BUTTON(book->rowspin));
}

static void gsheet_menu (gbook *book, int multisheet)
{
    GtkWidget *w, *tmp, *frame;
    GtkWidget *vbox, *hbox, *list;
    GtkObject *adj;

    w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w), "gretl: gnumeric import");
    gtk_signal_connect(GTK_OBJECT(w), "destroy",  
		       GTK_SIGNAL_FUNC(gsheet_menu_quit), NULL);

    vbox = gtk_vbox_new (FALSE, 5);
    gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

    /* choose starting column and row */
    frame = gtk_frame_new("Start import at");
    gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 5);

    hbox = gtk_hbox_new (FALSE, 5);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
    gtk_container_add(GTK_CONTAINER (frame), hbox);

    tmp = gtk_label_new("column:");
    adj = gtk_adjustment_new(1, 1, 5, 1, 1, 1);
    book->colspin = gtk_spin_button_new (GTK_ADJUSTMENT(adj), 1, 0);
    gtk_signal_connect (adj, "value_changed",
			GTK_SIGNAL_FUNC (gbook_get_startcol), book);
    gtk_box_pack_start (GTK_BOX (hbox), tmp, FALSE, FALSE, 5);
    gtk_box_pack_start (GTK_BOX (hbox), book->colspin, FALSE, FALSE, 5);

    tmp = gtk_label_new("row:");
    adj = gtk_adjustment_new(1, 1, 5, 1, 1, 1);
    book->rowspin = gtk_spin_button_new (GTK_ADJUSTMENT(adj), 1, 0);
    gtk_signal_connect (adj, "value_changed",
			GTK_SIGNAL_FUNC (gbook_get_startrow), book);
    gtk_box_pack_start (GTK_BOX (hbox), tmp, FALSE, FALSE, 5);
    gtk_box_pack_start (GTK_BOX (hbox), book->rowspin, FALSE, FALSE, 5);

    /* choose the worksheet (if applicable) */
    if (multisheet) {
	frame = gtk_frame_new("Sheet to import");
	gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 5);
	list = gtk_clist_new(1);
	gtk_signal_connect (GTK_OBJECT (list), "select_row", 
			    GTK_SIGNAL_FUNC (gsheet_menu_select_row), 
			    book);
	gsheet_menu_make_list(list, book);
	gtk_container_set_border_width (GTK_CONTAINER (list), 10);
	gtk_container_add(GTK_CONTAINER(frame), list);
    }

    hbox = gtk_hbox_new (TRUE, 5);
    tmp = gtk_button_new_with_label("OK");
    GTK_WIDGET_SET_FLAGS (tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (hbox), 
                        tmp, TRUE, TRUE, FALSE);
    gtk_signal_connect_object (GTK_OBJECT (tmp), "clicked", 
                               GTK_SIGNAL_FUNC (gtk_widget_destroy), 
                               GTK_OBJECT (w));
    gtk_widget_grab_default (tmp);

    tmp = gtk_button_new_with_label("Cancel");
    GTK_WIDGET_SET_FLAGS (tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (hbox), 
                        tmp, TRUE, TRUE, FALSE);
    gtk_signal_connect (GTK_OBJECT (tmp), "clicked", 
			GTK_SIGNAL_FUNC (gsheet_menu_cancel), book);
    gtk_signal_connect_object (GTK_OBJECT (tmp), "clicked", 
                               GTK_SIGNAL_FUNC (gtk_widget_destroy), 
                               GTK_OBJECT (w));

    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(w), vbox);
    
    gtk_widget_show_all(w);
    gtk_main();
}

static int gsheet_labels_complete (gsheet *sheet)
{
    int t, rows = sheet->maxrow + 2 - sheet->startrow;
    
    for (t=1; t<rows; t++) {
	if (sheet->label[t][0] == '\0')
	    return 0;
    }
    return 1;
}

static int label_is_date (char *str)
{
    size_t len = strlen(str);
    int i, d, pd = 0;
    double dd, sub;

    for (i=0; i<len; i++)
	if (str[i] == ':') str[i] = '.';
     
    if (len == 4 && sscanf(str, "%4d", &d) == 1 &&
	d > 0 && d < 3000) {
	pd = 1;
    }
    else if (len == 6 && sscanf(str, "%lf", &dd) == 1 &&
	dd > 0 && dd < 3000) { 
	sub = 10.0 * (dd - (int) dd);
	if (sub >= .999 && sub <= 4.001) pd = 4;
    }
    else if (len == 7 && sscanf(str, "%lf", &dd) == 1 &&
	dd > 0 && dd < 3000) {
	sub = 100.0 * (dd - (int) dd);
	if (sub >= .9999 && sub <= 12.0001) pd = 12;
    }
    return pd;
}

static int consistent_date_labels (gsheet *sheet)
{
    int t, rows = sheet->maxrow + 2 - sheet->startrow;
    int pd = 0, pdbak = 0;
    double x, xbak = 0.0;
    
    for (t=1; t<rows; t++) {
	if (sheet->label[t][0] == '\0') return 0;
	pd = label_is_date(sheet->label[t]);
	if (pd == 0) return 0;
	x = atof(sheet->label[t]);
	if (t == 1) pdbak = pd;
	else { /* t > 1 */
	    if (pd != pdbak) return 0;
	    if (x <= xbak) return 0;
	}
	xbak = x;
    }
    return pd;
}

static int obs_column (gsheet *sheet)
{
    if (sheet->label[0][0] == '\0') return 1;

    lower(sheet->label[0]);
    if (strcmp(sheet->label[0], "obs") == 0 ||
	strcmp(sheet->label[0], "date") == 0 ||
	strcmp(sheet->label[0], "year") == 0)
	return 1;

    return 0;
}

int gbook_get_data (const char *fname, double ***pZ, DATAINFO *pdinfo,
		    char *errtext)
{
    gbook book;
    gsheet sheet;
    int err = 0, sheetnum = -1;

    errbuf = errtext;
    errbuf[0] = '\0';

    if (gbook_get_info(fname, &book)) {
	sprintf(errbuf, "Failed to get workbook info");
	err = 1;
    } else
	gbook_print_info(&book);

    if (book.nsheets == 0) {
	sprintf(errbuf, "No sheets found");
    }
    else if (book.nsheets > 1) {
	gsheet_menu(&book, 1);
	sheetnum = book.selected;
    }
    else {
	gsheet_menu(&book, 0);
	sheetnum = 0;
    }

    if (sheetnum >= 0) {
	sprintf(errbuf, "Getting data");
	if (gsheet_setup(&sheet, &book, sheetnum)) {
	    sprintf(errbuf, "error in gsheet_setup()");
	    err = 1;
	} else {
	    err = gsheet_get_data(fname, &sheet);
	    if (!err) 
		gsheet_print_info(&sheet);
	}
    }

    gbook_free(&book);

    if (!err) {
	int i, t, i_sheet, label_strings = sheet.text_cols;
	int time_series = 0;

	if (sheet.text_cols == 0 && obs_column(&sheet)) {
	    int pd = consistent_date_labels(&sheet);

	    if (pd) pdinfo->pd = pd;
	    pdinfo->sd0 = atof(sheet.label[1]);
	    strcpy(pdinfo->stobs, sheet.label[1]);
	    pdinfo->time_series = TIME_SERIES;
	    sheet.text_cols = 1;
	    time_series = 1;
	}

	pdinfo->v = sheet.maxcol + 3 - sheet.startcol - sheet.text_cols;
	pdinfo->n = sheet.maxrow + 1 - sheet.startrow;
	fprintf(stderr, "pdinfo->v = %d, pdinfo->n = %d\n",
		pdinfo->v, pdinfo->n);

	start_new_Z(pZ, pdinfo, 0);

	if (!time_series) {
	    strcpy(pdinfo->stobs, "1");
	    sprintf(pdinfo->endobs, "%d", pdinfo->n);
	    pdinfo->sd0 = 1.0;
	    pdinfo->pd = 1;
	    pdinfo->time_series = 0;
	} else {
	    ntodate(pdinfo->endobs, pdinfo->n - 1, pdinfo);
	}
	pdinfo->extra = 0; 

	for (i=1; i<pdinfo->v; i++) {
	    i_sheet = i - 1 + sheet.text_cols;
	    strcpy(pdinfo->varname[i], sheet.varname[i_sheet]);
	    for (t=0; t<pdinfo->n; t++) {
		(*pZ)[i][t] = sheet.Z[i_sheet][t+1];
	    }
	}

	if (label_strings && gsheet_labels_complete(&sheet)) {
	    char **S = NULL;

	    pdinfo->markers = 1;
	    if (allocate_case_markers(&S, pdinfo->n) == 0) {
		pdinfo->markers = 1;
		for (t=0; t<pdinfo->n; t++)
		    strcpy(S[t], sheet.label[t+1]);
		pdinfo->S = S;
	    }
	}
    } 	    

    gsheet_free(&sheet);

    return err;

}
