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

/* panel data plugin for gretl */

#include "libgretl.h"

typedef struct {
    int ns;
    double sigma_e;
    double H;
    double *bdiff;
    double *sigma;
} hausman_t;

/* #define PDEBUG 1 */

/* .................................................................. */

static void print_panel_const (MODEL *panelmod, PRN *prn)
{
    char numstr[18];
    int i = panelmod->list[0] - 1;

    sprintf(numstr, "(%.5g)", panelmod->sderr[i]);
    pprintf(prn, _(" constant: %14.5g %15s\n"), 
	    panelmod->coeff[i], numstr);
}

/* .................................................................. */

static void print_panel_coeff (MODEL *pmod, MODEL *panelmod,
			       DATAINFO *pdinfo, int i, 
			       PRN *prn)
{
    char numstr[18];

    sprintf(numstr, "(%.5g)", panelmod->sderr[i]);
    pprintf(prn, "%9s: %14.5g %15s\n", 
	    pdinfo->varname[pmod->list[i+1]],
	    panelmod->coeff[i], numstr);
}

/* .................................................................. */

static double group_means_variance (MODEL *pmod, 
				    double **Z, DATAINFO *pdinfo,
				    double ***groupZ, DATAINFO **ginfo,
				    int nunits, int T) 
{
    int i, j, t, start, *list;
    double xx;
    MODEL meanmod;

#ifdef PDEBUG
    fprintf(stderr, "group_means_variance: creating dataset\n"
	    " nvar=%d, nobs=%d, *groupZ=%p\n", pmod->list[0],
	    nunits, (void *)*groupZ);
#endif

    *ginfo = create_new_dataset(groupZ, pmod->list[0], nunits, 0);
    if (*ginfo == NULL) return NADBL;
    (*ginfo)->extra = 1;

    list = malloc((pmod->list[0] + 1) * sizeof *list);
    if (list == NULL) {
	clear_datainfo(*ginfo, CLEAR_FULL);
	free(*ginfo);
	return NADBL;
    }

#ifdef PDEBUG
    fprintf(stderr, "gmv: *groupZ=%p\n", (void *) *groupZ);
#endif

    list[0] = pmod->list[0];
    list[list[0]] = 0;
    for (j=1; j<list[0]; j++) { /* the variables */
	list[j] = j;
	start = 0;
	for (i=0; i<nunits; i++) { /* the observations */
	    xx = 0.0;
	    if (pdinfo->time_series == 2) {
		for (t=start; t<start+T; t++) 
		    xx += Z[pmod->list[j]][t];
		start += T;
	    } else {
		for (t=start; t<pdinfo->n; t += nunits) 
		    xx += Z[pmod->list[j]][t];
		start++;
	    }
	    xx /= (double) T;
	    (*groupZ)[j][i] = xx;
	}
    }

#ifdef PDEBUG
    fprintf(stderr, "group_means_variance: about to run OLS\n");
    printlist(list, NULL);
    fprintf(stderr, "*groupZ=%p, ginfo=%p\n", (void *)*groupZ, (void *)*ginfo);
#endif

    meanmod = lsq(list, groupZ, *ginfo, OLS, 0, 0.0);
#ifdef PDEBUG
    fprintf(stderr, "gmv: lsq errcode was %d\n", meanmod.errcode);
#endif
    if (meanmod.errcode) xx = NADBL;
    else xx = meanmod.sigma * meanmod.sigma;

    clear_model(&meanmod, NULL);
    free(list);
#ifdef PDEBUG
    fprintf(stderr, "gmv: done freeing stuff\n");
#endif

    return xx;
}

/* .................................................................. */

static void vcv_slopes (hausman_t *haus, MODEL *pmod, int nunits, 
			int subt)
{
    int i, j, k = 0, min = 0, max;

    for (j=0; j<haus->ns; j++) {
	max = min + haus->ns - j;
	for (i=min; i<max; i++) {
	    /*  printf("vcv[%d] = %g\n", k, pmod->vcv[i]); */
	    if (subt) haus->sigma[k++] -= pmod->vcv[i];
	    else haus->sigma[k++] = pmod->vcv[i];
	}
	if (subt) min = max + 1;
	else min = max + nunits;
    }
}

/* .......................................................... */

#define TINY 1.0e-20

static int lu_decomp (double **a, int n, int *idx)
{
    int i, j, k, imax = 0;
    double big, dum, sum, tmp;
    double *xx;

    xx = malloc((n + 1) * sizeof *xx);
    if (xx == NULL) return 1;
    for (i=0; i<=n; i++) xx[i] = 1.0;

    for (i=1; i<=n; i++) {
	big = 0.0;
	for (j=1; j<=n; j++) 
	    if ((tmp = fabs(a[i][j])) > big) big = tmp;
	if (floateq(big, 0.0)) {
	    free(xx);
	    return 1;
	}
	xx[i] = 1.0/big;
    }
    for (j=1; j<=n; j++) {
	for (i=1; i<j; i++) {
	    sum = a[i][j];
	    for (k=1; k<i; k++) sum -= a[i][k] * a[k][j];
	    a[i][j] = sum;
	}
	big = 0.0;
	for (i=j; i<=n; i++) {
	    sum = a[i][j];
	    for (k=1; k<j; k++) sum -= a[i][k] * a[k][j];
	    a[i][j] = sum;
	    if ((dum = xx[i] * fabs(sum)) >= big) {
		big = dum;
		imax = i;
	    }
	}
	if (j != imax) {
	    for (k=1; k<=n; k++) {
		dum = a[imax][k];
		a[imax][k] = a[j][k];
		a[j][k] = dum;
	    }
	    xx[imax] = xx[j];
	}
	idx[j] = imax;
	if (floateq(a[j][j], 0.0)) a[j][j] = TINY;
	if (j != n) {
	    dum = 1.0/a[j][j];
	    for (i=j+1; i<=n; i++) a[i][j] *= dum;
	}
    }
    free(xx);
    return 0;
}

/* .................................................................. */

static void lu_backsub (double **a, int n, int *idx, double *b)
{
    int i, k = 0, ip, j;
    double sum;

    for (i=1; i<=n; i++) {
	ip = idx[i];
	sum = b[ip];
	b[ip] = b[i];
	if (k) 
	    for (j=k; j<=i-1; j++) sum -= a[i][j] * b[j];
	else if (floatneq(sum, 0.0)) k = i;
	b[i] = sum;
    }
    for (i=n; i>=1; i--) {
	sum = b[i];
	for (j=i+1; j<=n; j++) sum -= a[i][j] * b[j];
	b[i] = sum / a[i][i];
    }
}

/* .................................................................. */

static double bXb (double *b, double **X, int n)
{
    int i, j;
    double row, xx = 0.0;

    for (i=1; i<=n; i++) {
	row = 0.0;
	for (j=1; j<=n; j++) row += b[j] * X[j][i];
	xx += b[i] * row;
    }
    return xx;
}

/* .................................................................. */

static int haus_invert (hausman_t *haus)
{
    double **a, **y, *col;
    int i, j, k, *idx;
    int err = 0, n = haus->ns;

    a = malloc((n + 1) * sizeof *a);
    if (a == NULL) return 1;
    for (i=1; i<=n; i++) {
	a[i] = malloc((n + 1) * sizeof **a);
	if (a[i] == NULL) return 1;
    }
    y = malloc((n + 1) * sizeof *y);
    if (y == NULL) return 1;
    for (i=1; i<=n; i++) {
	y[i] = malloc((n + 1) * sizeof **y);
	if (y[i] == NULL) return 1;
    }
    col = malloc((n + 1) * sizeof *col);
    if (col == NULL) return 1;
    idx = malloc((n + 1) * sizeof *idx);
    if (idx == NULL) return 1;

    k = 0;
    for (i=1; i<=n; i++) {
	for (j=i; j<=n; j++) {
	    a[i][j] = haus->sigma[k];
            if (i != j) 
	       a[j][i] = haus->sigma[k];
            k++;
	}
    }

    err = lu_decomp(a, n, idx);
    if (!err) {
	for (j=1; j<=n; j++) {
	    for (i=1; i<=n; i++) col[i] = 0.0;
	    col[j] = 1.0;
	    lu_backsub(a, n, idx, col);
	    for (i=1; i<=n; i++) y[i][j] = col[i];
	}
	haus->H = bXb(haus->bdiff, y, n);
    }

    for (i=1; i<=n; i++) {
	free(a[i]);
	free(y[i]);
    }
    free(a);
    free(y);
    free(col);
    free(idx);
    return err;
}

/* .................................................................. */

static double LSDV (MODEL *pmod, double ***pZ, DATAINFO *pdinfo,
		    int nunits, int T, hausman_t *haus, PRN *prn) 
{
    int i, t, oldv = pdinfo->v, start;
    int *dvlist;
    double var, F;
    MODEL lsdv;

    dvlist = malloc((pmod->list[0] + nunits) * sizeof *dvlist);
    if (dvlist == NULL) return NADBL;
    if (dataset_add_vars(nunits - 1, pZ, pdinfo)) {
	free(dvlist);
	return NADBL;
    }

    start = 0;
    for (i=0; i<nunits-1; i++) {
	for (t=0; t<pdinfo->n; t++) 
	    (*pZ)[oldv+i][t] = 0.0;
	if (pdinfo->time_series == STACKED_TIME_SERIES) {
	    for (t=start; t<start+T; t++) 
		(*pZ)[oldv+i][t] = 1.0;
	    start += T;
	} else {
	    for (t=start; t<pdinfo->n; t += nunits) 
		(*pZ)[oldv+i][t] = 1.0;
	    start++;
	}
    }

    dvlist[0] = pmod->list[0] + nunits - 1;
    for (i=1; i<=pmod->list[0]; i++) 
	dvlist[i] = pmod->list[i];
    for (i=1; i<nunits; i++) 
	dvlist[pmod->list[0] + i] = oldv + i - 1;

#ifdef PDEBUG
    fprintf(stderr, "LSDV: about to run OLS\n");
#endif

    lsdv = lsq(dvlist, pZ, pdinfo, OLS, 0, 0.0);
    if (lsdv.errcode) {
	var = NADBL;
	pprintf(prn, _("Error estimating fixed effects model\n"));
	errmsg(lsdv.errcode, prn);
    } else {
	haus->sigma_e = lsdv.sigma;
	var = lsdv.sigma * lsdv.sigma;
	pprintf(prn, 
		_("                          Fixed effects estimator\n"
		"          allows for differing intercepts by cross-sectional "
		"unit\n"
		"         (slope standard errors in parentheses, a_i = "
		"intercepts)\n\n"));
	for (i=1; i<pmod->list[0] - 1; i++) {
	    print_panel_coeff(&lsdv, &lsdv, pdinfo, i, prn);
	    haus->bdiff[i] = lsdv.coeff[i];
	} for (i=pmod->list[0]; i<=dvlist[0]; i++) {
	    char dumstr[9];

	    if (i < dvlist[0]) 
		lsdv.coeff[i-1] += lsdv.coeff[dvlist[0] - 1];
	    sprintf(dumstr, "a_%d", i - pmod->list[0] + 1);
	    pprintf(prn, "%9s: %14.4g\n", dumstr, lsdv.coeff[i-1]);
	}
	pprintf(prn, _("\nResidual variance: %g/(%d - %d) = %g\n"), 
		lsdv.ess, pdinfo->n, lsdv.ncoeff, var);
	F = (pmod->ess - lsdv.ess) * lsdv.dfd /
	    (lsdv.ess * (nunits - 1.0));
	pprintf(prn, _("Joint significance of unit dummy variables:\n"
		" F(%d, %d) = %g with p-value %g\n"), nunits - 1,
		lsdv.dfd, F, fdist(F, nunits - 1, lsdv.dfd));
	pprintf(prn, _("(A low p-value counts against the null hypothesis that "
		"the pooled OLS model\nis adequate, in favor of the fixed "
		"effects alternative.)\n\n"));
	makevcv(&lsdv);
	vcv_slopes(haus, &lsdv, nunits, 0);
    }

    clear_model(&lsdv, NULL);
    dataset_drop_vars(nunits - 1, pZ, pdinfo);
    free(dvlist);

    return var;
}

/* .................................................................. */

static int random_effects (MODEL *pmod, double **Z, DATAINFO *pdinfo, 
			   double **groupZ, double theta, int nunits, int T, 
			   hausman_t *haus, PRN *prn)
{
    double **reZ;
    DATAINFO *reinfo;
    MODEL remod;
    int *relist;
    int i, j, t, err = 0;

    reinfo = create_new_dataset(&reZ, pmod->list[0], pdinfo->n, 0);
    if (reinfo == NULL) return E_ALLOC;
    reinfo->extra = 1;

    relist = malloc((pmod->list[0] + 1) * sizeof *relist);
    if (relist == NULL) {
	free_Z(reZ, reinfo);
	clear_datainfo(reinfo, CLEAR_FULL);
	free(reinfo);
	return E_ALLOC;
    }

    relist[0] = pmod->list[0];
    relist[relist[0]] = 0;
    /* create transformed variables */
    for (i=1; i<relist[0]; i++) {
	relist[i] = i;
	j = 0;
	if (pdinfo->time_series == STACKED_TIME_SERIES) { 
	    for (t=0; t<pdinfo->n; t++) {
		if (t && (t % T == 0)) j++; 
		reZ[i][t] = Z[pmod->list[i]][t] 
		    - theta * groupZ[i][j];
	    }
	} else { /* stacked cross sections */
	    for (t=0; t<pdinfo->n; t++) {
		if (t && t % nunits == 0) j = 0; /* FIXME ?? */
		reZ[i][t] = Z[pmod->list[i]][t] 
		    - theta * groupZ[i][j];
		j++;
	    }
	}
    }
    for (t=0; t<pdinfo->n; t++) reZ[0][t] = 1.0 - theta;

#ifdef PDEBUG
    fprintf(stderr, "random_effects: about to run OLS\n");
#endif

    remod = lsq(relist, &reZ, reinfo, OLS, 0, 0.0);
    if ((err = remod.errcode)) {
	pprintf(prn, _("Error estimating random effects model\n"));
	errmsg(err, prn);
    } else {
	pprintf(prn,
		_("                         Random effects estimator\n"
		"           allows for a unit-specific component to the "
		"error term\n"
		"                     (standard errors in parentheses)\n\n"));
	print_panel_const(&remod, prn);
	for (i=1; i<relist[0] - 1; i++) {
	    print_panel_coeff(pmod, &remod, pdinfo, i, prn);
	    haus->bdiff[i] -= remod.coeff[i];
	}
	makevcv(&remod);
	vcv_slopes(haus, &remod, nunits, 1);
    }

    clear_model(&remod, NULL);
    free_Z(reZ, reinfo);
    clear_datainfo(reinfo, CLEAR_FULL);
    free(reinfo);
    free(relist);    

    return err;
}

/* .................................................................. */

int breusch_pagan_LM (MODEL *pmod, DATAINFO *pdinfo, 
		      int nunits, int T, PRN *prn)
{
    double *ubar, LM, eprime = 0.0;
    int i, t, start = 0;

    ubar = malloc(nunits * sizeof *ubar);
    if (ubar == NULL) return E_ALLOC;

    for (i=0; i<nunits; i++) {
	ubar[i] = 0.0;
	if (pdinfo->time_series == STACKED_TIME_SERIES) {
	    for (t=start; t<start+T; t++) 
		ubar[i] += pmod->uhat[t];
	    start += T;
	} else {
	    for (t=start; t<pdinfo->n; t += nunits) 
		ubar[i] += pmod->uhat[t];
	    start++;
	}
	ubar[i] /= (double) T;
	eprime += ubar[i] * ubar[i];
    }
#ifdef PDEBUG
    fprintf(stderr,  "breusch_pagan: found ubars\n");
#endif

    pprintf(prn, _("\nMeans of pooled OLS residuals for cross-sectional "
	    "units:\n\n"));
    for (i=0; i<nunits; i++) {
	pprintf(prn, _(" unit %2d: %13.5g\n"), 
		i + 1, ubar[i]);
    }
    free(ubar);

    LM = (double) pdinfo->n/(2.0*(T - 1.0)) * 
	pow((T * T * eprime/pmod->ess) - 1.0, 2);
    pprintf(prn, _("\nBreusch-Pagan test statistic:\n"
	    " LM = %g with p-value = prob(chi-square(1) > %g) = %g\n"), 
	    LM, LM, chisq(LM, 1));
    pprintf(prn, _("(A low p-value counts against the null hypothesis that "
	    "the pooled OLS model\nis adequate, in favor of the random "
	    "effects alternative.)\n\n"));
    return 0;
}

/* .................................................................. */

static int do_hausman_test (hausman_t *haus, PRN *prn)
{
#ifdef notdef
    int i, ns = haus->ns;
    int nterms = (ns * ns + ns) / 2;

    for (i=1; i<=ns; i++)
 	pprintf(prn, "b%d_FE - beta%d_RE = %g\n", i, i, haus->bdiff[i]);
    pprintf(prn, "\n");

    for (i=0; i<nterms; i++)
 	pprintf(prn, "vcv_diff[%d] = %g\n", i, haus->sigma[i]);
#endif

    if (haus_invert(haus)) { 
	pprintf(prn, _("Error attempting to invert vcv difference matrix\n"));
	return 1;
    }
    if (haus->H < 0) 
	pprintf(prn, _("\nHausman test matrix is not positive definite (this "
		"result may be treated as\n\"fail to reject\" the random effects "
		"specification).\n"));
    else {
	pprintf(prn, _("\nHausman test statistic:\n"
		" H = %g with p-value = prob(chi-square(%d) > %g) = %g\n"),
		haus->H, haus->ns, haus->H, chisq(haus->H, haus->ns));
	pprintf(prn, _("(A low p-value counts against the null hypothesis that "
		"the random effects\nmodel is consistent, in favor of the fixed "
		"effects model.)\n"));
    }

    return 0;
}

/* .................................................................. */

int panel_diagnostics (MODEL *pmod, double ***pZ, DATAINFO *pdinfo, 
		       PRN *prn)
{
    int nunits, ns, T;
    double var1, var2, theta;
    double **groupZ = NULL;
    DATAINFO *ginfo = NULL;
    hausman_t haus;

    if (get_panel_structure(pdinfo, &nunits, &T))
	return 1;

    if (nunits > pmod->ncoeff) {
	ns = haus.ns = pmod->ncoeff - 1;
	haus.bdiff = malloc(pmod->ncoeff * sizeof(double));
	if (haus.bdiff == NULL) return E_ALLOC;
	haus.sigma = malloc(((ns * ns + ns) / 2) * sizeof(double));
	if (haus.sigma == NULL) return E_ALLOC; 
    }   
    
    pprintf(prn, _("      Diagnostics: assuming a balanced panel with %d "
	    "cross-sectional units\n "
	    "                        observed over %d periods\n\n"), 
	    nunits, T);

    var2 = LSDV(pmod, pZ, pdinfo, nunits, T, &haus, prn);

#ifdef PDEBUG
    fprintf(stderr, "panel_diagnostics: LSDV gave %g\n", var2);
#endif

    breusch_pagan_LM(pmod, pdinfo, nunits, T, prn);

#ifdef PDEBUG
    fprintf(stderr, "panel_diagnostics: done breusch_pagan_LM()\n");
#endif
    
    if (nunits > pmod->ncoeff && var2 > 0) {
	var1 = group_means_variance(pmod, *pZ, pdinfo, 
				    &groupZ, &ginfo, nunits, T);
	if (var1 < 0) 
	    pprintf(prn, _("Couldn't estimate group means regression\n"));
	else {
	    pprintf(prn, _("Residual variance for group means "
		    "regression: %g\n\n"), var1);    
	    theta = 1.0 - sqrt(var2 / (T * var1));
	    random_effects(pmod, *pZ, pdinfo, groupZ, theta, nunits, T, 
			   &haus, prn);
	    do_hausman_test(&haus, prn);
	}
	free_Z(groupZ, ginfo);
	clear_datainfo(ginfo, CLEAR_FULL);
	free(ginfo);
	free(haus.bdiff);
	free(haus.sigma);
    }

    return 0;
}

/* .................................................................. */

static void panel_copy_var (double **targZ, DATAINFO *targinfo, int targv,
			    double *src, DATAINFO *srcinfo, int srcv,
			    int order)
{
    int t, j = 0;

    for (t=srcinfo->t1; t<=srcinfo->t2; t++) {
	if (t % srcinfo->pd >= order) {
	    targZ[targv][j++] = src[t];
	} 
    }

    if (srcv == -1) {
	strcpy(targinfo->varname[targv], "uhat");
	strcpy(targinfo->label[targv], _("residual"));
    } else {
	strcpy(targinfo->varname[targv], srcinfo->varname[srcv]);
	strcpy(targinfo->label[targv], srcinfo->label[srcv]);
    }
}

static void make_reduced_data_info (DATAINFO *targ, DATAINFO *src, int order)
{
    targ->pd = src->pd - order;
    ntodate(targ->stobs, src->t1 + order, src);
    targ->sd0 = obs_str_to_double(targ->stobs); 
    targ->time_series = src->time_series;
}

static void panel_lag (double **tmpZ, DATAINFO *tmpinfo, 
		       double *src, DATAINFO *srcinfo, 
		       int v, int order, int lag)
{
    int t, j = 0;

    for (t=srcinfo->t1; t<=srcinfo->t2; t++) {
	if (t % srcinfo->pd >= order) {
	    tmpZ[v][j++] = src[t - lag];
	}
    }

    sprintf(tmpinfo->varname[v], "uhat_%d", lag);
    tmpinfo->label[v][0] = 0;
}

/* - do some sanity checks
   - create a local copy of the required portion of the data set,
     skipping the obs that will be missing
   - copy in the lags of uhat
   - estimate the aux model
   - destroy the temporary data set
*/

int panel_autocorr_test (MODEL *pmod, int order, 
			 double **Z, DATAINFO *pdinfo, 
			 PRN *prn, GRETLTEST *test)
{
    int *aclist;
    double **tmpZ;
    DATAINFO *tmpinfo;
    MODEL aux;
    double trsq, LMF;
    int i, nv, nunits, nobs, err = 0;
    int sn = pdinfo->t2 - pdinfo->t1 + 1;

    /* basic checks */
    if (order <= 0) order = 1;
    if (order > pdinfo->pd - 1) return E_DF;
    if (pmod->ncoeff + order >= sn) return E_DF;

    if (pdinfo->time_series != STACKED_TIME_SERIES ||
	!balanced_panel(pdinfo)) { 
        return E_DATA;
    }

    /* get number of cross-sectional units */
    nunits = sn / pdinfo->pd;

    /* we lose "order" observations for each unit */
    nobs = sn - nunits * order;

    /* the required number of variables */
    nv = pmod->list[0] + order;

    /* create temporary reduced dataset */
    tmpinfo = create_new_dataset(&tmpZ, nv, nobs, 0);
    if (tmpinfo == NULL) return E_ALLOC;
    make_reduced_data_info(tmpinfo, pdinfo, order);

#ifdef PDEBUG
    fprintf(stderr, "Created data set, n=%d, pd=%d, vars=%d, stobs='%s'\n", 
	    tmpinfo->n, tmpinfo->pd, tmpinfo->v, tmpinfo->stobs);
#endif

    /* allocate the auxiliary regression list */
    aclist = malloc((nv + 1) * sizeof *aclist);
    if (aclist == NULL) {
	err = E_ALLOC;
    } 

    if (!err) {
	aclist[0] = pmod->list[0] + order;
	/* copy model uhat to position 1 in temp data set */
	aclist[1] = 1;
	panel_copy_var(tmpZ, tmpinfo, 1,
		       &pmod->uhat[0], pdinfo, -1,
		       order);
	/* copy across the original indep vars, making
	   the new regression list while we're at it */
	for (i=2; i<=pmod->list[0]; i++) {
	    if (pmod->list[i] == 0) { /* the constant */
		aclist[i] = 0;
	    } else {
		aclist[i] = i;
		panel_copy_var(tmpZ, tmpinfo, i, 
			       &Z[pmod->list[i]][0], pdinfo, pmod->list[i], 
			       order);
	    }
	}
    }
	
    if (!err) {
	int v = pmod->list[0] - 1;

	/* add lags of uhat to temp data set */
	for (i=1; i<=order; i++) {
	    panel_lag(tmpZ, tmpinfo, &pmod->uhat[0], 
		      pdinfo, v + i, order, i);
	    aclist[v + i + 1] = v + i;
	}
    }

    if (!err) {
	aux = lsq(aclist, &tmpZ, tmpinfo, OLS, 1, 0.0);
	err = aux.errcode;
	if (err) {
	    errmsg(aux.errcode, prn);
	}
    } 

    if (!err) {
	aux.aux = AUX_AR;
	aux.order = order;
	printmodel(&aux, tmpinfo, prn);
	trsq = aux.rsq * aux.nobs;
	LMF = (aux.rsq/(1.0 - aux.rsq)) * 
	    (aux.nobs - pmod->ncoeff - order)/order; 

	pprintf(prn, "\n%s: LMF = %f,\n", _("Test statistic"), LMF);
	pprintf(prn, "%s = P(F(%d,%d) > %g) = %.3g\n", _("with p-value"), 
		order, aux.nobs - pmod->ncoeff - order, LMF,
		fdist(LMF, order, aux.nobs - pmod->ncoeff - order));

	pprintf(prn, "\n%s: TR^2 = %f,\n", 
		_("Alternative statistic"), trsq);
	pprintf(prn, "%s = P(%s(%d) > %g) = %.3g\n\n", 	_("with p-value"), 
		_("Chi-square"), order, trsq, chisq(trsq, order));

	if (test != NULL) {
	    strcpy(test->type, N_("LM test for autocorrelation up to order %s"));
	    strcpy(test->h_0, N_("no autocorrelation"));
	    sprintf(test->param, "%d", order);
	    test->teststat = GRETL_TEST_LMF;
	    test->dfn = order;
	    test->dfd = aux.nobs - pmod->ncoeff - order;
	    test->value = LMF;
	    test->pvalue = fdist(LMF, test->dfn, test->dfd);
	}
    }

    free(aclist);
    clear_model(&aux, tmpinfo); 

    free_Z(tmpZ, tmpinfo);
    clear_datainfo(tmpinfo, CLEAR_FULL);
    free(tmpinfo);

    return err;
}

int switch_panel_orientation (double **Z, DATAINFO *pdinfo)
{
    double **tmpZ;
    int i, j, k, t, nvec;
    int nunits = pdinfo->pd;
    int nperiods = pdinfo->n / nunits;
    char **markers = NULL;

    tmpZ = malloc((pdinfo->v - 1) * sizeof *tmpZ);
    if (tmpZ == NULL) return E_ALLOC;

    /* allocate temporary data matrix */
    j = 0;
    for (i=1; i<pdinfo->v; i++) {
	if (pdinfo->vector[i]) {
	    tmpZ[j] = malloc(pdinfo->n * sizeof **tmpZ);
	    if (tmpZ[j] == NULL) {
		for (i=0; i<j; i++) free(tmpZ[i]);
		free(tmpZ);
		return E_ALLOC;
	    }
	    j++;
	} 
    }
    nvec = j;

    /* allocate marker space if relevant */
    if (pdinfo->S != NULL) {
	markers = malloc(pdinfo->n * sizeof *markers);
	if (markers != NULL) {
	    for (t=0; t<pdinfo->n; t++) {
		markers[t] = malloc(9);
		if (markers[t] == NULL) {
		    free(markers);
		    markers = NULL;
		    break;
		} else {
		    strcpy(markers[t], pdinfo->S[t]);
		}
	    }
	}
    }

    /* copy the data (vectors) across */
    j = 0;
    for (i=1; i<pdinfo->v; i++) {
	if (pdinfo->vector[i]) {
	    for (t=0; t<pdinfo->n; t++) {
		tmpZ[j][t] = Z[i][t];
	    }
	    j++;
	} 
    }

    /* copy the data back in transformed order: construct a set of
       time series for each unit in turn -- and do markers if present */
    for (k=0; k<nunits; k++) {
	j = 0;
	for (i=1; i<pdinfo->v; i++) {
	    if (!pdinfo->vector[i]) continue;
	    for (t=0; t<nperiods; t++) {
		Z[i][k * nperiods + t] = tmpZ[j][k + nunits * t];
	    }
	    j++;
	}
	if (markers != NULL) {
	    for (t=0; t<nperiods; t++) {
		strcpy(pdinfo->S[k * nperiods + t], markers[k + nunits * t]);
	    }
	}
    }

    /* change the datainfo setup */
    pdinfo->time_series = STACKED_TIME_SERIES;
    pdinfo->pd = nperiods;
    if (nperiods < 9) {
	strcpy(pdinfo->stobs, "1:1");
    } else {
	strcpy(pdinfo->stobs, "1:01");
    }
    pdinfo->sd0 = obs_str_to_double(pdinfo->stobs);
    ntodate(pdinfo->endobs, pdinfo->n - 1, pdinfo);

    /* clean up */
    for (i=0; i<nvec; i++) free(tmpZ[i]);
    free(tmpZ);
    if (markers != NULL) {
	for (t=0; t<pdinfo->n; t++) {
	    free(markers[t]);
	}
	free(markers);
    }

    return 0;
}



