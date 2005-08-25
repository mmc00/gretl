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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* gretl_commands.c */

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "gretl_commands.h"

struct gretl_cmd {
    int cnum;
    const char *cword;
};

static struct gretl_cmd gretl_cmds[] = {
    { SEMIC,    ";" },     
    { ADD,      "add" },
    { ADDOBS,   "addobs" },
    { ADDTO,    "addto" },
    { ADF,      "adf" }, 
    { APPEND,   "append" },
    { AR,       "ar" },       
    { ARCH,     "arch" },
    { ARMA,     "arma" },
    { BREAK,    "break" },
    { BXPLOT,   "boxplot" },
    { CHOW,     "chow" },     
    { COEFFSUM, "coeffsum" },
    { COINT,    "coint" },
    { COINT2,   "coint2" },
    { CORC,     "corc" }, 
    { CORR,     "corr" },     
    { CORRGM,   "corrgm" },   
    { CRITERIA, "criteria" },
    { CRITICAL, "critical" },
    { CUSUM,    "cusum" },
    { DATA,     "data" },
    { DELEET,   "delete" },
    { DIFF,     "diff" },
    { ELSE,     "else" },
    { END,      "end" },
    { ENDIF,    "endif" },
    { ENDLOOP,  "endloop" },
    { EQNPRINT, "eqnprint" },
    { EQUATION, "equation" },
    { ESTIMATE, "estimate" },
    { FCAST,    "fcast" }, 
    { FCASTERR, "fcasterr" },
    { FIT,      "fit" }, 
    { FREQ,     "freq" }, 
    { FUNC,     "function" },
    { FUNCERR,  "funcerr" },
    { GARCH,    "garch" },
    { GENR,     "genr" },     
    { GNUPLOT,  "gnuplot" },  
    { GRAPH,    "graph" }, 
    { HAUSMAN,  "hausman" },
    { HCCM,     "hccm" }, 
    { HELP,     "help" },    
    { HILU,     "hilu" },    
    { HSK,      "hsk" }, 
    { HURST,    "hurst" },
    { IF,       "if" },
    { IMPORT,   "import" },
    { INCLUDE,  "include" },
    { INFO,     "info" }, 
    { KPSS,     "kpss" },
    { LABEL,    "label" },
    { LABELS,   "labels" },
    { LAD,      "lad" },
    { LAGS,     "lags" },    
    { LDIFF,    "ldiff" },
    { LEVERAGE, "leverage" },
    { LMTEST,   "lmtest" }, 
    { LOGISTIC, "logistic" },
    { LOGIT,    "logit" },
    { LOGS,     "logs" },
    { LOOP,     "loop" },
    { MAHAL,    "mahal" },
    { MEANTEST, "meantest" },
    { MODELTAB, "modeltab" },
    { MPOLS,    "mpols" },
    { MULTIPLY, "multiply" },
    { NLS,      "nls" },
    { NULLDATA, "nulldata" },
    { OLS,      "ols" },     
    { OMIT,     "omit" },
    { OMITFROM, "omitfrom" },
    { OPEN,     "open" },
    { OUTFILE,  "outfile" },
    { PANEL,    "panel" },
    { PCA,      "pca" },
    { PERGM,    "pergm" },
    { PLOT,     "plot" },    
    { POISSON,  "poisson" },
    { POOLED,   "pooled" },
    { PRINT,    "print" }, 
    { PRINTF,   "printf" },
    { PROBIT,   "probit" },
    { PVALUE,   "pvalue" },  
    { PWE,      "pwe" },
    { QUIT,     "quit" }, 
    { REMEMBER, "remember" },
    { RENAME,   "rename" },
    { RESET,    "reset" },
    { RESTRICT, "restrict" },
    { RHODIFF,  "rhodiff" }, 
    { RMPLOT,   "rmplot" },
    { RUN,      "run" },
    { RUNS,     "runs" },
    { SCATTERS, "scatters" },
    { SET,      "set" },
    { SETOBS,   "setobs" },
    { SETMISS,  "setmiss" },
    { SHELL,    "shell" },   
    { SIM,      "sim" },     
    { SMPL,     "smpl" },
    { SPEARMAN, "spearman" },
    { SQUARE,   "square" },  
    { STORE,    "store" },   
    { SUMMARY,  "summary" },
    { SYSTEM,   "system" },
    { TABPRINT, "tabprint" },
    { TESTUHAT, "testuhat" },
    { TOBIT,    "tobit" },
    { TSLS,     "tsls" },    
    { VAR,      "var" },
    { VARLIST,  "varlist" },
    { VARTEST,  "vartest" },
    { VECM,     "vecm" },
    { VIF,      "vif" },
    { WLS,      "wls" },
    { NC,       NULL}
}; 

const char *gretl_command_word (int i)
{
    if (i >= 0 && i < NC) {
	return gretl_cmds[i].cword;
    } else {
	return "";
    }
}

static GHashTable *ht;

static void gretl_command_hash_init (void)
{
    int i;

    ht = g_hash_table_new(g_str_hash, g_str_equal);

    for (i=0; gretl_cmds[i].cword != NULL; i++) {
	g_hash_table_insert(ht, (gpointer) gretl_cmds[i].cword, 
			    GINT_TO_POINTER(gretl_cmds[i].cnum));
    }
}

int gretl_command_number (const char *s)
{    
    gpointer p;
    int ret = 0;

    if (ht == NULL) {
	gretl_command_hash_init();
    }
    
    p = g_hash_table_lookup(ht, s);
    if (p != NULL) {
	ret = GPOINTER_TO_INT(p);
    }

    return ret;
}

void gretl_command_hash_cleanup (void)
{
    if (ht != NULL) {
	g_hash_table_destroy(ht);
    }
}

const char *gretl_command_complete_next (const char *s,
					 int idx)
{
    size_t n = strlen(s);
    int i;

    for (i=idx; i<NC; i++) {
	if (!strncmp(s, gretl_cmds[i].cword, n)) {
	    return gretl_cmds[i].cword;
	}
    }
    return NULL;
}  

const char *gretl_command_complete (const char *s)
{
    return gretl_command_complete_next(s, 0);
}  
