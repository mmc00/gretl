/* 
 * Copyright (C) 2004 Riccardo Lucchetti and Allin Cottrell
 *
 * This program is free software; you can redistribute it and/or
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

/* The algorithm here was contributed by Riccardo "Jack" Lucchetti; 
   C coding by Allin Cottrell. 
*/

#include "libgretl.h"
#include "bhhh_max.h"

/* #define DEBUG */

/* Below: we are buying ourselves a considerable simplification when it comes
   to the tobit_ll function.  That function needs access to the original y
   and X data.  But the sample used for estimation may be at an offset into
   the full dataset, and the variables chosen for the analysis may not
   be at contiguous locations in the main dataset.  So we construct a
   "virtual dataset" in the form of a set of const pointers into the real
   dataset.  These pointers start at the correct sample offset, and are
   contiguous, so the indexing is a lot easier.
*/

static const double **make_tobit_X (const MODEL *pmod, const double **Z)
{
    const double **X;
    int nv = pmod->list[0];
    int offset = pmod->t1;
    int v, i;

    X = malloc(nv * sizeof *X);
    if (X == NULL) return NULL;

    /* constant in slot 0 */
    X[0] = Z[0] + offset;

    /* dependent var in slot 1 */
    X[1] = Z[pmod->list[1]] + offset;

    /* independent vars in slots 2, 3, ... */
    for (i=2; i<nv; i++) {
	v = pmod->list[i + 1];
	X[i] = Z[v] + offset;
    }

    return X;
}

/* Compute log likelihood, and the score matrix if do_score is non-zero */

static int tobit_ll (double *theta, const double **X, double **Z, model_info *tobit, 
		     int do_score)
{
    const double *y = X[1];
    double **series = model_info_get_series(tobit);
    double *e = series[0];
    double *f = series[1];
    double *P = series[2];
    double *ystar = series[3];
    int k = model_info_get_k(tobit);
    int n = model_info_get_n(tobit);
    
    double siginv = theta[k-1]; /* inverse of variance */
    double ll, llt;
    int i, t;

    if (siginv < 0.0) {
	fprintf(stderr, "tobit_ll: got a negative variance\n");
	return 1;
    } 

    /* calculate ystar, e, f, and P vectors */
    for (t=0; t<n; t++) {
	ystar[t] = theta[0];
	for (i=1; i<k-1; i++) {
	    ystar[t] += theta[i] * X[i+1][t]; /* coeff * orig data */
	}
	e[t] = y[t] * siginv - ystar[t];
	f[t] = siginv * normal_pdf(e[t]);
	P[t] = normal_cdf(ystar[t]);
    }

    /* compute loglikelihood for each obs, cumulate into ll */
    ll = 0.0;
    for (t=0; t<n; t++) {
	if (y[t] == 0.0) {
	    llt = 1.0 - P[t];
	} else {
	    llt = f[t];
	}
	ll += log(llt);
    }

    model_info_set_ll(tobit, ll, do_score);

    if (do_score) {
	int i, gi, xi;
	double den, tail;

	for (t=0; t<n; t++) {
	    den = normal_pdf(ystar[t]);
	    tail = 1.0 - P[t];
	    
	    for (i=0; i<k; i++) {

		/* set the indices into the data arrays */
		gi = i + 1;
		xi = (i == 0)? 0 : i + 1;

		if (y[t] == 0.0) {
		    /* score if y is censored */
		    if (xi < k) {
			Z[gi][t] = -den / tail * X[xi][t];
		    } else {
			Z[gi][t] = 0.0;
		    } 
		} else {
		    /* score if y is not censored */
		    if (xi < k) {
			Z[gi][t] = e[t] * X[xi][t];
		    } else {
			Z[gi][t] = e[t] * -y[t];
		    }
		    if (xi == k) {
			Z[gi][t] += 1.0 / siginv;
		    }
		}
	    }
	}
    }

    return 0;
}

/* Transcribe the VCV matrix into packed triangular form */

static int make_vcv (MODEL *pmod, gretl_matrix *v)
{
    const int nv = pmod->ncoeff;
    const int nterms = nv * (nv + 1) / 2;
    double x;
    int i, j, k;

    if (pmod->vcv == NULL) {
	pmod->vcv = malloc(nterms * sizeof *pmod->vcv);
    }
    if (pmod->vcv == NULL) return 1;  

    for (i=0; i<nv; i++) {
	for (j=0; j<=i; j++) {
	    k = ijton(i, j, nv);
	    x = gretl_matrix_get(v, i, j);
	    pmod->vcv[k] = x;
	}
    }

    return 0;
}

static int add_norm_test_to_model (MODEL *pmod, double chi2)
{
    pmod->tests = malloc(sizeof *pmod->tests);
    if (pmod->tests == NULL) return 1;

    strcpy(pmod->tests[0].type, N_("Test for normality of residual"));
    strcpy(pmod->tests[0].h_0, N_("error is normally distributed"));
    pmod->tests[0].param[0] = '\0';
    pmod->tests[0].teststat = GRETL_TEST_NORMAL_CHISQ;
    pmod->tests[0].value = chi2;
    pmod->tests[0].dfn = 2;
    pmod->tests[0].dfd = 0;
    pmod->tests[0].pvalue = chisq(chi2, 2);

    pmod->ntests = 1;

    return 0;
}

/* Taking the original OLS model as a basis, re-write the statistics
   to reflect the Tobit results.
*/

static int write_tobit_stats (MODEL *pmod, double *theta, int ncoeff,
			      double sigma, double ll, const double **X,
			      gretl_matrix *VCV, int iters)
{
    int i, t, cenc = 0;
    int offset = pmod->t1;
    const double *y = X[1];
    double chi2, ubar, udev, skew, kurt;

    for (i=0; i<ncoeff; i++) {
	pmod->coeff[i] = theta[i];
	pmod->sderr[i] = sqrt(gretl_matrix_get(VCV, i, i));
    }

    pmod->sigma = sigma;
    pmod->lnL = ll;

    pmod->ess = 0.0;
    for (t=pmod->t1; t<=pmod->t2; t++) {
	double yhat = pmod->coeff[0];

	for (i=1; i<ncoeff; i++) {
	    yhat += pmod->coeff[i] * X[i + 1][t - offset];
	}

	pmod->yhat[t] = yhat;
	pmod->uhat[t] = y[t - offset] - yhat;

	pmod->ess += pmod->uhat[t] * pmod->uhat[t]; /* Is this at all valid? */

	if (y[t - offset] == 0.0) cenc++;
    }

    /* run normality test on the untruncated uhat */
    moments(pmod->t1, pmod->t2, pmod->uhat, &ubar, &udev, &skew, &kurt, pmod->ncoeff);
    chi2 = doornik_chisq(skew, kurt, pmod->nobs); 
    add_norm_test_to_model(pmod, chi2);

    /* now truncate reported yhat, uhat */
    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (pmod->yhat[t] < 0.0) {
	    pmod->yhat[t] = 0.0;
	    pmod->uhat[t] = y[t - offset];
	}
    }

    pmod->fstt = pmod->rsq = pmod->adjrsq = NADBL;

    make_vcv(pmod, VCV);

    pmod->ci = TOBIT;

    gretl_model_set_int(pmod, "censobs", cenc);
    gretl_model_set_int(pmod, "iters", iters);

    return 0;
}

static model_info *
tobit_model_info_init (int nobs, int k, int n_series)
{
    model_info *tobit = model_info_new();

    if (tobit == NULL) return NULL;

    model_info_set_opts(tobit, FULL_VCV_MATRIX);
    model_info_set_tol(tobit, 1.0e-09);
    model_info_set_k(tobit, k);
    model_info_set_n_series(tobit, n_series);
    model_info_set_t1_t2(tobit, 0, nobs - 1);

    return tobit;
}

/* Main Tobit function */

static int do_tobit (const double **Z, DATAINFO *pdinfo, MODEL *pmod,
		     PRN *prn)
{
    const double **X;
    double *coeff, *theta = NULL;
    double sigma, ll;
    int i, j, k, n;
    int n_series = 4;
    int err = 0;

    model_info *tobit;

    /* for VCV manipulation */
    gretl_matrix *VCV = NULL;
    gretl_matrix *J = NULL; 
    gretl_matrix *tmp = NULL; 

    k = pmod->ncoeff + 1; /* add the variance */
    n = pmod->nobs;

    /* set of pointers into original data */
    X = make_tobit_X(pmod, Z);
    if (X == NULL) {
	return E_ALLOC;
    }

    coeff = realloc(pmod->coeff, k * sizeof *pmod->coeff);
    if (coeff == NULL) {
	free(X);
	return E_ALLOC;
    }
    pmod->coeff = coeff;
    pmod->coeff[k-1] = 1.0;

    tobit = tobit_model_info_init(pmod->nobs, k, n_series);
    if (tobit == NULL) {
	free(X);
	return E_ALLOC;
    }

    /* call BHHH routine to maximize ll */
    err = bhhh_max(tobit_ll, X, pmod->coeff, tobit, prn);

    if (err) {
	goto bailout;
    }

    theta = model_info_get_theta(tobit);

    /* recover estimate of variance */
    sigma = 1.0 / theta[k-1]; 

    /* recover slope estimates */
    for (i=0; i<k-1; i++) {
	theta[i] *= sigma;
    }

    /* get estimate of variance matrix for Olsen parameters */
    VCV = model_info_get_VCV(tobit);
    gretl_invert_symmetric_matrix(VCV);
    gretl_matrix_divide_by_scalar(VCV, n);

    /* Jacobian mat. for transforming VCV from Olsen to slopes + variance */
    J = gretl_matrix_alloc(k, k);
    gretl_matrix_zero(J);
    for (i=0; i<k; i++) {
	for (j=0; j<k; j++) {
	    if (i == j && i < k-1) {
		/* upper left diagonal component */
		gretl_matrix_set(J, i, j, sigma);
	    } else if (j == k-1 && i < j) {
		/* right-hand column */
		gretl_matrix_set(J, i, j, -sigma * theta[i]);
	    } else if (j == k-1 && i == j) {
		/* bottom right-hand element */
		gretl_matrix_set(J, i, j, -sigma * sigma);
	    }
	}
    }

    /* VCV matrix transformation */
    tmp = gretl_matrix_alloc(k, k);
    gretl_matrix_multiply(J, VCV, tmp);
    gretl_matrix_multiply_mod(tmp, GRETL_MOD_NONE,
			      J, GRETL_MOD_TRANSPOSE,
			      VCV);
    gretl_matrix_free(tmp);
    gretl_matrix_free(J);

    ll = model_info_get_ll(tobit);
    write_tobit_stats(pmod, theta, k-1, sigma, ll, X, VCV, 
		      model_info_get_iters(tobit));

 bailout:

    free(X);
    model_info_free(tobit);

    return err;
}

/* the driver function for the plugin */

MODEL tobit_estimate (int *list, double ***pZ, DATAINFO *pdinfo,
		      PRN *prn) 
{
    MODEL model;

    /* run initial OLS: OPT_M bans missing obs */
    model = lsq(list, pZ, pdinfo, OLS, OPT_A | OPT_M, 0.0);
    if (model.errcode) {
	return model;
    }    

    /* do the actual Tobit analysis */
    do_tobit((const double **) *pZ, pdinfo, &model, prn);

    return model;
}

