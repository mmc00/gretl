/*
 *   Copyright (c) by Allin Cottrell
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

/* lexer module for 'genr' and related commands */

#include "genparse.h"
#include "usermat.h"
#include "loop_private.h"
#include "gretl_func.h"

#define NUMLEN 32
#define MAXQUOTE 64

#if GENDEBUG
# define LDEBUG 1
#else
# define LDEBUG 0
#endif

const char *wordchars = "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                        "0123456789_";

static char *fromdbl (double x)
{ 
    static char num[NUMLEN];
   
    sprintf(num, "%g", x);
    return num;
}

struct str_table {
    int id;
    const char *str;
};

struct str_table consts[] = {
    { CONST_PI, "pi" },
    { CONST_NA, "NA" },
    { 0,        NULL }
};

struct str_table dummies[] = {
    { DUM_NULL,    "null" },
    { DUM_DIAG,    "diag" },
    { DUM_DATASET, "dataset" },
    { 0,        NULL }
};

struct str_table dvars[] = {
    { R_NOBS,      "$nobs" },
    { R_NVARS,     "$nvars" },
    { R_PD,        "$pd" },
    { R_TEST_STAT, "$test" },
    { R_TEST_PVAL, "$pvalue" },
    { R_INDEX,     "t" },
    { R_INDEX,     "obs" },
    { R_T1,        "$t1" },
    { R_T2,        "$t2" },
    { 0,           NULL },
};

struct str_table mvars[] = {
    { M_ESS,     "$ess" },
    { M_T,       "$T" },
    { M_RSQ,     "$rsq" },
    { M_SIGMA,   "$sigma" },
    { M_DF,      "$df" },
    { M_NCOEFF,  "$ncoeff" },
    { M_LNL,     "$lnl" },
    { M_AIC,     "$aic" },
    { M_BIC,     "$bic" },
    { M_HQC,     "$hqc" },
    { M_TRSQ,    "$trsq" },
    { M_UHAT,    "$uhat" },
    { M_YHAT,    "$yhat" },
    { M_AHAT,    "$ahat" },
    { M_H,       "$h" },
    { M_COEFF,   "$coeff" },
    { M_SE,      "$stderr" },
    { M_VCV,     "$vcv" },
    { M_RHO,     "$rho" },
    { M_JALPHA,  "$jalpha" }, 
    { M_JBETA,   "$jbeta" },
    { M_JVBETA,  "$jvbeta" },
    { M_JS00,    "$s00" },
    { M_JS11,    "$s11" },
    { M_JS01,    "$s01" },
    { 0,         NULL }
};

struct str_table funcs[] = {
    { ABS,      "abs" },
    { SIN,      "sin" },
    { COS,      "cos" },
    { TAN,      "tan" },
    { ATAN,     "atan" },
    { LOG,      "log" },
    { LOG,      "ln" },
    { LOG10,    "log10" },
    { LOG2,     "log2" },
    { EXP,      "exp" },
    { SQRT,     "sqrt" },
    { DIF,      "diff" },
    { LDIF,     "ldiff" },
    { SDIF,     "sdiff" },
    { TOINT,    "int" },
    { SORT,     "sort" }, 
    { DSORT,    "dsort" }, 
    { ODEV,     "orthdev" },
    { NOBS,     "nobs" },
    { T1,       "firstobs" },
    { T2,       "lastobs" },
    { UNIFORM,  "uniform" }, 
    { NORMAL,   "normal" }, 
    { CHISQ,    "chisq" }, 
    { STUDENT,  "student" },
    { BINOMIAL, "binomial" },
    { GENPOIS,  "poisson" },
    { CUM,      "cum" }, 
    { MISSING,  "missing" },
    { OK,       "ok" },        /* opposite of missing */
    { MISSZERO, "misszero" },
    { LRVAR,    "lrvar" },
    { MEDIAN,   "median" },
    { GINI,     "gini" },
    { ZEROMISS, "zeromiss" },
    { SUM,      "sum" },
    { MEAN,     "mean" },
    { MIN,      "min" },
    { MAX,      "max" },
    { SD,       "sd" },
    { VCE,      "var" },
    { SST,      "sst" },
    { CNORM,    "cnorm" },
    { DNORM,    "dnorm" },
    { QNORM,    "qnorm" },
    { GAMMA,    "gamma" },
    { LNGAMMA,  "lngamma" },
    { RESAMPLE, "resample" },
    { PMEAN,    "pmean" },     /* panel mean */
    { PSD,      "psd" },       /* panel std dev */
    { HPFILT,   "hpfilt" },    /* Hodrick-Prescott filter */
    { BKFILT,   "bkfilt" },    /* Baxter-King filter */
    { FRACDIF,  "fracdiff" },  /* fractional difference */
    { COV,      "cov" },
    { COR,      "corr" },
    { IMAT,     "I" },
    { ZEROS,    "zeros" },
    { ONES,     "ones" },
    { MUNIF,    "muniform" },
    { MNORM,    "mnormal" },
    { SUMR,     "sumr" },
    { SUMC,     "sumc" },
    { MEANR,    "meanr" },
    { MEANC,    "meanc" },
    { MCOV,     "mcov" },
    { MCORR,    "mcorr" },
    { CDEMEAN,  "cdemean" },
    { CHOL,     "cholesky" },
    { INV,      "inv" },
    { DIAG,     "diag" },
    { TRANSP,   "transp" },
    { TVEC,     "vec" },
    { VECH,     "vech" },
    { UNVECH,   "unvech" },
    { ROWS,     "rows" },
    { COLS,     "cols" },
    { DET,      "det" },
    { LDET,     "ldet" },
    { TRACE,    "tr" },
    { NORM1,    "onenorm" },
    { RCOND,    "rcond" },
    { QFORM,    "qform" },
    { MLAG,     "mlag" },
    { QR,       "qrdecomp" },
    { EIGSYM,   "eigensym" },
    { EIGGEN,   "eigengen" },
    { NULLSPC,  "nullspace" },
    { MEXP,     "mexp" },
    { FDJAC,    "fdjac" },
    { BFGSMAX,  "BFGSmax" },
    { VARNUM,   "varnum" },
    { OBSNUM,   "obsnum" },
    { ISSERIES, "isseries" },
    { ISLIST,   "islist" },
    { ISSTRING, "isstring" },
    { ISNULL,   "isnull" },
    { LISTLEN,  "nelem" },
    { CDF,      "cdf" },
    { PVAL,     "pvalue" },
    { CRIT,     "critical" },
    { MAKEMASK, "makemask" },
    { VALUES,   "values" },
    { MSHAPE,   "mshape" },
    { SVD,      "svd" },
    { 0,     NULL }
};

int const_lookup (const char *s)
{
    int i;

    for (i=0; consts[i].id != 0; i++) {
	if (!strcmp(s, consts[i].str)) {
	    return consts[i].id;
	}
    }

    return 0;
}

const char *constname (int c)
{
    int i;

    for (i=0; consts[i].id != 0; i++) {
	if (c == consts[i].id) {
	    return consts[i].str;
	}
    }

    return "unknown";
}

int function_lookup (const char *s)
{
    int i;

    for (i=0; funcs[i].id != 0; i++) {
	if (!strcmp(s, funcs[i].str)) {
	    return funcs[i].id;
	}
    }

    return 0;
}

static const char *funname (int t)
{
    int i;

    for (i=0; funcs[i].id != 0; i++) {
	if (t == funcs[i].id) {
	    return funcs[i].str;
	}
    }

    return "unknown";
}

/* for external purposes (.lang file, manual) */

int gen_func_count (void)
{
    int i;

    for (i=0; funcs[i].id != 0; i++) ;
    return i;
}

const char *gen_func_name (int i)
{
    return funcs[i].str;
}

int model_var_count (void)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) ;
    return i;
}

const char *model_var_name (int i)
{
    return mvars[i].str;
}

int data_var_count (void)
{
    int i, n = 0;

    for (i=0; dvars[i].id != 0; i++) {
	if (dvars[i].str[0] == '$') {
	    n++;
	}
    }

    return n;
}

const char *data_var_name (int i)
{
    return dvars[i].str;
}

/* end external stuff */

static int dummy_lookup (const char *s)
{
    int i;

    for (i=0; dummies[i].id != 0; i++) {
	if (!strcmp(s, dummies[i].str)) {
	    return dummies[i].id;
	}
    }

    return 0;
}

const char *dumname (int t)
{
    int i;

    for (i=0; dummies[i].id != 0; i++) {
	if (t == dummies[i].id) {
	    return dummies[i].str;
	}
    }

    return "unknown";
}

static int dvar_lookup (const char *s)
{
    int i;

    for (i=0; dvars[i].id != 0; i++) {
	if (!strcmp(s, dvars[i].str)) {
	    return dvars[i].id;
	}
    }

    return 0;
}

const char *dvarname (int t)
{
    int i;

    for (i=0; dvars[i].id != 0; i++) {
	if (t == dvars[i].id) {
	    return dvars[i].str;
	}
    }

    return "unknown";
}

static int mvar_lookup (const char *s)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) {
	if (!strcmp(s, mvars[i].str)) {
	    return mvars[i].id;
	}
    }

    if (!strcmp(s, "$nrsq")) {
	/* alias */
	return M_TRSQ;
    }

    return 0;
}

const char *mvarname (int t)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) {
	if (t == mvars[i].id) {
	    return mvars[i].str;
	}
    }

    return "unknown";
}

static void undefined_symbol_error (const char *s, parser *p)
{
    parser_print_input(p);
    pprintf(p->prn, _("The symbol '%s' is undefined\n"), s);
    p->err = E_UNKVAR;
}

static void function_noargs_error (const char *s, parser *p)
{
    parser_print_input(p);
    pprintf(p->prn, _("'%s': no argument was given\n"), s);
    p->err = 1;
}

void context_error (int c, parser *p)
{
    parser_print_input(p);
    if (c != 0) {
	pprintf(p->prn, _("The symbol '%c' is not valid in this context\n"), 
		p->ch);
    } else {
	pprintf(p->prn, _("The symbol '%s' is not valid in this context\n"), 
		getsymb(p->sym, p));
    }
    if (p->err == 0) {
	p->err = 1;
    }
}

static char *get_quoted_string (parser *p)
{
    int n = parser_charpos(p, '"');
    char *s = NULL;

    if (n >= 0) {
	s = gretl_strndup(p->point, n);
	parser_advance(p, n + 1);
    } else {
	parser_print_input(p);
	pprintf(p->prn, _("Unmatched '%c'\n"), '"');
	p->err = E_PARSE;
    }

    return s;
}

static int might_be_date_string (const char *s, int n)
{
    char test[12];
    int y, m, d;

#if LDEBUG
    fprintf(stderr, "might_be_date_string: s='%s', n=%d\n", s, n);
#endif
    
    if (n > 10) {
	return 0;
    }

    *test = 0;
    strncat(test, s, n);

    if (strspn(s, "1234567890") == n) {
	/* plain integer */
	return 1;
    } else if (sscanf(s, "%d:%d", &y, &m) == 2) {
	/* quarterly, monthly date */
	return 1;
    } else if (sscanf(s, "%d/%d/%d", &y, &m, &d) == 3) {
	/* daily date */
	return 1;
    }

    return 0;
}

NODE *obs_node (parser *p)
{
    NODE *ret = NULL;
    char word[OBSLEN + 2] = {0};
    const char *s = p->point - 1;
    int close;
    int special = 0;
    int t = -1;

    close = haschar(']', s);

#if LDEBUG
    fprintf(stderr, "obs_node: s='%s', ch='%c', close=%d\n", 
	    s, (char) p->ch, close);
#endif

    if (close == 0) {
	pprintf(p->prn, _("Empty observation []\n"));
	p->err = E_PARSE;
    } else if (close < 0) {
	pprintf(p->prn, _("Unmatched '%c'\n"), '[');
	p->err = E_PARSE;
    } else if (*s == '"' && close < OBSLEN + 2 &&
	       haschar('"', s+1) == close - 2) {
	/* quoted observation label? */
	strncat(word, s, close);
	special = 1;
    } else if (might_be_date_string(s, close)) {
	strncat(word, s, close);
	special = 1;
    } 

    if (special && !p->err) {
	t = get_t_from_obs_string(word, (const double **) *p->Z, 
				  p->dinfo);
	if (t >= 0) {
	    /* convert to use-style 1-based index */
	    t++;
	}
    }

    if (t > 0) {
	parser_advance(p, close - 1);
	lex(p);
	ret = newdbl(t);
    } else if (!p->err) {
#if LDEBUG
	fprintf(stderr, "obs_node: first try failed, going for expr\n");
#endif
	lex(p);
	ret = expr(p);
    }

    return ret;
}

static void look_up_dollar_word (const char *s, parser *p)
{
    p->idnum = dvar_lookup(s);
    if (p->idnum > 0) {
	p->sym = DVAR;
    } else {
	p->idnum = mvar_lookup(s);
	if (p->idnum > 0) {
	    p->sym = MVAR;
	} else {
	    undefined_symbol_error(s, p);
	}
    }
}

static void look_up_word (const char *s, parser *p)
{
    int fsym, err = 0;

    fsym = p->sym = function_lookup(s);

    if (p->sym == 0 || p->ch != '(') {
	p->idnum = const_lookup(s);
	if (p->idnum > 0) {
	    p->sym = CON;
	} else {
	    p->idnum = dummy_lookup(s);
	    if (p->idnum > 0) {
		p->sym = DUM;
	    } else {
		p->idnum = varindex(p->dinfo, s);
		if (p->idnum < p->dinfo->v) {
		    p->sym = UVAR;
		} else if (get_matrix_by_name(s)) {
		    p->sym = UMAT;
		    p->idstr = gretl_strdup(s);
		} else if (gretl_get_object_by_name(s)) {
		    p->sym = UOBJ;
		    p->idstr = gretl_strdup(s);
		} else if (get_list_by_name(s)) {
		    p->sym = LIST;
		    p->idstr = gretl_strdup(s);
		} else if (gretl_is_user_function(s)) {
		    p->sym = UFUN;
		    p->idstr = gretl_strdup(s);
		} else {
		    err = 1;
		}
	    }
	}
    }

    if (err) {
	if (fsym) {
	    function_noargs_error(s, p);
	} else {
	    undefined_symbol_error(s, p);
	}
    }
}

#define could_be_matrix(t) (model_data_matrix(t) || t == M_UHAT)

static void word_check_next_char (const char *s, parser *p)
{
#if LDEBUG
    fprintf(stderr, "word_check_next_char: ch = '%c'\n", p->ch);
#endif

    if (p->ch == '(') {
	/* series (lag) or function */
	if (p->sym == UVAR && var_is_series(p->dinfo, p->idnum)) {
	    if (p->idnum == p->lh.v) {
		p->flags |= P_AUTOREG;
	    }
	    p->sym = LAG;
	} else if (p->sym == MVAR && model_data_matrix(p->idnum)) {
	    /* old-style "$coeff(x1)" etc. */
	    p->sym = DMSTR;
	    p->idstr = gretl_strdup(s);
	} else if (!func_symb(p->sym) && !func2_symb(p->sym) &&
		   !funcn_symb(p->sym) && p->sym != UFUN) {
	    p->err = 1;
	} 
    } else if (p->ch == '[') {
	if (p->sym == UMAT) {
	    /* slice of user matrix */
	    p->sym = MSL;
	} else if (p->sym == MVAR && could_be_matrix(p->idnum)) {
	    /* slice of $ matrix */
	    p->sym = DMSL;
	    p->idstr = gretl_strdup(s);
	} else if (p->sym == UVAR && var_is_series(p->dinfo, p->idnum)) {
	    /* observation from series */
	    p->sym = OBS;
	} else {
	    p->err = 1;
	} 
    } else if (p->ch == '.' && parser_charpos(p, '$') == 0) {
	if (p->sym == UOBJ) {
	    /* name of saved object followed by dollar variable? */
	    p->sym = OVAR;
	} else {
	    p->err = 1;
	}	    
    }

    if (p->err) {
	context_error(p->ch, p);
    }
}

static void getword (parser *p)
{  
    char word[32];
    int i = 0;

    /* we know the first char is acceptable (and might be '$') */
    word[i++] = p->ch;
    parser_getc(p);

    while (p->ch != 0 && strchr(wordchars, p->ch) != NULL && i < 31) {
	word[i++] = p->ch;
	parser_getc(p);
    }

    word[i] = '\0';

#if LDEBUG
    fprintf(stderr, "getword: word = '%s'\n", word);
#endif

    while (p->ch != 0 && strchr(wordchars, p->ch) != NULL) {
	/* flush excess word characters */
	parser_getc(p);
    }

    if (p->getstr) {
	/* uninterpreted string wanted */
	p->sym = STR;
	p->idstr = gretl_strdup(word);
	p->getstr = 0;
	return;
    }

    /* handle loop index scalar */
    if (word[1] == '\0' && is_active_index_loop_char(word[0])) {
	p->sym = LOOPIDX;
	p->idstr = gretl_strdup(word);
	return;
    }

    if (*word == '$' || !strcmp(word, "t") || !strcmp(word, "obs")) {
	look_up_dollar_word(word, p);
    } else {
	look_up_word(word, p);
    }

    if (!p->err) {
	word_check_next_char(word, p);
    }

#if LDEBUG
    fprintf(stderr, "getword: p->err = %d\n", p->err);
#endif
}

/* below: we're testing 'ch' for validity, given what we've already
   packed into string 's' up to element 'i'
*/

static int ok_dbl_char (int ch, char *s, int i)
{
    int ret = 0;

    if (i < 0) {
	return 1;
    }

    if (ch >= '0' && ch <= '9') {
	/* numeral: always OK */
	ret = 1;
    } else if (s[i] == 'e' || s[i] == 'E') {
	if (ch == '+' || ch == '-') {
	    /* +/- "inside" a double: only OK at the start of the exponent */
	    ret = 1;
	}
    } else if (ch == '.' && !strchr(s, '.') && 
	       !strchr(s, 'e') && !strchr(s, 'E')) {
	/* point is OK is we haven't already got one, and
	   we're not already in the exponent part */
	ret = 1;
    } else if (ch == 'e' || ch == 'E') {
	if (!strchr(s, 'e') && !strchr(s, 'E')) {
	    /* exponent char is OK if we don't already have one */
	    ret = 1;
	}
    }

    return ret;
}

static double getdbl (parser *p)
{
    char xstr[NUMLEN] = {0};
    int i = 0;

    while (ok_dbl_char(p->ch, xstr, i - 1) && i < NUMLEN - 1) {
	xstr[i++] = p->ch;
	parser_getc(p);
    }  

    while (p->ch >= '0' && p->ch <= '9') {
	/* flush excess numeric characters */
	parser_getc(p);
    } 

#if LDEBUG
    fprintf(stderr, "getdbl: xstr = '%s', x = %g\n", xstr,
	    dot_atof(xstr));
#endif

    return dot_atof(xstr);
}

/* FIXME: '&' as "take-address" operator */

static void deprecation_note (parser *p)
{
    if (p->sym == B_AND) {
	pprintf(p->prn, "Note: the recommended form for logical "
		"AND is '&&'\n");
    } else if (p->sym == B_OR) {
	pprintf(p->prn, "Note: the recommended form for logical "
		"OR is '||'\n");
    }
}

#define matrix_gen(p) (p->lh.t == MAT || p->targ == MAT)

#define unary_context(p) (p->sym < F2_MAX || p->sym == COM)

void lex (parser *p)
{
    while (p->ch != 0) {
	switch (p->ch) {
	case ' ':
	case '\t':
	case '\r':
        case '\n': 
	    parser_getc(p);
	    break;
        case '+': 
	    p->sym = B_ADD;
	    parser_getc(p);
	    return;
        case '-': 
	    p->sym = B_SUB;
	    parser_getc(p);
	    return;
        case '*': 
	    parser_getc(p);
	    if (p->ch == '*') {
		p->sym = B_POW;
		parser_getc(p);
	    } else {
		p->sym = B_MUL;
	    }
	    return;
	case '\'':
	    p->sym = B_TRMUL;
	    parser_getc(p);
	    return;
        case '/': 
	    p->sym = B_DIV;
	    parser_getc(p);
	    return;
        case '%': 
	    p->sym = B_MOD;
	    parser_getc(p);
	    return;
        case '^': 
	    p->sym = B_POW;
	    parser_getc(p);
	    return;
        case '&': 
	    if (unary_context(p)) {
		p->sym = U_ADDR;
		parser_getc(p);
		return;
	    }
	    p->sym = B_AND;
	    parser_getc(p);
	    if (p->ch == '&') {
		parser_getc(p);
	    } else {
		deprecation_note(p);
	    }
	    return;
        case '|': 
	    p->sym = (matrix_gen(p))? MRCAT : B_OR;
	    parser_getc(p);
	    if (p->ch == '|') {
		if (p->sym == B_OR) {
		   deprecation_note(p);
		} else {
		    p->sym = B_OR;
		}
		parser_getc(p);
	    }
	    return;
        case '!': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_NEQ;
		parser_getc(p);
	    } else {
		p->sym = U_NOT;
	    }
	    return;
        case '=': 
	    p->sym = B_EQ;
	    parser_getc(p);
	    return;
        case '>': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_GTE;
		parser_getc(p);
	    } else {
		p->sym = B_GT;
	    }
	    return;
        case '<': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_LTE;
		parser_getc(p);
	    } else if (p->ch == '>') {
		p->sym = B_NEQ;
		parser_getc(p);
	    } else {
		p->sym = B_LT;
	    }
	    return;
        case '(': 
	    p->sym = LPR;
	    parser_getc(p);
	    return;
        case ')': 
	    p->sym = RPR;
	    parser_getc(p);
	    return;
        case '[': 
	    p->sym = LBR;
	    parser_getc(p);
	    return;
        case '{': 
	    p->sym = LCB;
	    parser_getc(p);
	    return;
        case '}': 
	    p->sym = RCB;
	    parser_getc(p);
	    return;
        case ']': 
	    p->sym = RBR;
	    parser_getc(p);
	    return;
        case '~':
	    p->sym = MCCAT;
	    parser_getc(p);
	    return;
        case '`': 
	    p->sym = MRCAT;
	    parser_getc(p);
	    return;
        case ',': 
	    p->sym = COM;
	    parser_getc(p);
	    return;
        case ';': 
	    p->sym = SEMI;
	    parser_getc(p);
	    return;
        case ':': 
	    p->sym = COL;
	    parser_getc(p);
	    return;
        case '?': 
	    p->sym = QUERY;
	    parser_getc(p);
	    return;
	case '.':
	    if (*p->point == '$') {
		p->sym = DOT;
		parser_getc(p);
		return;
	    }
	    parser_getc(p);
	    if (p->ch == '*') {
		p->sym = DOTMULT;
		parser_getc(p);
		return;
	    } else if (p->ch == '/') {
		p->sym = DOTDIV;
		parser_getc(p);
		return;
	    } else if (p->ch == '^') {
		p->sym = DOTPOW;
		parser_getc(p);
		return;
	    } else if (p->ch == '+') {
		p->sym = DOTADD;
		parser_getc(p);
		return;
	    } else if (p->ch == '-') {
		p->sym = DOTSUB;
		parser_getc(p);
		return;
	    } else if (p->ch == '=') {
		p->sym = DOTEQ;
		parser_getc(p);
		return;
	    } else {
		/* not a "dot operator", back up */
		parser_ungetc(p);
	    }
        default: 
	    if (isdigit(p->ch) || (p->ch == '.' && isdigit(*p->point))) {
		p->xval = getdbl(p);
		p->sym = NUM;
		return;
	    } else if (islower(p->ch) || isupper(p->ch) || p->ch == '$') {
		getword(p);
		return;
	    } else if (p->ch == '"') {
		p->idstr = get_quoted_string(p);
		p->sym = STR;
		return;
	    } else {
		parser_print_input(p);
		pprintf(p->prn, _("Invalid character '%c'\n"), p->ch);
		p->err = 1;
		return;
	    }
	} /* end ch switch */
    } /* end while ch != 0 */
}

const char *getsymb (int t, const parser *p)
{  
    if ((t > OP_MAX && t < FUNC_MAX) ||
	(t > FUNC_MAX && t < F2_MAX) ||
	(t > F2_MAX && t < FN_MAX)) {
	return funname(t);
    }

    /* yes, well */
    if (t == OBS) {
	return "OBS";
    } else if (t == MSL) {
	return "MSL";
    } else if (t == DMSL) {
	return "DMSL";
    } else if (t == DMSTR) {
	return "DMSTR";
    } else if (t == MSL2) {
	return "MSL2";
    } else if (t == MSPEC) {
	return "MSPEC";
    } else if (t == SUBSL) {
	return "SUBSL";
    } else if (t == MDEF) {
	return "MDEF";
    } else if (t == FARGS) {
	return "FARGS";
    } else if (t == LIST) {
	return "LIST";
    } else if (t == OVAR) {
	return "OVAR";
    }

    if (p != NULL) {
	if (t == NUM) {
	    return fromdbl(p->xval); 
	} else if (t == UVAR) {
	    return p->dinfo->varname[p->idnum];
	} else if (t == UMAT || t == UOBJ ||
		   t == LOOPIDX) {
	    return p->idstr;
	} else if (t == CON) {
	    return constname(p->idnum);
	} else if (t == DUM) {
	    return dumname(p->idnum);
	} else if (t == DVAR) {
	    return dvarname(p->idnum);
	} else if (t == MVAR) {
	    return mvarname(p->idnum);
	} else if (t == UFUN) {
	    return p->idstr;
	} else if (t == STR) {
	    return p->idstr;
	}
    } 

    switch (t) {
    case B_ASN:
	return "=";
    case B_ADD: 
    case U_POS:
	return "+";
    case B_SUB: 
    case U_NEG:
	return "-";
    case B_MUL: 
	return "*";
    case B_TRMUL: 
	return "'";
    case B_DIV: 
	return "/";
    case B_MOD: 
	return "%";
    case B_POW: 
	return "^";
    case B_EQ: 
	return "=";
    case B_NEQ: 
	return "!=";
    case B_GT: 
	return ">";
    case B_LT: 
	return "<";
    case B_GTE: 
	return ">=";
    case B_LTE: 
	return "<=";
    case B_AND: 
	return "&&";
    case U_ADDR:
	return "&";
    case B_OR: 
	return "||";	
    case U_NOT: 
	return "!";
    case LPR: 
	return "(";
    case RPR: 
	return ")";
    case LBR: 
	return "[";
    case RBR: 
	return "]";
    case LCB: 
	return "{";
    case RCB: 
	return "}";
    case DOTMULT: 
	return ".*";
    case DOTDIV: 
	return "./";
    case DOTPOW: 
	return ".^";
    case DOTADD: 
	return ".+";
    case DOTSUB: 
	return ".-";
    case DOTEQ: 
	return ".=";
    case KRON: 
	return "**";
    case MCCAT: 
	return "~";
    case MRCAT: 
	return "|";
    case COM: 
	return ",";
    case DOT: 
	return ".";
    case SEMI: 
	return ";";
    case COL: 
	return ":";
    case QUERY: 
	return "?";
    case LAG:
	return "lag";
    default: 
	break;
    }

    return "unknown";
}


