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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* texprint.c for gretl - LaTeX output from modeling */

#include "libgretl.h"

/* ......................................................... */

static void tex_print_float (const double x, const int tab, PRN *prn)
     /* prints a floating point number as a TeX math string.
	if tab != 0, print the sign in front, separated by a
	tab symbol (for equation-style regression printout).
     */
{
    char number[16];

    if (fabs(x) < 100. && fabs(x) > 1.) 
	sprintf(number, "%6.4f", x);
    else {
	if (fabs(x) < 10000. && fabs(x) > 99.999) 
	    sprintf(number, "%6.3f", x);
	else {
	    if (fabs(x) < .0000001) sprintf(number, "%.4g", x);
	    else sprintf(number, "%f", x);
	}
    }
    if (tab) {
	if (x < 0.) pprintf(prn, "& $-$ & $%s$", number + 1);
	else pprintf(prn, "& $+$ & $%s$", number);
    } else pprintf(prn, "$%s$", number);
}

/**
 * tex_escape:
 * @targ: target string (must be pre-allocated)
 * @src: source string.
 *
 * Copies from @src to @targ, escaping any characters in @src that are 
 * special to TeX (by inserting a leading backslash).
 * 
 * Returns: the transformed copy of the string
 */

char *tex_escape (char *targ, const char *src)
{
    while (*src) {
	if (*src == '$' || *src == '&' || *src == '_' || 
	    *src == '%' || *src == '#')
	    *targ++ = '\\';
	*targ++ = *src++;
    }
    *targ = '\0';
    return targ;
}

/* ......................................................... */ 

static void tex_print_coeff (const DATAINFO *pdinfo, const MODEL *pmod, 
			     const int c, PRN *prn)
{
    int n;
    double decbit;
    char intstr[16], decstr[9], tmp[16];
    
    n = (int) pmod->coeff[c-1];
    decbit = pmod->coeff[c-1] - n;
    if (pmod->coeff[c-1] < 0) {
	decbit = -decbit;
	if (n == 0) strcpy(intstr, "-0");
	else sprintf(intstr, "%d", n);
    }
    else sprintf(intstr, "%d", n);
    sprintf(decstr, "%f", decbit);

    tmp[0] = '\0';
    tex_escape(tmp, pdinfo->varname[pmod->list[c]]);
    pprintf(prn, "%s &\n"
	    "  $%s$&%s &\n"
	    "    $%f$ &\n"
	    "      $%.4f$ &\n"
	    "        $%f$ \\\\\n",  
	    tmp,
	    intstr, decstr+2,
	    pmod->sderr[c-1],
	    pmod->coeff[c-1]/pmod->sderr[c-1],
	    tprob(pmod->coeff[c-1]/pmod->sderr[c-1], 
		  pmod->dfd));	
}

/* ......................................................... */

static int make_texfile (const PATHS *ppaths, const int model_count,
			 int equation, char *texfile, PRN *prn)
{
    prn->buf = NULL;

    sprintf(texfile, "%s%s_%d.tex", ppaths->userdir,
	    (equation)? "equation" : "model", model_count);

    prn->fp = fopen(texfile, "w");
    if (prn->fp == NULL) return 1;
    else return 0;
}

/**
 * tex_print_equation:
 * @pmod:  pointer to gretl MODEL struct.
 * @pdinfo:  information regarding the data set.
 * @standalone: indicator variable.
 * @prn: gretl printing struct.
 *
 * Prints a gretl model in the form of a LaTeX equation, either as
 * a stand-alone document or as a fragment of LaTeX source for
 * insertion into a document.
 * 
 * Returns: 0 on successful completion.
 */

int tex_print_equation (const MODEL *pmod, const DATAINFO *pdinfo, 
			const int standalone, PRN *prn)
{

    int i, start, constneg = 0, ncoeff = pmod->list[0];
    double tstat, const_tstat = 0, const_coeff = 0;
    char tmp[16];

    if (standalone) {
	pprintf(prn, "\\documentclass[11pt]{article}\n\\begin{document}\n"
		"\\thispagestyle{empty}\n\n");
    }
    pprintf(prn, "\\begin{center}\n");

    if (pmod->ifc) {
	const_coeff = pmod->coeff[pmod->list[0]-1];
	const_tstat = pmod->coeff[pmod->list[0]-1]
	    / pmod->sderr[pmod->list[0]-1];
	if (const_coeff < 0.) constneg = 1;
	ncoeff--;
    }

    /* tabular header */
    pprintf(prn, "{\\setlength{\\tabcolsep}{.5ex}\n"
	    "\\renewcommand{\\arraystretch}{1}\n"
	    "\\begin{tabular}{rc"
	    "%s", (pmod->ifc)? "c" : "c@{\\,}l");
    start = (pmod->ifc)? 1 : 2;
    for (i=start; i<ncoeff; i++) pprintf(prn, "cc@{\\,}l");
    pprintf(prn, "}\n");

    /* dependent variable */
    tmp[0] = '\0';
    tex_escape(tmp, pdinfo->varname[pmod->list[1]]);
    pprintf(prn, "$\\widehat{\\rm %s}$ & = &\n", tmp);

    start++;
    /* coefficients times indep vars */
    if (pmod->ifc) tex_print_float(const_coeff, 0, prn);
    else {
	tex_escape(tmp, pdinfo->varname[pmod->list[2]]);
	tex_print_float(pmod->coeff[1], 0, prn);
	pprintf(prn, " & %s ", tmp);
    }
    for (i=start; i<=ncoeff; i++) {
	tex_print_float(pmod->coeff[i-1], 1, prn);
	tex_escape(tmp, pdinfo->varname[pmod->list[i]]);
	pprintf(prn, " & %s ", tmp);
    }
    pprintf(prn, "\\\\\n");

    /* t-stats in row beneath */
    if (pmod->ifc) {
	pprintf(prn, "& ");
	pprintf(prn, "& {\\small $(%.2f)$} ", const_tstat);
    } 
    for (i=2; i<=ncoeff; i++) {
        tstat = pmod->coeff[i-1]/pmod->sderr[i-1];
	if (i == 2) pprintf(prn, "& & \\small{$(%.2f)$} ", tstat);
	else pprintf(prn, "& & & \\small{$(%.2f)$} ", tstat);
    }
    pprintf(prn, "\n\\end{tabular}}\n\n");

    /* additional info (R^2 etc) */
    pprintf(prn, "\\vspace{.8ex}\n");
    pprintf(prn, "$T = %d,\\, \\bar{R}^2 = %.3f,\\, F(%d,%d) = %.3f,\\, "
	    "\\hat{\\sigma} = %f$\n",
	   pmod->nobs, pmod->adjrsq, pmod->dfn, 
	   pmod->dfd, pmod->fstt, pmod->sigma);

    pprintf(prn, "\n($t$-statistics in parentheses)\n"
	    "\\end{center}\n");

    if (standalone) 
	pprintf(prn, "\n\\end{document}\n");

    return 0;
}

/**
 * tex_print_model:
 * @pmod:  pointer to gretl MODEL struct.
 * @pdinfo:  information regarding the data set.
 * @standalone: indicator variable.
 * @prn: gretl printing struct.
 *
 * Prints a gretl model in the form of a LaTeX table, either as
 * a stand-alone document or as a fragment of LaTeX source for
 * insertion into a document.
 * 
 * Returns: 0 on successful completion.
 */

int tex_print_model (const MODEL *pmod, const DATAINFO *pdinfo, 
		     const int standalone, PRN *prn)
{
    int i, ncoeff = pmod->list[0];
    int t1 = pmod->t1, t2 = pmod->t2;
    char tmp[16];
    char startdate[9], enddate[9];

    if (pmod->data) {
	MISSOBS *mobs = (MISSOBS *) pmod->data;

	t2 += mobs->misscount;
    }

    ncoeff = pmod->list[0];
    ntodate(startdate, t1, pdinfo);
    ntodate(enddate, t2, pdinfo);

    if (standalone) {
	pprintf(prn, "\\documentclass{article}\n\\begin{document}\n\n"
		"\\thispagestyle{empty}\n");
    }

    pprintf(prn, "\\begin{center}\n");
    tex_escape(tmp, pdinfo->varname[pmod->list[1]]);
    pprintf(prn, "\\textsc{Model %d: OLS estimates using the %d "
	    "observations %s--%s}\\\\\n"
	    "Dependent variable: %s\n\n", 
	    pmod->ID, pmod->nobs, startdate, enddate, tmp);

    pprintf(prn, "\\vspace{1em}\n\n"
	  "\\begin{tabular*}{\\textwidth}"
	  "{@{\\extracolsep{\\fill}}\n"
	  "l%%  col 1: varname\n"
	  "  r@{\\extracolsep{0pt}.}l%% col 2: first part of coeff\n"
	  "    @{\\extracolsep{\\fill}}rrr}%% cols 3,4,5: stderr, "
	  "tstat, pvalue\n"
	  "Variable &\n"
	  "  \\multicolumn{2}{c}{Coefficient} &\n"
	  "    \\multicolumn{1}{c}{Std.\\ Error} &\n"
	  "      \\multicolumn{1}{c}{$t$-statistic} &\n"
	  "        \\multicolumn{1}{c}{p-value} \\\\[1ex]\n");

    if (pmod->ifc) {
	tex_print_coeff(pdinfo, pmod, ncoeff, prn);
	ncoeff--;
    }
    for (i=2; i<=ncoeff; i++) tex_print_coeff(pdinfo, pmod, i, prn);

    pprintf(prn, "\\end{tabular*}\n\n"
	  "\\vspace{1em}\n\n"
	  "\\begin{tabular*}{\\textwidth}{@{\\extracolsep{\\fill}}lrlr}\n");

    pprintf(prn, "Mean of dep.\\ var. & $%f$ &"
	    "S.D. of dep. variable & %f\\\\\n", 
	    pmod->ybar, pmod->sdy);
    pprintf(prn, "ESS & %f &"
	    "Std Err of Resid. ($\\hat{\\sigma}$) & %f\\\\\n",
	    pmod->ess, pmod->sigma);
    pprintf(prn, "$R^2$  & %f &"
	    "$\\bar{R}^2$        & $%f$ \\\\\n",
	    pmod->rsq, pmod->adjrsq);
    pprintf(prn, "F-statistic (%d, %d) & %f &"
	    "p-value for F()          & %f\\\\\n",
	    pmod->dfn, pmod->dfd, pmod->fstt,
	    fdist(pmod->fstt, pmod->dfn, pmod->dfd));
    pprintf(prn, "Durbin--Watson stat. & $%f$ &"
	    "$\\hat{\\rho}$ & $%f$ \n",
	    pmod->dw, pmod->rho);

    pprintf(prn, "\\end{tabular*}\n\n"
	    "\\vspace{1em}\n\n"
	    "\\textsc{model selection statistics}\n\n"
	    "\\vspace{1em}\n\n"
	    "\\begin{tabular*}{\\textwidth}{@{\\extracolsep{\\fill}}lrlrlr}\n");
    pprintf(prn, "\\textsc{sgmasq}  &  %g   &"  
	    "\\textsc{aic}     &  %g  &"  
	    "\\textsc{fpe}     &  %g  \\\\\n"
	    "\\textsc{hq}      &  %g  &"
	    "\\textsc{schwarz} &  %g  &"  
	    "\\textsc{shibata} &  %g  \\\\\n"
	    "\\textsc{gcv}     &  %g  &"  
	    "\\textsc{rice}    &  %g\n",
	    pmod->criterion[0], pmod->criterion[1], pmod->criterion[2],
	    pmod->criterion[3], pmod->criterion[4], pmod->criterion[5],
	    pmod->criterion[6], pmod->criterion[7]);
    pprintf(prn, "\\end{tabular*}\n\n"
	    "\n\\end{center}\n");
    
    if (standalone) 
	pprintf(prn, "\n\\end{document}\n");

    return 0;
}

/**
 * tabprint:
 * @pmod: pointer to gretl MODEL struct.
 * @pdinfo: information regarding the data set.
 * @ppaths: struct containing information on paths.
 * @texfile: name of file to save.
 * @model_count: count of models estimated so far.
 * @oflag: option: standalone or not.
 *
 * Prints to file a gretl model in the form of a LaTeX table, either as
 * a stand-alone document or as a fragment of LaTeX source for
 * insertion into a document.
 * 
 * Returns: 0 on successful completion, 1 on error.
 */

int tabprint (const MODEL *pmod, const DATAINFO *pdinfo,
	      const PATHS *ppaths, char *texfile,
	      const int model_count, int oflag)
{
    PRN prn;

    if (make_texfile(ppaths, model_count, 0, texfile, &prn))
	return 1;

    tex_print_model(pmod, pdinfo, oflag, &prn);
    if (prn.fp != NULL) fclose(prn.fp);
    return 0;
}

/**
 * eqnprint:
 * @pmod: pointer to gretl MODEL struct.
 * @pdinfo: information regarding the data set.
 * @ppaths: struct containing information on paths.
 * @texfile: name of file to save.
 * @model_count: count of models estimated so far.
 * @oflag: option: standalone or not.
 *
 * Prints to file a gretl model in the form of a LaTeX equation, either as
 * a stand-alone document or as a fragment of LaTeX source for
 * insertion into a document.
 * 
 * Returns: 0 on successful completion, 1 on error.
 */

int eqnprint (const MODEL *pmod, const DATAINFO *pdinfo,
	      const PATHS *ppaths, char *texfile,
	      const int model_count, int oflag)
{
    PRN prn;

    if (make_texfile(ppaths, model_count, 1, texfile, &prn))
	return 1;

    tex_print_equation(pmod, pdinfo, oflag, &prn);
    if (prn.fp != NULL) fclose(prn.fp);
    return 0;
}
