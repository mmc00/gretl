/*
 * Copyright (C) 1999-2005 Allin Cottrell
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this software; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "libgretl.h"
#include "forecast.h"

#ifdef max
# undef max
#endif
#define max(x,y) (((x) > (y))? (x) : (y))

/* estimators where a simple X*b does _not_ give the
   predicted value of the dependent variable */

#define FCAST_SPECIAL(c) (c == LOGIT || \
                          c == LOGISTIC || \
                          c == NLS || \
                          c == POISSON || \
                          c == PROBIT || \
                          c == TOBIT)

#define CHECK_LAGGED_DEPVAR(c) (c != NLS && c != ARMA)

typedef struct Forecast_ Forecast;

struct Forecast_ {
    int method;       /* static, dynamic or auto */
    double *yhat;     /* array of forecast values */
    double *sderr;    /* array of forecast standard errors */
    double *eps;      /* array of estimated forecast errors */
    int *dvlags;      /* info on lagged dependent variable */
    int offset;       /* start of yhat, etc. arrays relative to true 0 */
    int t1;           /* start of forecast range */
    int t2;           /* end of forecast range */
    int model_t2;     /* end of period over which model was estimated */
};

static int 
allocate_basic_fit_resid_arrays (FITRESID *fr)
{
    fr->actual = malloc(fr->nobs * sizeof *fr->actual);
    if (fr->actual == NULL) {
	return E_ALLOC;
    }

    fr->fitted = malloc(fr->nobs * sizeof *fr->fitted);
    if (fr->fitted == NULL) {
	free(fr->actual);
	fr->actual = NULL;
	return E_ALLOC;
    }

    fr->sderr = NULL;

    return 0;
}

static int fit_resid_add_sderr (FITRESID *fr)
{
    int err = 0;

    fr->sderr = malloc(fr->nobs * sizeof *fr->sderr);

    if (fr->sderr == NULL) {
	err = E_ALLOC;
    } 
    
    return err;
}

/**
 * fit_resid_new:
 * @n: the number of observations to allow for, or 0 if this
 * information will be added later.
 *
 * Allocates a #FITRESID struct for holding fitted values and
 * residuals from a model (or out-of-sample forecasts).  If
 * @n is greater than 0 the arrays required for that number
 * of observations will be allocated.
 *
 * Returns: pointer to allocated structure, or %NULL on failure.
 */

static FITRESID *fit_resid_new (int n)
{
    FITRESID *fr = malloc(sizeof *fr);

    if (fr == NULL) {
	return NULL;
    }

    fr->model_ID = 0;
    fr->model_ci = 0;
    fr->err = 0;
    fr->t1 = 0;
    fr->t2 = 0;
    fr->nobs = 0;
    fr->real_nobs = 0;

    if (n > 0) {
	fr->nobs = n;
	if (allocate_basic_fit_resid_arrays(fr)) {
	    free(fr);
	    fr = NULL;
	} 
    } else {
	fr->actual = NULL;
	fr->fitted = NULL;
	fr->sderr = NULL;
    } 
    
    return fr;
}

/**
 * free_fit_resid:
 * @fr: the pointer to be freed.
 *
 * Frees all resources associated with @fr, then frees the pointer
 * itself.
 */

void free_fit_resid (FITRESID *fr)
{
    free(fr->actual);
    free(fr->fitted);
    free(fr->sderr);
    free(fr);
}

/**
 * get_fit_resid:
 * @pmod: the model for which actual and fitted values
 * are wanted.
 * @Z: data array using which @pmod was estimated.
 * @pdinfo: dataset information.
 *
 * Allocates a #FITRESID structure and fills it out with
 * the actual and predicted values of the dependent variable
 * in @pmod.
 *
 * Returns: pointer to allocated structure, or %NULL on failure.
 */

FITRESID *get_fit_resid (const MODEL *pmod, const double **Z, 
			 const DATAINFO *pdinfo)
{
    int depvar, t, ft;
    FITRESID *fr;

    depvar = gretl_model_get_depvar(pmod);

    fr = fit_resid_new(pmod->t2 - pmod->t1 + 1);
    if (fr == NULL) {
	return NULL;
    }

    if (LIMDEP(pmod->ci)) {
	fr->sigma = NADBL;
    } else {
	fr->sigma = pmod->sigma;
    }

    fr->t1 = pmod->t1;
    fr->t2 = pmod->t2;
    fr->real_nobs = pmod->nobs;

    for (t=fr->t1; t<=fr->t2; t++) {
	ft = t - fr->t1;
	fr->actual[ft] = Z[depvar][t];
	fr->fitted[ft] = pmod->yhat[t];
    }

    if (gretl_isdummy(0, fr->nobs, fr->actual) > 0) {
	fr->pmax = get_precision(fr->fitted, fr->nobs, 8);
    } else {
	fr->pmax = get_precision(fr->actual, fr->nobs, 8);
    }
    
    strcpy(fr->depvar, pdinfo->varname[depvar]);
    
    return fr;
}

static int 
has_depvar_lags (const MODEL *pmod, const DATAINFO *pdinfo)
{
    char xname[9], tmp[9];  
    const char *label;
    const char *yname;
    int i, vi, lag;
    int ret = 0;

    if (pmod->ci == NLS || pmod->ci == ARMA) {
	/* we won't do this for these models */
	return 0;
    }

    yname = pdinfo->varname[gretl_model_get_depvar(pmod)];

    for (i=2; i<=pmod->list[0]; i++) {
	vi = pmod->list[i];
	if (vi == LISTSEP) {
	    /* two-stage least squares */
	    break;
	}
	label = VARLABEL(pdinfo, vi);
	if ((sscanf(label, "= %8[^(](t - %d)", xname, &lag) == 2 ||
	     sscanf(label, "%8[^=]=%8[^(](-%d)", tmp, xname, &lag) == 3) &&
	    !strcmp(xname, yname)) {
	    ret = 1;
	    break;
	}
    }

    return ret;
}

/* Make a list to keep track of any "independent variables" that are
   really lags of the dependent variable.  The list has as many
   elements as the model has coefficients, and in each place we either
   write a zero (if the coefficient does not correspond to a lag of
   the dependent variable) or a positive integer corresponding to the
   lag order.
*/

static int process_lagged_depvar (const MODEL *pmod, 
				  const DATAINFO *pdinfo,
				  int **depvar_lags)
{
    int *dvlags = NULL;
    int anylags;
    int err = 0;

    anylags = has_depvar_lags(pmod, pdinfo);

    if (!anylags) {
	*depvar_lags = NULL;
	return 0;
    }

    dvlags = malloc(pmod->ncoeff * sizeof *dvlags);
    if (dvlags == NULL) {
	err = 1;
    } else {
	char xname[9], tmp[9];
	const char *label;
	const char *yname;
	int i, vi, lag;

	yname = pdinfo->varname[gretl_model_get_depvar(pmod)];

	for (i=0; i<pmod->ncoeff; i++) {
	    vi = pmod->list[i+2];
	    if (vi == LISTSEP) {
		break;
	    }
	    label = VARLABEL(pdinfo, vi);
	    if ((sscanf(label, "= %8[^(](t - %d)", xname, &lag) == 2 ||
		 sscanf(label, "%8[^=]=%8[^(](-%d)", tmp, xname, &lag) == 3) &&
		!strcmp(xname, yname)) {
		dvlags[i] = lag;
	    } else {
		dvlags[i] = 0;
	    }
	}
    } 

    *depvar_lags = dvlags;

    return err;
}

/* initialize FITRESID struct based on model and string giving
   starting and ending observations */

static int 
fit_resid_init (const char *line, const MODEL *pmod, 
		const DATAINFO *pdinfo, FITRESID *fr)
{
    char t1str[OBSLEN], t2str[OBSLEN];

    if (!strncmp(line, "fcasterr", 8)) {
	line += 9;
    }

    /* parse the date strings giving the limits of the forecast
       range */
    if (sscanf(line, "%10s %10s", t1str, t2str) != 2) {
	fr->err = E_OBS;
    }

    if (!fr->err) {
	fr->t1 = dateton(t1str, pdinfo);
	fr->t2 = dateton(t2str, pdinfo);

	if (fr->t1 < 0 || fr->t2 < 0 || fr->t2 <= fr->t1) {
	    fr->err = E_OBS;
	}
    }

    if (!fr->err) {
	fr->nobs = fr->t2 - fr->t1 + 1;
	fr->err = allocate_basic_fit_resid_arrays(fr);
    }

    fr->model_ID = pmod->ID;
    fr->model_ci = pmod->ci;

    return fr->err;
}

#define AR_DEBUG 0

/* Get a value for a lag of the dependent variable.  If method is
   dynamic we prefer lagged prediction to lagged actual.  If method is
   static, we don't want the lagged prediction, only the actual.  If
   method is "auto", which we prefer depends on whether we're in or
   out of sample (actual within, lagged prediction without).
*/

static double fcast_get_ldv (Forecast *fc, int i, int t, int lag,
			     const double **Z)
{
    /* initialize to actual lagged value */
    double ldv = Z[i][t];

#if AR_DEBUG
    fprintf(stderr, "fcast_get_ldv: i=%d, t=%d, lag=%d; "
	    "initial ldv = Z[%d][%d] = %g\n", i, t, lag, i, t, ldv);
#endif

    if (fc->method != FC_STATIC) {
	int yht = t - fc->offset - lag;

	if (fc->method == FC_DYNAMIC) {
	    if (lag < t - fc->t1) {
		ldv = fc->yhat[yht];
	    }
	} else if (fc->method == FC_AUTO && yht >= 0) {
	    if (t > fc->model_t2 || na(ldv)) {
		ldv = fc->yhat[yht];
#if AR_DEBUG
		fprintf(stderr, "fcast_get_ldv: reset ldv = yhat[%d] = %g\n",
			yht, ldv);
#endif
	    } 
	}
    }

    return ldv;
}

/* Get forecasts, plus standard errors for same, for models without
   autoregressive errors and without "special requirements"
   (e.g. nonlinearity).  The forecast standard errors include both
   uncertainty over the error process and parameter uncertainty
   (Davidson and MacKinnon method).
*/

static int
static_fcast_with_errs (Forecast *fc, MODEL *pmod, 
			const double **Z, const DATAINFO *pdinfo) 
{
    gretl_matrix *V = NULL;
    gretl_vector *Xs = NULL;
    gretl_vector *b = NULL;

    double s2 = pmod->sigma * pmod->sigma;
    int k = pmod->ncoeff;
    int i, vi, s, t;
    int err = 0;

    V = gretl_vcv_matrix_from_model(pmod, NULL);
    if (V == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    Xs = gretl_vector_alloc(k);
    if (Xs == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    b = gretl_coeff_vector_from_model(pmod, NULL);
    if (b == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    for (t=fc->t1; t<=fc->t2 && !err; t++) {
	int missing = 0;
	double vyh;

	s = t - fc->offset;

	/* skip if we can't compute forecast */
	if (t >= pmod->t1 && t <= pmod->t2) {
	    missing = na(pmod->yhat[t]);
	}   

	/* populate Xs vector for observation */
	for (i=0; i<k && !missing; i++) {
	    double xval;

	    vi = pmod->list[i + 2];
	    xval = Z[vi][t];
	    if (na(xval)) {
		fc->sderr[s] = fc->yhat[s] = NADBL;
		missing = 1;
	    } else {
		gretl_vector_set(Xs, i, xval);
	    }
	}

	if (missing) {
	    fc->sderr[s] = fc->yhat[s] = NADBL;
	    continue;
	}

	/* forecast value */
	fc->yhat[s] = gretl_matrix_dot_product(Xs, GRETL_MOD_NONE,
					       b, GRETL_MOD_NONE,
					       NULL);

	/* forecast variance */
	vyh = gretl_scalar_b_X_b_prime(Xs, V, NULL);
	if (na(vyh)) {
	    err = 1;
	} else {
	    vyh += s2;
	    if (vyh >= 0.0) {
		fc->sderr[s] = sqrt(vyh);
	    } else {
		fc->sderr[s] = NADBL;
		err = 1;
	    }
	}
    }

 bailout:

    gretl_matrix_free(V);
    gretl_vector_free(Xs);
    gretl_vector_free(b);

    return err;
}

/* Generate forecasts from nonlinear least squares model, using
   the string specification of the regression function that
   was saved as data on the model (see nls.c).  For now we don't
   attempt to calculate forecast error variance.
*/

static int nls_fcast (Forecast *fc, const MODEL *pmod, 
		      double ***pZ, DATAINFO *pdinfo)
{
    int oldt1 = pdinfo->t1;
    int oldt2 = pdinfo->t2;
    const char *nlfunc;    
    char formula[MAXLINE];
    int t, oldv = pdinfo->v;
    int err = 0;

    nlfunc = gretl_model_get_data(pmod, "nl_regfunc");
    if (nlfunc == NULL) {
	err = E_DATA;
    }

    if (!err) {
	pdinfo->t1 = fc->t1;
	pdinfo->t2 = fc->t2;
	sprintf(formula, "$nl_y = %s", nlfunc);
	err = generate(formula, pZ, pdinfo, NULL, OPT_P);
    }

    if (!err) {
	/* transcribe values from last generated var to target */
	for (t=fc->t1; t<=fc->t2; t++) {
	    fc->yhat[t - fc->offset] = (*pZ)[oldv][t];
	}
	err = dataset_drop_last_variables(pdinfo->v - oldv, pZ, pdinfo);
    }

    pdinfo->t1 = oldt1;
    pdinfo->t2 = oldt2;

    return err;
}

#define ARF_DEBUG 0

#if ARF_DEBUG
# include <stdarg.h>
static void dprintf (const char *format, ...)
{
   va_list args;

   va_start(args, format);
   vfprintf(stderr, format, args);
   va_end(args);

   return;
}
# define DPRINTF(x) dprintf x
#else 
# define DPRINTF(x)
#endif 

#define depvar_lag(f,i) ((f->dvlags != NULL)? f->dvlags[i] : 0)

/* forecasts for GARCH models -- seems as if we ought to be able to
   do something interesting with forecast error variance, but right
   now we don't do anything */

static int garch_fcast (Forecast *fc, const MODEL *pmod, 
			const double **Z, const DATAINFO *pdinfo)
{
    double xval;
    int xvars, yno;
    int *xlist = NULL;
    int i, v, s, t;

    xlist = gretl_arma_model_get_x_list(pmod);
    yno = gretl_model_get_depvar(pmod);

    if (xlist != NULL) {
	xvars = xlist[0];
    } else {
	xvars = 0;
    }

    for (t=fc->t1; t<=fc->t2; t++) {
	int lag, miss = 0;
	double yh = 0.0;

	s = t - fc->offset;

	if (fc->method != FC_DYNAMIC && 
	    t >= pmod->t1 && t <= pmod->t2) {
	    fc->yhat[s] = pmod->yhat[t];
	    continue;
	}

	for (i=0; i<xvars; i++) {
	    v = xlist[i+1];
	    if ((lag = depvar_lag(fc, i))) {
		xval = fcast_get_ldv(fc, i, t, lag, Z);
	    } else {
		xval = Z[v][t];
	    }
	    if (na(xval)) {
		miss = 1;
	    } else {
		yh += pmod->coeff[i-1] * xval;
	    }
	}

	if (miss) {
	    fc->yhat[s] = NADBL;
	} else {
	    fc->yhat[s] = yh;
	}
    }

    free(xlist);

    return 0;
}

/* Compute ARMA forecast error variance (ignoring parameter
   uncertainty, as is common), via recursion. 
*/

static double 
arma_variance_machine (const double *phi, int p, 
		       const double *theta, int q,
		       double *psi, int s, double *ss_psi)
{
    int i, h = s - 1;

    /* s = number of steps ahead of forecast baseline
       h = index into "psi" array of infinite-order MA
           coefficients
    */

    if (h > p) {
	/* we don't retain more that (AR order + 1) 
	   psi values (the last one being used as
           workspace) */
	h = p;
    }

    if (s == 1) {
	/* one step ahead: initialize to unity */
	psi[h] = 1.0;
    } else {
	/* init to zero prior to adding components */
	psi[h] = 0.0;
    }

    /* add AR-derived psi[h] components */
    for (i=1; i<=p && i<s; i++) {
	psi[h] += phi[i-1] * psi[h-i];
    }

    /* add MA-derived psi[h] components */
    if (s > 1 && s <= q+1) {
	psi[h] += theta[s-2];
    }

    /* increment running sum of psi squared terms */
    *ss_psi += psi[h] * psi[h];
    
    if (s > p) {
	/* drop the oldest psi and make space for a new one */
	for (i=0; i<p; i++) {
	    psi[i] = psi[i+1];
	}
    }

    return *ss_psi;
}

/* generate forecasts for ARMA (or ARMAX) models, including
   forecast standard errors if we're doing out-of-sample
   forecasting
*/

static int arma_fcast (Forecast *fc, const MODEL *pmod, 
		       const double **Z, const DATAINFO *pdinfo)
{
    const double *phi;
    const double *theta;
    const double *beta;

    double *psi = NULL;
    double ss_psi = 0.0;
    double xval, yval;
    int xvars, yno;
    int *xlist = NULL;
    int p, q;
    int tstart = fc->t1;
    int ar_smax, ma_smax;
    int i, s, t, tt;
    int err = 0;

    DPRINTF(("\n\n*** arma_fcast: METHOD = %d\n", fc->method));

    if (fc->method != FC_DYNAMIC) {
	/* use pre-calculated fitted values over model estimation range,
	   and don't bother calculating forecast error variance */
	for (t=fc->t1; t<=pmod->t2; t++) {
	    tt = t - fc->offset;
	    fc->yhat[tt] = pmod->yhat[t];
	    if (fc->sderr != NULL) {
		fc->sderr[tt] = NADBL;
	    }
	}
	if (fc->t2 <= pmod->t2) {
	    /* no "real" forecasts were called for, we're done */
	    return 0;
	}
	tstart = pmod->t2 + 1;
    }

    p = gretl_arma_model_get_AR_order(pmod);
    q = gretl_arma_model_get_MA_order(pmod);
    xlist = gretl_arma_model_get_x_list(pmod);
    yno = gretl_model_get_depvar(pmod);

    DPRINTF(("forecasting variable %d (%s), obs %d to %d\n", yno, 
	     pdinfo->varname[yno], fc->t1, fc->t2));

    if (xlist != NULL) {
	xvars = xlist[0];
    } else {
	xvars = 0;
    }

    phi = pmod->coeff + pmod->ifc; /* AR coeffs */
    theta = phi + p;               /* MA coeffs */
    beta = theta + q;              /* coeffs on indep vars */

    /* setup for forecast error variance */
    if (fc->sderr != NULL) {
	psi = malloc((p + 1) * sizeof *psi);
    }

    /* cut-off points for using actual rather than forecast
       values of y in generating further forecasts */
    if (fc->method == FC_STATIC) {
	ar_smax = fc->t2;
	ma_smax = pmod->t2;
    } else if (fc->method == FC_DYNAMIC) {
	ar_smax = max(fc->t1 - 1, p);
	ma_smax = max(fc->t1 - 1, q);
    } else {
	ar_smax = pmod->t2;
	ma_smax = pmod->t2;
    }

    DPRINTF(("ar_smax = %d, ma_smax = %d\n", ar_smax, ma_smax));

    /* do real forecast */
    for (t=tstart; t<=fc->t2 && !err; t++) {
	int miss = 0;
	double yh = 0.0;

	tt = t - fc->offset;

	DPRINTF(("\n *** Doing forecast for obs %d\n", t));

	/* contribution of independent variables */
	for (i=1; i<=xvars; i++) {
	    int j = 0;

	    if (xlist[i] == 0) {
		yh += pmod->coeff[0];
	    } else {
		xval = Z[xlist[i]][t];
		if (na(xval)) {
		    miss = 1;
		} else {
		    yh += beta[j++] * xval;
		}
	    }
	}

	DPRINTF((" x contribution = %g\n", yh));

	/* AR contribution */
	for (i=0; i<p && !miss; i++) {
	    s = t - i - 1;
	    if (s < 0) {
		yval = NADBL;
	    } else if (s <= ar_smax) {
		yval = Z[yno][s];
		DPRINTF(("  AR: lag %d, y[%d] = %g\n", i+1, s, yval));
	    } else {
		yval = fc->yhat[s - fc->offset];
		DPRINTF(("  AR: lag %d, yhat[%d] = %g\n", i+1, s - fc->offset, yval));
	    }
	    if (na(yval)) {
		DPRINTF(("  AR: lag %d, s =%d, missing value\n", i+1, s));
		miss = 1;
	    } else {
		DPRINTF(("  AR: lag %d, s=%d, using coeff %g\n", i+1, s, phi[i]));
		yh += phi[i] * yval;
	    }
	}

	DPRINTF((" with AR contribution: %g\n", yh));

	/* MA contribution */
	for (i=0; i<q && !miss; i++) {
	    s = t - i - 1;
	    if (s >= pmod->t1 && s <= ma_smax) {
		DPRINTF(("  MA: lag %d, e[%d] = %g, coeff %g\n", i+1, s, 
			 pmod->uhat[s], theta[i]));
		yh += theta[i] * pmod->uhat[s];
	    } else if (fc->eps != NULL) {
		DPRINTF(("  MA: lag %d, ehat[%d] = %g, coeff %g\n", i+1, s, 
			 fc->eps[s - fc->offset], theta[i]));
		yh += theta[i] * fc->eps[s - fc->offset];
	    }
	}

	DPRINTF((" with MA contribution: %g\n", yh));

	if (miss) {
	    fc->yhat[tt] = NADBL;
	} else {
	    fc->yhat[tt] = yh;
	}

	if (fc->eps != NULL && !miss) {
	    /* form estimated error in case of static forecast */
	    fc->eps[tt] = Z[yno][t] - fc->yhat[tt];
	}

	/* forecast error variance */
	if (psi != NULL) {
	    arma_variance_machine(phi, p, theta, q,
				  psi, t - tstart + 1, 
				  &ss_psi);
	    fc->sderr[tt] = pmod->sigma * sqrt(ss_psi);
	}

	if (miss && t >= p) {
	    DPRINTF(("aborting with NA at t=%d (p=%d)\n", t, p));
	    err = 1;
	}
    }

    free(xlist);

    if (psi != NULL) {
	free(psi);
    }

    return err;
}

/* construct the "phi" array of AR coefficients, based on the
   ARINFO that was added to the model at estimation time.
   The latter's rho member may be a compacted array, with
   zero elements omitted, but here we need a full-length
   array with zeros inserted as required */

static double *make_phi_from_arinfo (const ARINFO *arinfo, int pmax)
{
    double *phi = malloc(pmax * sizeof *phi);

    if (phi != NULL) {
	int i, lag;

	for (i=0; i<pmax; i++) {
	    phi[i] = 0.0;
	}

	for (i=1; i<=arinfo->arlist[0]; i++) {
	    lag = arinfo->arlist[i];
	    phi[lag-1] = arinfo->rho[i-1];
	}
    }

    return phi;
}

/* determine the highest lag order for AR model, either
   via AR in the error term or via inclusion of lagged
   dependent variable */

static int 
get_max_ar_lag (Forecast *fc, const MODEL *pmod, int p)
{
    int i, pmax = p; /* AR error order */

    if (fc->dvlags != NULL) {
	for (i=0; i<pmod->ncoeff; i++) {
	    if (fc->dvlags[i] > pmax) {
		pmax = fc->dvlags[i];
	    }
	}
    }

    return pmax;
}

/* Set things up for computing forecast error variance for ar models
   (AR, CORC, HILU, PWE).  This is complicated by the fact that there
   may be a lagged dependent variable in the picture.  If there is,
   the effective AR coefficients have to be incremented, for the
   purposes of calculating forecast variance.  But I'm not sure this
   is quite right yet.
*/

static void set_up_ar_fcast_variance (const MODEL *pmod, int pmax,
				      double **phi, double **psi,
				      double **errphi)
{
    *errphi = make_phi_from_arinfo(pmod->arinfo, pmax);

    if (*errphi != NULL) {
	*psi = malloc((pmax + 1) * sizeof **psi);
	if (*psi == NULL) {
	    free(*errphi);
	    *errphi = NULL;
	} else {
	    *phi = malloc(pmax * sizeof **phi);
	    if (*phi == NULL) {
		free(*errphi);
		*errphi = NULL;
		free(*psi);
		*psi = NULL;
	    }
	}
    } 
}

/* 
   The code below generates forecasts that incorporate the 
   predictable portion of an AR error term:

       u_t = r1 u_{t-1} + r2 u_{t-1} + ... + e_t

   where e_t is white noise.  The forecasts are based on the
   representation of a model with such an error term as

       (1 - r(L)) y_t = (1 - r(L)) X_t b + e_t

   or

       y_t = r(L) y_t + (1 - r(L)) X_t b + e_t

   where r(L) is a polynomial in the lag operator.

   We also attempt to calculate forecast error variance for
   out-of-sample forecasts.  These calculations, like those for
   ARMA, do not take into account parameter uncertainty.

   This code is used for AR, CORC, HILU and PWE models.
*/

static int ar_fcast (Forecast *fc, const MODEL *pmod, 
		     const double **Z, const DATAINFO *pdinfo)
{
    const int *arlist;
    double *phi = NULL;
    double *psi = NULL;
    double *errphi = NULL;
    double ss_psi = 0.0;
    double xval, yh;
    double rk, ylag, xlag;
    int miss, yno;
    int i, k, v, s, t, tk;
    int p, dvlag, pmax = 0;
    int err = 0;

#if AR_DEBUG
    fprintf(stderr, "\n*** arma_fcast, method = %d\n\n", fc->method);
#endif

    yno = pmod->list[1];
    arlist = pmod->arinfo->arlist;
    p = arlist[arlist[0]]; /* AR order of error term */

    if (fc->t2 > pmod->t2 && fc->sderr != NULL) {
	/* we compute variance only if we're forecasting
	   out of sample */
	pmax = get_max_ar_lag(fc, pmod, p);
	set_up_ar_fcast_variance(pmod, pmax, &phi, &psi, &errphi);
    }

    for (t=fc->t1; t<=fc->t2; t++) {
	miss = 0;
	yh = 0.0;
	s = t - fc->offset;

	if (t < p) {
	    fc->yhat[s] = NADBL;
	    if (fc->sderr != NULL) {
		fc->sderr[s] = NADBL;
	    }
	    continue;
	}

        if (pmod->ci == PWE && t == pmod->t1) {
            /* PWE first obs is special */
            fc->yhat[s] = pmod->yhat[t];
            continue;
        }

	if (phi != NULL) {
	    /* initialize the phi's based on the AR error process
	       alone */
	    for (i=0; i<pmax; i++) {
		phi[i] = errphi[i];
	    }
	}

	/* r(L) y_t */
	for (k=1; k<=arlist[0]; k++) {
	    rk = pmod->arinfo->rho[k-1];
	    tk = t - arlist[k];
	    ylag = fcast_get_ldv(fc, yno, tk, 0, Z);
	    if (na(ylag)) {
		miss = 1;
	    } else {
		yh += rk * ylag;
	    }
	}

	/* (1 - r(L)) X_t b */
	for (i=0; i<pmod->ncoeff && !miss; i++) {
	    v = pmod->list[i+2];
	    if ((dvlag = depvar_lag(fc, i))) {
		xval = fcast_get_ldv(fc, v, t, dvlag, Z);
	    } else {
		xval = Z[v][t];
	    }
	    if (na(xval)) {
		miss = 1;
	    } else {
		if (dvlag > 0 && phi != NULL) {
		    /* augment phi for computation of variance */
		    phi[dvlag - 1] += pmod->coeff[i];
		}
		for (k=1; k<=arlist[0]; k++) {
		    rk = pmod->arinfo->rho[k-1];
		    tk = t - arlist[k];
		    if (dvlag > 0) {
			xlag = fcast_get_ldv(fc, v, tk, dvlag, Z);
		    } else {
			xlag = Z[v][tk];
		    }
		    if (!na(xlag)) {
			xval -= rk * xlag;
		    }
		}
		yh += xval * pmod->coeff[i];
	    }
	}

	if (miss) {
	    fc->yhat[s] = NADBL;
	} else {
	    fc->yhat[s] = yh;
	}

	/* forecast error variance */
	if (phi != NULL) {
	    if (t > pmod->t2) {
		arma_variance_machine(phi, pmax, NULL, 0,
				      psi, t - pmod->t2, 
				      &ss_psi);
		fc->sderr[s] = pmod->sigma * sqrt(ss_psi);
	    } else {
		fc->sderr[s] = NADBL;
	    }
	}
    }

    if (psi != NULL) {
	free(phi);
	free(psi);
	free(errphi);
    }

    return err;
}

/* Calculates the transformation required to get from xb (= X*b) to
   the actual prediction for the dependent variable, for models of
   type LOGISTIC, LOGIT, PROBIT, TOBIT and POISSON.
 */

static double fcast_transform (double xb, int ci, int t, 
			       const double *offset,
			       double lmax)
{
    double yf = xb;

    if (ci == TOBIT) {
	if (xb < 0.0) {
	    yf = 0.0;
	}
    } else if (ci == LOGIT) {
	yf = exp(xb) / (1.0 + exp(xb));
    } else if (ci == PROBIT) {
	yf = normal_cdf(xb);
    } else if (ci == LOGISTIC) {
	if (na(lmax)) {
	    yf = 1.0 / (1.0 + exp(-xb));
	} else {
	    yf = lmax / (1.0 + exp(-xb));
	}
    } else if (ci == POISSON) {
	if (offset != NULL) {
	    if (na(offset[t])) {
		yf = NADBL;
	    } else {
		yf = exp(xb + log(offset[t]));
	    }
	} else {
	    yf = exp(xb);
	}
    }

    return yf;
}

/* compute forecasts for linear models without autoregressive errors,
   version without computation of forecast standard errors
*/

static int linear_fcast (Forecast *fc, const MODEL *pmod, 
			 const double **Z, const DATAINFO *pdinfo)
{
    const double *offvar = NULL;
    double lmax = NADBL;
    double xval;
    int i, vi, t, s;

    if (pmod->ci == POISSON) {
	/* special for poisson "offset" variable */
	int offnum = gretl_model_get_int(pmod, "offset_var");

	if (offnum > 0) {
	    offvar = Z[offnum];
	}
    } else if (pmod->ci == LOGISTIC) {
	lmax = gretl_model_get_double(pmod, "lmax");
    }

    for (t=fc->t1; t<=fc->t2; t++) {
	int miss = 0;
	double yh = 0.0;

	s = t - fc->offset;

	for (i=0; i<pmod->ncoeff && !miss; i++) {
	    int lag;

	    vi = pmod->list[i+2];
	    if ((lag = depvar_lag(fc, i))) {
		xval = fcast_get_ldv(fc, vi, t, lag, Z);
	    } else {
		xval = Z[vi][t];
	    }
	    if (na(xval)) {
		miss = 1;
	    } else {
		yh += xval * pmod->coeff[i];
	    }
	}

	if (miss) {
	    fc->yhat[s] = NADBL;
	} else if (FCAST_SPECIAL(pmod->ci)) {
	    /* special handling for LOGIT and others */
	    fc->yhat[s] = fcast_transform(yh, pmod->ci, t, offvar, lmax);
	} else {
	    fc->yhat[s] = yh;
	}
    }

    return 0;
}

static int get_forecast_method (Forecast *fc,
				const MODEL *pmod, 
				const DATAINFO *pdinfo,
				gretlopt opt)
{
    int dyn_ok = 0;
    int dyn_errs_ok = 0;
    int err = 0;

    fc->dvlags = NULL;
    fc->method = FC_STATIC;

    if ((opt & OPT_D) && (opt & OPT_S)) {
	/* conflicting options: remove them */
	fputs("got conflicting options, static and dynamic\n", stderr);
	opt &= ~OPT_D;
	opt &= ~OPT_S;
	err = 1;
    }

    /* do setup for possible lags of the dependent variable,
       unless OPT_S for "static" has been given */
    if (dataset_is_time_series(pdinfo) && !(opt & OPT_S)) {
	process_lagged_depvar(pmod, pdinfo, &fc->dvlags);
    }

    if (!(opt & OPT_S) && (pmod->ci == ARMA || fc->dvlags != NULL)) {
	/* dynamic forecast is possible, and not ruled out by 
	   "static" option */
	dyn_ok = 1;
    }

    if (SIMPLE_AR_MODEL(pmod->ci) && !(opt & OPT_S)) {
	dyn_errs_ok = 1;
    }    

    if (!dyn_ok && (opt & OPT_D)) {
	/* "dynamic" option given, but can't be honored */
	fputs("requested dynamic option, but it is not applicable\n", stderr);
	opt &= ~OPT_D;
	err = 1;
    }

    if (opt & OPT_D) {
	/* user requested dynamic forecast and it seems OK */
	fc->method = FC_DYNAMIC;
    } else if ((dyn_ok || dyn_errs_ok) && fc->t2 > pmod->t2) {
	/* do dynamic f'cast out of sample */
	fc->method = FC_AUTO;
    }

    return err;
}

/* driver for various functions that compute forecasts
   for different sorts of models */

static int real_get_fcast (FITRESID *fr, MODEL *pmod, 
			   double ***pZ, DATAINFO *pdinfo,
			   gretlopt opt) 
{
    Forecast fc;
    const double **Z = (const double **) *pZ;
    int yno = gretl_model_get_depvar(pmod);
    int DM_errs = 0;
    int dyn_errs = 0;
    int nf = 0;
    int s, t, err = 0;

    fc.t1 = fr->t1;
    fc.t2 = fr->t2;
    fc.offset = fr->t1;
    fc.model_t2 = pmod->t2;
    fc.eps = NULL;

    get_forecast_method(&fc, pmod, pdinfo, opt);

    if (!FCAST_SPECIAL(pmod->ci) && pmod->ci != GARCH) {
	if (pmod->ci != ARMA && !SIMPLE_AR_MODEL(pmod->ci) && fc.dvlags == NULL) {
	    /* we'll do Davidson-MacKinnon error variance */
	    DM_errs = 1;
	} else if (fc.method == FC_DYNAMIC) {
	    /* we'll do dynamic forecast errors throughout */
	    dyn_errs = 1;
	} else if (fc.method == FC_AUTO && fc.t2 > pmod->t2) {
	    /* do dynamic forecast errors out of sample */
	    dyn_errs = 1;
	}
    }

    /* bodge: for now we don't actually handle dynamic forecast 
       standard errors for other than AR and ARMA */
    if (pmod->ci != ARMA && !SIMPLE_AR_MODEL(pmod->ci)) {
	dyn_errs = 0;
    }

    if (DM_errs || dyn_errs) {
	err = fit_resid_add_sderr(fr);
    }

    if (err) {
	return err;
    }

    fc.yhat = fr->fitted;
    fc.sderr = fr->sderr;

    if (pmod->ci == ARMA && fc.method == FC_STATIC) {
	fc.eps = malloc(fr->nobs * sizeof *fc.eps);
    }

    if (DM_errs) {
	err = static_fcast_with_errs(&fc, pmod, Z, pdinfo);
    } else if (pmod->ci == NLS) {
	err = nls_fcast(&fc, pmod, pZ, pdinfo);
    } else if (SIMPLE_AR_MODEL(pmod->ci)) {
	err = ar_fcast(&fc, pmod, Z, pdinfo);
    } else if (pmod->ci == ARMA) {
	err = arma_fcast(&fc, pmod, Z, pdinfo);
    } else if (pmod->ci == GARCH) {
	err = garch_fcast(&fc, pmod, Z, pdinfo);
    } else {
	err = linear_fcast(&fc, pmod, Z, pdinfo);
    }

    if (fc.dvlags != NULL) {
	free(fc.dvlags);
    }
    if (fc.eps != NULL) {
	free(fc.eps);
    }

    for (t=fr->t1; t<=fr->t2; t++) {
	s = t - fr->t1;
	if (!na(fr->fitted[s])) {
	    nf++;
	}
	fr->actual[s] = (*pZ)[yno][t];
    }

    if (nf == 0) {
	err = E_DATA;
    } else {
	if (pmod->ci == ARMA) {
	    /* asymptotic normal */
	    fr->tval = 1.96;
	} else {
	    fr->tval = tcrit95(pmod->dfd);
	}

	strcpy(fr->depvar, pdinfo->varname[yno]);
	fr->df = pmod->dfd;
    }

    return err;
}

/* public forecast-related functions follow */

/**
 * get_forecast:
 * @str: string giving starting and ending observations, separated
 * by a space.
 * @pmod: the model from which forecasts are wanted.
 * @pZ: pointer to data array using which @pmod was estimated.
 * @pdinfo: dataset information.
 * @opt: if OPT_D, force a dynamic forecast; if OPT_S, force
 * a static forecast.  By default, the forecast is static within
 * the data range over which the model was estimated, and dynamic
 * out of sample (in cases where a dynamic forecast is meaningful).
 *
 * Allocates a #FITRESID structure and fills it out with forecasts
 * based on @pmod, over the specified range of observations.  
 * For some sorts of models forecast standard errors are also
 * computed (these appear in the %sderr member of the structure
 * to which a pointer is returned; otherwise the %sderr member is
 * %NULL).
 * 
 * The calculation of forecast errors, where applicable, is based 
 * on Davidson and MacKinnon, Econometric Theory and Methods, 
 * chapter 3 (p. 104), which shows how the variance of forecast errors 
 * can be computed given the covariance matrix of the parameter 
 * estimates, provided the error term may be assumed to be serially 
 * uncorrelated.
 *
 * Returns: pointer to allocated structure, or %NULL on failure.
 * The %err member of the returned object should be checked:
 * a non-zero value indicates an error condition.
 */

FITRESID *get_forecast (const char *str, MODEL *pmod, 
			double ***pZ, DATAINFO *pdinfo,
			gretlopt opt) 
{
    FITRESID *fr;

    fr = fit_resid_new(0); 
    if (fr == NULL) {
	return NULL;
    }

    /* Reject in case model was estimated using repacked daily
       data: this case should be handled more elegantly */
    if (gretl_model_get_int(pmod, "daily_repack")) {
	fr->err = E_DATA;
	return fr;
    }

    fit_resid_init(str, pmod, pdinfo, fr); 
    if (fr->err) {
	return fr;
    }

    fr->err = real_get_fcast(fr, pmod, pZ, pdinfo, opt);

    return fr;
}

/**
 * add_forecast:
 * @str: command string, giving a starting observation, ending
 * observation, and variable name to use for the forecast values
 * (the starting and ending observations may be omitted).
 * @pmod: pointer to model.
 * @pZ: pointer to data matrix.
 * @pdinfo: pointer to data information struct.
 * @opt: if OPT_D, force a dynamic forecast; if OPT_S, force
 * a static forecast.  By default, the forecast is static within
 * the data range over which the model was estimated, and dynamic
 * out of sample (in cases where this distinction is meaningful).
 *
 * Adds to the dataset a new variable containing predicted values for the
 * dependent variable in @pmod over the specified range of observations,
 * or, by default, over the sample range currently defined in @pdinfo.
 *
 * In the case of "simple" models with an autoregressive error term 
 * (%AR, %CORC, %HILU, %PWE) the predicted values incorporate
 * the forecastable portion of the error.  
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int add_forecast (const char *str, const MODEL *pmod, double ***pZ,
		  DATAINFO *pdinfo, gretlopt opt)
{
    int oldv = pdinfo->v;
    int t, t1, t2, vi;
    char t1str[OBSLEN], t2str[OBSLEN], varname[VNAMELEN];
    int nf = 0;
    int err = 0;

    *t1str = '\0'; *t2str = '\0';

    /* Reject in case model was estimated using repacked daily
       data: this case should be handled more elegantly */
    if (gretl_model_get_int(pmod, "daily_repack")) {
	return E_DATA;
    }

    /* the varname should either be in the 2nd or 4th position */
    if (sscanf(str, "%*s %8s %8s %8s", t1str, t2str, varname) != 3) {
	if (sscanf(str, "%*s" "%8s", varname) != 1) {
	    return E_PARSE;
	}
    }

    if (*t1str && *t2str) {
	t1 = dateton(t1str, pdinfo);
	t2 = dateton(t2str, pdinfo);
	if (t1 < 0 || t2 < 0 || t2 < t1) {
	    return E_DATA;
	}
    } else {
	t1 = pdinfo->t1;
	t2 = pdinfo->t2;
    }

    if (check_varname(varname)) {
	return 1;
    }

    vi = varindex(pdinfo, varname);
    if (vi == pdinfo->v) {
	err = dataset_add_series(1, pZ, pdinfo);
    }

    if (!err) {
	const double **Z = (const double **) *pZ;
	Forecast fc;

	strcpy(pdinfo->varname[vi], varname);
	strcpy(VARLABEL(pdinfo, vi), _("predicted values"));

	for (t=0; t<pdinfo->n; t++) {
	    (*pZ)[vi][t] = NADBL;
	}

	fc.yhat = (*pZ)[vi];
	fc.sderr = NULL;
	fc.eps = NULL;
	fc.t1 = t1;
	fc.t2 = t2;
	fc.offset = 0;
	fc.model_t2 = pmod->t2;

	get_forecast_method(&fc, pmod, pdinfo, opt);

	if (pmod->ci == ARMA && fc.method == FC_STATIC) {
	    fc.eps = malloc(pdinfo->n * sizeof *fc.eps);
	}

	/* write forecast values into the newly added variable: note
	   that in this case we're not interested in computing
	   forecast standard errors
	*/
	if (pmod->ci == NLS) {
	    nls_fcast(&fc, pmod, pZ, pdinfo);
	} else if (SIMPLE_AR_MODEL(pmod->ci)) {
	    ar_fcast(&fc, pmod, Z, pdinfo);
	} else if (pmod->ci == ARMA) {
	    arma_fcast(&fc, pmod, Z, pdinfo);
	} else if (pmod->ci == GARCH) {
	    garch_fcast(&fc, pmod, Z, pdinfo);
	} else {
	    linear_fcast(&fc, pmod, Z, pdinfo);
	}

	if (fc.dvlags != NULL) {
	    free(fc.dvlags);
	}
	if (fc.eps != NULL) {
	    free(fc.eps);
	}
    }

    for (t=0; t<pdinfo->n; t++) {
	if (!na((*pZ)[vi][t])) {
	    nf++;
	}
    }

    if (nf == 0) {
	dataset_drop_last_variables(pdinfo->v - oldv, pZ, pdinfo);
	err = E_DATA;
    }

    return err;
}

/**
 * display_forecast:
 * @str: string giving starting and ending observations, separated
 * by a space.
 * @pmod: the model from which forecasts are wanted.
 * @pZ: pointer to data array using which @pmod was estimated.
 * @pdinfo: dataset information.
 * @opt: if OPT_D, force a dynamic forecast; if OPT_S, force
 * a static forecast.  By default, the forecast is static within
 * the data range over which the model was estimated, and dynamic
 * out of sample (in cases where this distinction is meaningful).
 * @prn: printing structure.
 *
 * Computes forecasts based on @pmod, over the range of observations
 * given in @str.  Forecast standard errors are also computed
 * if possible.  The results are printed to @prn, and are also
 * plotted if %OPT_P is given.
 *
 * Returns: 0 on success, non-zero error code on error.
 */

int display_forecast (const char *str, MODEL *pmod, 
		      double ***pZ, DATAINFO *pdinfo, 
		      gretlopt opt, PRN *prn)
{
    FITRESID *fr;
    int err;

    fr = get_forecast(str, pmod, pZ, pdinfo, opt);

    if (fr == NULL) {
	return E_ALLOC;
    }

    err = fr->err;

    if (!err) {
	err = text_print_forecast(fr, pZ, pdinfo, opt, prn);
    }

    free_fit_resid(fr);
    
    return err;
}

/**
 * forecast_options_for_model:
 * @pmod: the model from which forecasts are wanted.
 * @t2: end point of the forecast range.
 * @pdinfo: dataset information.
 * @dyn_ok: location to receive 1 if the "dynamic" option is
 * applicable, 0 otherwise.
 * @auto_ok: location to receive 1 if the automatic forecasting 
 * option (static within sample, dynamic out of sample) is
 * applicable, 0 otherwise.
 *
 * Examines @pmod and determines which forecasting options are
 * applicable for this model and forecast range.
 */

void forecast_options_for_model (const MODEL *pmod, int t2,
				 const DATAINFO *pdinfo,
				 int *dyn_ok, int *auto_ok)
{
    *dyn_ok = 0;
    *auto_ok = 0;

    if (pmod->ci == ARMA) {
	*dyn_ok = 1;
    } else if (dataset_is_time_series(pdinfo) &&
	       has_depvar_lags(pmod, pdinfo)) {
	*dyn_ok = 1;
    }

    if (*dyn_ok && t2 > pmod->t2) {
	*auto_ok = 1;
    }
}

