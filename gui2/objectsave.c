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

/* objectsave.c for gretl: save models estimated via commands */

#include "gretl.h"
#include "session.h"
#include "gpt_control.h"
#include "objectsave.h"

#include "cmd_private.h"
#include "var.h"
#include "varprint.h"
#include "objstack.h"

enum {
    OBJ_ACTION_NONE,
    OBJ_ACTION_INVALID,
    OBJ_ACTION_NULL,
    OBJ_ACTION_MODEL_SHOW,
    OBJ_ACTION_MODEL_FREE,
    OBJ_ACTION_MODEL_STAT,
    OBJ_ACTION_VAR_SHOW,
    OBJ_ACTION_VAR_IRF,
    OBJ_ACTION_VAR_FREE,
    OBJ_ACTION_GRAPH_SHOW,
    OBJ_ACTION_GRAPH_FREE,
    OBJ_ACTION_TEXT_SHOW,
    OBJ_ACTION_TEXT_FREE,
    OBJ_ACTION_SYS_SHOW,
    OBJ_ACTION_SYS_FREE
};

#define obj_action_free(a) (a == OBJ_ACTION_MODEL_FREE || \
                            a == OBJ_ACTION_VAR_FREE || \
                            a == OBJ_ACTION_SYS_FREE)   

static int match_object_command (const char *s, char sort)
{
    if (sort == OBJ_MODEL) {
	if (*s == 0) return OBJ_ACTION_MODEL_SHOW; /* default */
	if (strcmp(s, "show") == 0) return OBJ_ACTION_MODEL_SHOW;
	if (strcmp(s, "free") == 0) return OBJ_ACTION_MODEL_FREE; 
	return OBJ_ACTION_MODEL_STAT;
    }

    if (sort == OBJ_VAR) {
	if (*s == 0) return OBJ_ACTION_VAR_SHOW; /* default */
	if (strcmp(s, "show") == 0) return OBJ_ACTION_VAR_SHOW;
	if (strcmp(s, "irf") == 0)  return OBJ_ACTION_VAR_IRF;
	if (strcmp(s, "free") == 0) return OBJ_ACTION_VAR_FREE; 
    }

    if (sort == OBJ_GRAPH) {
	if (*s == 0) return OBJ_ACTION_GRAPH_SHOW; /* default */
	if (strcmp(s, "show") == 0) return OBJ_ACTION_GRAPH_SHOW;
	if (strcmp(s, "free") == 0) return OBJ_ACTION_GRAPH_FREE; 
    } 

    if (sort == OBJ_TEXT) {
	if (*s == 0) return OBJ_ACTION_TEXT_SHOW; /* default */
	if (strcmp(s, "show") == 0) return OBJ_ACTION_TEXT_SHOW;
	if (strcmp(s, "free") == 0) return OBJ_ACTION_TEXT_FREE; 
    }  

    if (sort == OBJ_SYS) {
	if (*s == 0) return OBJ_ACTION_SYS_SHOW; /* default */
	if (strcmp(s, "show") == 0) return OBJ_ACTION_SYS_SHOW;
	if (strcmp(s, "free") == 0) return OBJ_ACTION_SYS_FREE; 
    }  

    return OBJ_ACTION_INVALID;
}

static void print_model_stat (const char *modname, const char *param, PRN *prn)
{
    MODEL *pmod = get_model_by_name(modname);
    int idx = gretl_model_stat_index(param);
    double x;
    int err = 0;

    if (pmod == NULL || idx <= 0) return;

    x = gretl_model_get_scalar(pmod, idx, &err);
    if (err) {
	pprintf(prn, _("%s: no data for '%s'\n"), pmod->name, param);
    } else {
	pprintf(prn, "%s: %s = %.8g\n", pmod->name, param + 1);
    }
}

static void get_word_and_command (const char *s, char *word, 
				  char *cmd)
{
    int start = 0, len, d;
    int quoted = 0;
    const char *p;

    *word = 0;
    *cmd = 0;

    /* skip any leading whitespace */
    while (*s && isspace(*s)) {
	s++; start++;
    }

    /* skip an opening quote */
    if (*s == '"') {
	s++;
	quoted = 1;
    }

    p = s;

    /* crawl to end of (possibly quoted) "word" */
    len = 0;
    while (*s) {
	if (*s == '"') quoted = 0;
	if (!quoted && isspace(*s)) break;
	s++; len++;
    }

#if 0
    fprintf(stderr, "remaining s ='%s', len=%d\n", s, len);
#endif

    /* is an object command embedded? */
    d = dotpos(p);
    if (d < s - p) {
	strncat(cmd, p + d + 1, len - d - 1);
	len -= (len - d);
    }

    if (len == 0) {
	return;
    }

    if (len > MAXSAVENAME - 1) {
	len = MAXSAVENAME - 1;
    }

    strncat(word, p, len);

    if (word[len - 1] == '"') {
	word[len - 1] = 0;
    }

#if 0
    fprintf(stderr, "word='%s', cmd='%s'\n", word, cmd);
#endif
}

static int parse_object_request (const char *line, 
				 char *objname, char *param,
				 void **pptr, PRN *prn)
{
    char word[MAXSAVENAME] = {0};
    int sort = 0;
    int action;

    /* get object name (if any) and dot param */
    get_word_and_command(line, word, param);

    /* if no dot param, nothing doing */
    if (*param == 0) return OBJ_ACTION_NONE;

    /* see if there's an object associated with the name */
    *pptr = get_session_object_by_name(word, &sort);

    if (*pptr == NULL) {
	/* no matching object */
	if (*param) {
	    pprintf(prn, _("%s: no such object\n"), word);
	}
	return OBJ_ACTION_NULL;
    }

    action = match_object_command(param, sort);

    if (action == OBJ_ACTION_INVALID) {
	pprintf(prn, _("command '%s' not recognized"), param);
	pputc(prn, '\n');
    } else {
	strcpy(objname, word);
    } 

    return action;
}

/* public interface below */

int maybe_save_model (const CMD *cmd, MODEL **ppmod, PRN *prn)
{
    const char *savename = gretl_cmd_get_savename(cmd);
    int err;

    set_last_model(*ppmod, EQUATION);

    if (*savename == 0) {
	return 0;
    }

    err = stack_model_as(*ppmod, savename);

    if (!err) {
	err = try_add_model_to_session(*ppmod);
    }

    if (!err) {
	MODEL *mnew = gretl_model_new();

	if (mnew != NULL) {
	    copy_model(mnew, *ppmod);
	    *ppmod = mnew;
	    pprintf(prn, _("%s saved\n"), savename);
	} else {
	    err = E_ALLOC;
	}
    }

    return err;
}

int maybe_save_var (const CMD *cmd, GRETL_VAR **pvar, PRN *prn)
{
    const char *savename = gretl_cmd_get_savename(cmd);
    GRETL_VAR *var;
    int err = 0;

    set_last_model(*pvar, VAR);

    if (*savename == 0) {
	*pvar = NULL;
	return 0;
    }

    var = *pvar;
    *pvar = NULL;

    err = stack_VAR_as(var, savename);

    if (!err) {
	err = try_add_var_to_session(var);
	if (!err) {
	    pprintf(prn, _("%s saved\n"), savename);
	}
    }

    return err;
}

int maybe_save_system (const CMD *cmd, gretl_equation_system *sys, PRN *prn)
{
    const char *savename = gretl_cmd_get_savename(cmd);
    int err;

    if (*savename == 0) {
	return 0;
    }

    err = stack_system_as(sys, savename);

    if (!err) {
	err = try_add_system_to_session(sys);
	if (!err) {
	    pprintf(prn, _("%s saved\n"), savename);
	} 
    }

    return err;
}

int maybe_save_graph (const CMD *cmd, const char *fname, int code,
		      PRN *prn)
{
    const char *savename = gretl_cmd_get_savename(cmd);
    char savedir[MAXLEN];
    gchar *tmp, *plotfile;
    int err = 0;

    if (*savename == 0) return 0;

    get_default_dir(savedir, SAVE_THIS_GRAPH);

    tmp = g_strdup(savename);
    plotfile = g_strdup_printf("%ssession.%s", savedir, 
			       space_to_score(tmp));
    g_free(tmp);

    if (code == GRETL_GNUPLOT_GRAPH) {
	err = copyfile(fname, plotfile);
	if (!err) {
	    int ret;

	    ret = real_add_graph_to_session(plotfile, savename, code);
	    if (ret == ADD_OBJECT_FAIL) {
		err = 1;
	    } else {
		remove(fname);
		if (ret == ADD_OBJECT_REPLACE) {
		    pprintf(prn, _("%s replaced\n"), savename);
		} else {
		    pprintf(prn, _("%s saved\n"), savename);
		}
	    }
	}
    }

    g_free(plotfile);

    return err;
}

int save_text_buffer (PRN *prn, const char *savename, PRN *errprn)
{
    int add, err = 0;

    add = real_add_text_to_session(prn, savename);

    if (add == ADD_OBJECT_FAIL) {
	err = 1;
    } else if (add == ADD_OBJECT_REPLACE) {
	pprintf(errprn, _("%s replaced\n"), savename);
    } else {
	pprintf(errprn, _("%s saved\n"), savename);
    }

    gretl_print_destroy(prn);

    return err;
}

int saved_object_action (const char *line, PRN *prn)
{
    char objname[MAXSAVENAME] = {0};
    char param[9] = {0};
    void *ptr = NULL;
    int code, err = 0;

    if (*line == '!' || *line == '#') { 
	/* shell command or comment */
	return 0;
    }

    code = parse_object_request(line, objname, param, &ptr, prn);

    if (code == OBJ_ACTION_NONE) {
	return 0;
    }

    if (code == OBJ_ACTION_NULL || code == OBJ_ACTION_INVALID) {
	return -1;
    }

    if (code == OBJ_ACTION_MODEL_SHOW) {
	display_saved_model(objname);
    } else if (code == OBJ_ACTION_MODEL_FREE) {
	err = delete_model_from_session(objname);
    } else if (code == OBJ_ACTION_MODEL_STAT) {
	print_model_stat(objname, param, prn);
    } else if (code == OBJ_ACTION_VAR_SHOW) {
	display_saved_VAR(objname);
    } else if (code == OBJ_ACTION_VAR_IRF) {
	session_VAR_do_irf(objname, line);
    } else if (code == OBJ_ACTION_VAR_FREE) {
	err = delete_VAR_from_session(objname);
	if (!err) pprintf(prn, _("Freed %s\n"), objname);
    } else if (code == OBJ_ACTION_GRAPH_SHOW) {
	GRAPHT *graph = (GRAPHT *) ptr;

	display_session_graph_png(graph->fname);
    } else if (code == OBJ_ACTION_GRAPH_FREE) {
	/* FIXME */
	dummy_call();
	fprintf(stderr, "Got request to delete graph\n");
    } else if (code == OBJ_ACTION_TEXT_SHOW) {
	display_saved_text(ptr);
    } else if (code == OBJ_ACTION_TEXT_FREE) {
	delete_text_from_session(ptr);
    } else if (code == OBJ_ACTION_SYS_SHOW) {
	display_saved_equation_system(objname);
    } else if (code == OBJ_ACTION_SYS_FREE) {
	err = delete_system_from_session(objname);
    }

    if (obj_action_free(code) && !err) {
	pprintf(prn, _("Freed %s\n"), objname);
    }

    return 1;
}
