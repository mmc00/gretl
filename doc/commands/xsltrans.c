/*
 *  Copyright (c) 2004 by Allin Cottrell
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

/* Formatter for gretl commands info stored as "generic" XML: takes
   a purely content-based XML representation of the info relating to
   the gretl commands (conforming to gretl_commands.dtd) and uses
   XSL transformation to turn this into output suitable for:

   * the "cmdref" chapter of the gretl manual (docbook XML); and 
   * the "online" help files.
   
   Uses the XSL stylesheets gretlman.xsl and gretltxt.xsl.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

#define ROOTNODE "commandlist"
#define UTF const xmlChar *

int apply_xslt (xmlDocPtr doc)
{
    xsltStylesheetPtr style;
    xmlDocPtr result;
    const char **null_params = NULL;
    const char *gui_params[] = {
	"hlp", "\"gui\"",
	NULL
    };
    FILE *fp;
    int err = 0;

    xmlIndentTreeOutput = 1;

    /* make XML output */
    style = xsltParseStylesheetFile("gretlman.xsl");
    if (style == NULL) {
	err = 1;
    } else {
	result = xsltApplyStylesheet(style, doc, null_params);
	if (result == NULL) {
	    err = 1;
	} else {
	    fp = fopen("cmdlist.xml", "w");
	    if (fp == NULL) {
		err = 1;
	    } else {
		xsltSaveResultToFile(fp, result, style);
		fclose(fp);
	    }
	    xsltFreeStylesheet(style);
	    xmlFreeDoc(result);
	}	    
    }

    /* make plain text output */
    style = xsltParseStylesheetFile("gretltxt.xsl");
    if (style == NULL) {
	err = 1;
    } else {
	/* cli version */
	result = xsltApplyStylesheet(style, doc, null_params);
	if (result == NULL) {
	    err = 1;
	} else {
	    fp = fopen("cmdlist.txt", "w");
	    if (fp == NULL) {
		err = 1;
	    } else {
		xsltSaveResultToFile(fp, result, style);
		fclose(fp);
	    }
	    xmlFreeDoc(result);
	}
	/* gui version */
	result = xsltApplyStylesheet(style, doc, gui_params);
	if (result == NULL) {
	    err = 1;
	} else {
	    fp = fopen("guilist.txt", "w");
	    if (fp == NULL) {
		err = 1;
	    } else {
		xsltSaveResultToFile(fp, result, style);
		fclose(fp);
	    }
	    xmlFreeDoc(result);
	}
	xsltFreeStylesheet(style);
    }

    return err;
}

int parse_commands_data (const char *fname) 
{
    xmlDocPtr doc;
    xmlNodePtr cur;
    int err = 0;

    LIBXML_TEST_VERSION 
	xmlKeepBlanksDefault(0);

    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;

    doc = xmlParseFile(fname);
    if (doc == NULL) {
	err = 1;
	goto bailout;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
	err = 1;
	goto bailout;
    }

    if (xmlStrcmp(cur->name, (UTF) ROOTNODE)) {
	fprintf(stderr, "File of the wrong type, root node not %s\n", ROOTNODE);
	err = 1;
	goto bailout;
    }

    apply_xslt(doc);

 bailout:

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return err;
}

int main (int argc, char **argv)
{
    const char *fname;
    int err;

    if (argc != 2) {
	fputs("Please give one parameter: the name of an XML file "
	      "to parse\n", stderr);
	exit(EXIT_FAILURE);
    }

    fname = argv[1];

    err = parse_commands_data(fname);

    return err;
}
