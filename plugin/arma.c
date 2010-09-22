/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "libgretl.h"
#include "bhhh_max.h"
#include "libset.h"
#include "kalman.h"
#include "matrix_extra.h"
#include "gretl_scalar.h"
#include "gretl_bfgs.h"
#include "arma_priv.h"

#include "../cephes/libprob.h"

#define ARMA_DEBUG 0

#include "arma_common.c"

#define KALMAN_ALL 999

struct bchecker {
    int qmax;
    double *temp;
    double *tmp2;
    cmplx *roots;
};

static void bchecker_free (struct bchecker *b)
{
    if (b != NULL) {
	free(b->temp);
	free(b->tmp2);
	free(b->roots);
	free(b);
    }
}

static struct bchecker *bchecker_allocate (arma_info *ainfo)
{
    struct bchecker *b;

    b = malloc(sizeof *b);
    if (b == NULL) {
	return NULL;
    }

    b->temp = NULL;
    b->tmp2 = NULL;
    b->roots = NULL;

    b->qmax = ainfo->q + ainfo->Q * ainfo->pd;

    b->temp  = malloc((b->qmax + 1) * sizeof *b->temp);
    b->tmp2  = malloc((b->qmax + 1) * sizeof *b->tmp2);
    b->roots = malloc(b->qmax * sizeof *b->roots);

    if (b->temp == NULL || b->tmp2 == NULL || b->roots == NULL) {
	bchecker_free(b);
	b = NULL;
    } 

    return b;
}

/* check whether the MA estimates have gone out of bounds in the
   course of iteration */

int ma_out_of_bounds (arma_info *ainfo, const double *theta,
		      const double *Theta)
{
    static struct bchecker *b = NULL;
    double re, im, rt;
    int i, j, k, m, si, qtot;
    int tzero = 1, Tzero = 1;
    int err = 0, cerr = 0;

    if (ainfo == NULL) {
	/* signal for cleanup */
	bchecker_free(b);
	b = NULL;
	return 0;
    }

    k = 0;
    for (i=0; i<ainfo->q && tzero; i++) {
	if (MA_included(ainfo, i)) {
	    if (theta[k++] != 0.0) {
		tzero = 0;
	    }
	}
    }  

    for (i=0; i<ainfo->Q && Tzero; i++) {
	if (Theta[i] != 0.0) {
	    Tzero = 0;
	}    
    }  
    
    if (tzero && Tzero) {
	/* nothing to be done */
	return 0;
    }

    if (b == NULL) {
	b = bchecker_allocate(ainfo);
	if (b == NULL) {
	    return 1;
	}
    }

    b->temp[0] = 1.0;

    /* initialize to non-seasonal MA or zero */
    k = 0;
    for (i=0; i<b->qmax; i++) {
        if (i < ainfo->q && MA_included(ainfo, i)) {
	    b->temp[i+1] = theta[k++];
        } else {
            b->temp[i+1] = 0.0;
        }
    }

    /* add seasonal MA and interaction */
    if (Tzero) {
	qtot = ainfo->q;
    } else {
	qtot = b->qmax;
	for (j=0; j<ainfo->Q; j++) {
	    si = (j + 1) * ainfo->pd;
	    b->temp[si] += Theta[j];
	    k = 0;
	    for (i=0; i<ainfo->q; i++) {
		if (MA_included(ainfo, i)) {
		    m = si + (i + 1);
		    b->temp[m] += Theta[j] * theta[k++];
		} 
	    }
	}
    }

#if ARMA_DEBUG
    if (b->temp[qtot] == 0.0) {
	fprintf(stderr, "b->temp[%d] = 0; polrt won't work\n", qtot);
	fprintf(stderr, "q = %d, Q = %d, b->qmax = %d\n", 
		ainfo->q, ainfo->Q, b->qmax);
	for (i=0; i<=qtot; i++) {
	    fprintf(stderr, "b->temp[%d] = %g\n", i, b->temp[i]);
	}
    }
#endif

    cerr = polrt(b->temp, b->tmp2, qtot, b->roots);
    if (cerr) {
	fprintf(stderr, "ma_out_of_bounds: polrt returned %d\n", cerr);
	return 0; /* ?? */
    }

    for (i=0; i<qtot; i++) {
	re = b->roots[i].r;
	im = b->roots[i].i;
	rt = re * re + im * im;
	if (rt > DBL_EPSILON && rt <= 1.0) {
	    pprintf(ainfo->prn, "MA root %d = %g\n", i, rt);
	    err = 1;
	    break;
	}
    }

    return err;
}

void bounds_checker_cleanup (void)
{
    ma_out_of_bounds(NULL, NULL, NULL);
}

/*
  Given an ARMA process $A(L)B(L) y_t = C(L)D(L) \epsilon_t$, finds the 
  roots of the four polynomials -- or just two polynomials if seasonal
  AR and MA effects, B(L) and D(L) are not present -- and attaches
  this information to the ARMA model.

  pmod: MODEL pointer to which the roots info should be attached.

  ainfo: gives various pieces of information on the ARMA model,
  including seasonal and non-seasonal AR and MA orders.

  coeff: ifc + p + q + P + Q vector of coefficients (if an intercept
  is present it is element 0 and is ignored)

  returns: zero on success, non-zero on failure
*/

int arma_model_add_roots (MODEL *pmod, arma_info *ainfo,
			  const double *coeff)
{
    const double *phi =   coeff + ainfo->ifc;
    const double *Phi =     phi + ainfo->np;
    const double *theta =   Phi + ainfo->P;
    const double *Theta = theta + ainfo->nq;
    int nr = ainfo->p + ainfo->P + ainfo->q + ainfo->Q;
    int pmax, qmax, lmax;
    double *temp = NULL, *tmp2 = NULL;
    cmplx *rptr, *roots = NULL;
    int i, k, cerr = 0;

    pmax = (ainfo->p > ainfo->P)? ainfo->p : ainfo->P;
    qmax = (ainfo->q > ainfo->Q)? ainfo->q : ainfo->Q;
    lmax = (pmax > qmax)? pmax : qmax;

    if (pmax == 0 && qmax == 0) {
	return 0;
    }

    temp = malloc((lmax + 1) * sizeof *temp);
    tmp2 = malloc((lmax + 1) * sizeof *tmp2);
    roots = malloc(nr * sizeof *roots);

    if (temp == NULL || tmp2 == NULL || roots == NULL) {
	free(temp);
	free(tmp2);
	free(roots);
	return E_ALLOC;
    }

    temp[0] = 1.0;
    rptr = roots;

    if (ainfo->p > 0) {
	/* A(L), non-seasonal */
	k = 0;
	for (i=0; i<ainfo->p; i++) {
	    if (AR_included(ainfo, i)) {
		temp[i+1] = -phi[k++];
	    } else {
		temp[i+1] = 0;
	    }
	}
	cerr = polrt(temp, tmp2, ainfo->p, rptr);
	rptr += ainfo->p;
    }

    if (!cerr && ainfo->P > 0) {
	/* B(L), seasonal */
	for (i=0; i<ainfo->P; i++) {
	    temp[i+1] = -Phi[i];
	}    
	cerr = polrt(temp, tmp2, ainfo->P, rptr);
	rptr += ainfo->P;
    }

    if (!cerr && ainfo->q > 0) {
	/* C(L), non-seasonal */
	k = 0;
	for (i=0; i<ainfo->q; i++) {
	    if (MA_included(ainfo, i)) {
		temp[i+1] = theta[k++];
	    } else {
		temp[i+1] = 0;
	    }
	}  
	cerr = polrt(temp, tmp2, ainfo->q, rptr);
	rptr += ainfo->q;
    }

    if (!cerr && ainfo->Q > 0) {
	/* D(L), seasonal */
	for (i=0; i<ainfo->Q; i++) {
	    temp[i+1] = Theta[i];
	}  
	cerr = polrt(temp, tmp2, ainfo->Q, rptr);
    }
    
    free(temp);
    free(tmp2);

    if (cerr) {
	free(roots);
    } else {
	gretl_model_set_data(pmod, "roots", roots, GRETL_TYPE_CMPLX_ARRAY,
			     nr * sizeof *roots);
    }

    return 0;
}

/* below: exact ML using Kalman filter apparatus */

typedef struct kalman_helper_ khelper;

struct kalman_helper_ {
    gretl_matrix_block *B;
    gretl_matrix *S;
    gretl_matrix *P;
    gretl_matrix *F;
    gretl_matrix *A;
    gretl_matrix *H;
    gretl_matrix *Q;
    gretl_matrix *E;
    gretl_matrix *Svar;
    gretl_matrix *Svar2;
    gretl_matrix *vQ;

    gretl_matrix *F_; /* used only for ARIMA via levels */
    gretl_matrix *Q_; /* ditto */
    gretl_matrix *P_; /* ditto */

    arma_info *kainfo; 
};

static void kalman_helper_free (khelper *kh)
{
    if (kh != NULL) {
	gretl_matrix_block_destroy(kh->B);
	gretl_matrix_free(kh->Svar2);
	gretl_matrix_free(kh->vQ);
	gretl_matrix_free(kh->F_);
	gretl_matrix_free(kh->Q_);
	gretl_matrix_free(kh->P_);
	free(kh);
    }
}

/* Get the dimension of the ARMA state, net of any
   augmentation associated with ARIMA estimation.
*/

static int ainfo_get_r0 (arma_info *ainfo)
{
    int pmax = ainfo->p + ainfo->pd * ainfo->P;
    int qmax = ainfo->q + ainfo->pd * ainfo->Q;

    return (pmax > qmax + 1)? pmax : qmax + 1;
}

static khelper *kalman_helper_new (arma_info *ainfo,
				   int r, int k)
{
    khelper *kh;
    int r0, r2;
    int err = 0;

    kh = malloc(sizeof *kh);
    if (kh == NULL) {
	return NULL;
    }

    r0 = ainfo_get_r0(ainfo);
    r2 = r0 * r0;

    kh->Svar2 = kh->vQ = NULL;
    kh->F_ = kh->Q_ = kh->P_ = NULL;

    kh->B = gretl_matrix_block_new(&kh->S, r, 1,
				   &kh->P, r, r,
				   &kh->F, r, r,
				   &kh->A, k, 1,
				   &kh->H, r, 1,
				   &kh->Q, r, r,
				   &kh->E, ainfo->fullT, 1,
				   &kh->Svar, r2, r2,
				   NULL);

    if (kh->B == NULL) {
	err = E_ALLOC;
    } else if (arma_using_vech(ainfo)) {
	int m = r0 * (r0 + 1) / 2;

	kh->Svar2 = gretl_matrix_alloc(m, m);
	kh->vQ = gretl_column_vector_alloc(m);
	if (kh->Svar2 == NULL || kh->vQ == NULL) {
	    err = E_ALLOC;
	}	    
    } else {
	kh->vQ = gretl_column_vector_alloc(r2);
	if (kh->vQ == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err && arima_levels(ainfo)) {
	kh->F_ = gretl_matrix_alloc(r0, r0);
	kh->Q_ = gretl_matrix_alloc(r0, r0);
	kh->P_ = gretl_matrix_alloc(r0, r0);
	if (kh->F_ == NULL || kh->Q_ == NULL || kh->P_ == NULL) {
	    err = E_ALLOC;
	}
    }

    if (err) {
	kalman_helper_free(kh);
	kh = NULL;
    } else {
	kh->kainfo = ainfo;
    }

    return kh;
}

/* Get the dimension of the state-space representation: note
   that this is augmented if we're estimating an ARIMA
   model using the levels formulation.
*/

static int ainfo_get_state_size (arma_info *ainfo)
{
    int pmax = ainfo->p + ainfo->pd * ainfo->P;
    int qmax = ainfo->q + ainfo->pd * ainfo->Q;
    int r = (pmax > qmax + 1)? pmax : qmax + 1;

    if (arima_levels(ainfo)) {
	int k = ainfo->d + ainfo->pd * ainfo->D;

	r += k;
    }

    return r;
}

static int allocate_ac_mc (arma_info *ainfo)
{
    int m = (ainfo->P > 0) + (ainfo->Q > 0);
    int err = 0;

    if (m > 0) {
	double *ac = NULL, *mc = NULL;
	int n, i = 0;

	ainfo->aux = doubles_array_new(m, 0);
	if (ainfo->aux == NULL) {
	    return E_ALLOC;
	}

	if (ainfo->P > 0) {
	    n = 1 + ainfo->p + ainfo->pd * ainfo->P;
	    ac = malloc(n * sizeof *ac);
	    if (ac == NULL) {
		err = E_ALLOC;
	    } else {
		ainfo->aux[i++] = ac;
	    }
	}

	if (!err && ainfo->Q > 0) {
	    n = 1 + ainfo->q + ainfo->pd * ainfo->Q;
	    mc = malloc(n * sizeof *mc);
	    if (mc == NULL) {
		err = E_ALLOC;
	    } else {
		ainfo->aux[i++] = mc;
	    }
	}

	if (err) {
	    doubles_array_free(ainfo->aux, m);
	} else {
	    ainfo->n_aux = m;
	}
    }

    return err;
}

static void write_big_phi (const double *phi, 
			   const double *Phi,
			   arma_info *ainfo,
			   gretl_matrix *F)
{
    int pmax = ainfo->p + ainfo->pd * ainfo->P;
    double *ac = ainfo->aux[0];
    double x, y;
    int i, j, k, ii;

    for (i=0; i<=pmax; i++) {
	ac[i] = 0.0;
    }

    for (j=-1; j<ainfo->P; j++) {
	x = (j < 0)? -1 : Phi[j];
	k = 0.0;
	for (i=-1; i<ainfo->p; i++) {
	    if (i < 0) {
		y = -1;
	    } else if (AR_included(ainfo, i)) {
		y = phi[k++];
	    } else {
		y = 0.0;
	    }
	    ii = (j+1) * ainfo->pd + (i+1);
	    ac[ii] -= x * y;
	}
    }

    for (i=0; i<pmax; i++) {
	gretl_matrix_set(F, 0, i, ac[i+1]);
    }
}

static void write_big_theta (const double *theta, 
			     const double *Theta,
			     arma_info *ainfo,
			     gretl_matrix *H)
{
    int qmax = ainfo->q + ainfo->pd * ainfo->Q;
    int i = (ainfo->P > 0)? 1 : 0;
    double *mc = ainfo->aux[i];
    double x, y;
    int j, k, ii;

    for (i=0; i<=qmax; i++) {
	mc[i] = 0.0;
    }

    for (j=-1; j<ainfo->Q; j++) {
	x = (j < 0)? 1 : Theta[j];
	k = 0;
        for (i=-1; i<ainfo->q; i++) {
	    if (i < 0) {
		y = 1;
	    } else if (MA_included(ainfo, i)) {
		y = theta[k++];
	    } else {
		y = 0;
	    }
            ii = (j+1) * ainfo->pd + (i+1);
	    mc[ii] = x * y;
        }
    }

    for (i=1; i<=qmax; i++) {
	H->val[i] = mc[i];
    }    
}

static void condense_row (gretl_matrix *targ,
			  const gretl_matrix *src,
			  int targrow, int srcrow,
			  int n)
{
    double x;
    int i, j, k, g;
    int targcol = 0;

    for (j=0; j<n; j++) {
	for (i=j; i<n; i++) {
	    k = j * n + i;
	    g = (k % n) * n + k / n;
	    x = gretl_matrix_get(src, srcrow, k);
	    if (g != k) {
		x += gretl_matrix_get(src, srcrow, g);
	    } 
	    gretl_matrix_set(targ, targrow, targcol++, x);
	}
    }
}

static void condense_state_vcv (gretl_matrix *targ, 
				const gretl_matrix *src,
				int n)
{
    int posr = 0, posc = 0;
    int i, j;

    for (i=0; i<n; i++) {
	for (j=0; j<n; j++) {
	    if (j >= i) {
		condense_row(targ, src, posr++, posc, n);
	    }
	    posc++;
	}
    }
}

/* Write into @c the coefficients of the lag operator in the 
   expansion of (1-L)^d * (1-L^s)^D, for d <= 2 && D <= 2.
*/

static void diff_coeffs (double *c, int d, int D, int s)
{
    int i, k = d + s * D;

    for (i=0; i<k; i++) {
	c[i] = 0;
    }

    if (d == 1) {
	c[0] = -1;
    } else if (d == 2) {
	c[0] = -2;
	c[1] = 1;
    }

    if (D > 0) {
	c[s-1] -= 1;
	if (d > 0) {
	    c[s] += 1;
	}
	if (d == 2) {
	    c[s] += 1;
	    c[s+1] -= 1;
	}
    } 

    if (D == 2) {
	c[s-1] -= 1;
	c[2*s-1] += 1;
	if (d > 0) {
	    c[s] += 1;
	    c[2*s] -= 1;
	}
	if (d == 2) {
	    c[s] += 1;
	    c[2*s] -= 1;
	    c[s+1] -= 1;
	    c[2*s+1] += 1;
	}
    }
}

static int kalman_matrices_init (arma_info *ainfo,
				 khelper *kh)
{
    int r = kh->F->rows;

    gretl_matrix_zero(kh->A);
    gretl_matrix_zero(kh->P);

    gretl_matrix_zero(kh->F);
    gretl_matrix_inscribe_I(kh->F, 1, 0, r - 1);

#if 0 /* not ready yet */
    if (arima_levels(ainfo)) {
	int i, k = ainfo->d + ainfo->pd * ainfo->D;
	int *c = malloc(k * sizeof *c);

	if (c == NULL) {
	    return E_ALLOC;
	}

	diff_coeffs(c, ainfo->d, ainfo->D, ainfo->pd);
	for (i=0; i<k; i++) {
	    gretl_matrix_set(kh->F, r, c, -c[i]);
	}
	gretl_matrix_inscribe_I(kh->F, r, c, k - 1);
	free(c);
    }
#endif

    gretl_matrix_zero(kh->Q);
    gretl_matrix_set(kh->Q, 0, 0, 1.0);

    gretl_matrix_zero(kh->H);
    gretl_vector_set(kh->H, 0, 1.0);

    return 0;
}

#define ARMA_MDEBUG 0

static int write_kalman_matrices (khelper *kh,
				  const double *b, 
				  int idx)
{
    arma_info *kainfo = kh->kainfo;
    const double *phi =       b + kainfo->ifc;
    const double *Phi =     phi + kainfo->np;
    const double *theta =   Phi + kainfo->P;
    const double *Theta = theta + kainfo->nq;
    const double *beta =  Theta + kainfo->Q;
    double mu = (kainfo->ifc)? b[0] : 0.0;
    int rewrite_A = 0;
    int rewrite_F = 0;
    int rewrite_H = 0;
    int i, k, err = 0;

    gretl_matrix_zero(kh->S);

    if (idx == KALMAN_ALL) {
	rewrite_A = rewrite_F = rewrite_H = 1;
    } else {
	/* called in context of calculating score */
	int pmax = kainfo->ifc + kainfo->np + kainfo->P;
	int tmax = pmax + kainfo->nq + kainfo->Q;

	if (kainfo->ifc && idx == 0) {
	    rewrite_A = 1;
	} else if (idx >= kainfo->ifc && idx < pmax) {
	    rewrite_F = 1;
	} else if (idx >= kainfo->ifc && idx < tmax) {
	    rewrite_H = 1;
	} else {
	    rewrite_A = 1;
	}
    }

#if ARMA_MDEBUG
    fprintf(stderr, "\n*** write_kalman_matrices: before\n");
    gretl_matrix_print(kh->A, "A");
    gretl_matrix_print(kh->P, "P");
    gretl_matrix_print(kh->F, "F");
    gretl_matrix_print(kh->H, "H");
#endif    

    /* See Hamilton, Time Series Analysis, ch 13, p. 375 */

    if (rewrite_A) {
	/* const and coeffs on exogenous vars */
	gretl_vector_set(kh->A, 0, mu);
	for (i=0; i<kainfo->nexo; i++) {
	    gretl_vector_set(kh->A, i + 1, beta[i]);
	}
    }

    if (rewrite_H) {
	/* form the H vector using theta and/or Theta */
	if (kainfo->Q > 0) {
	    write_big_theta(theta, Theta, kainfo, kh->H);
	} else {
	    k = 0;
	    for (i=0; i<kainfo->q; i++) {
		if (MA_included(kainfo, i)) {
		    gretl_vector_set(kh->H, i+1, theta[k++]);
		} else {
		    gretl_vector_set(kh->H, i+1, 0.0);
		}
	    }
	}
    }

    if (rewrite_F) {
	/* form the F matrix using phi and/or Phi */
	gretl_matrix *F = (kh->F_ == NULL)? kh->F : kh->F_;
	gretl_matrix *Q = (kh->Q_ == NULL)? kh->Q : kh->Q_;
	gretl_matrix *P = (kh->P_ == NULL)? kh->P : kh->P_;

	if (kainfo->P > 0) {
	    write_big_phi(phi, Phi, kainfo, F);
	} else {
	    k = 0;
	    for (i=0; i<kainfo->p; i++) {
		if (AR_included(kainfo, i)) {
		    gretl_matrix_set(F, 0, i, phi[k++]);
		} else {
		    gretl_matrix_set(F, 0, i, 0.0);
		}
	    }
	} 

	/* form $P_{1|0}$ (MSE) matrix, as per Hamilton, ch 13, p. 378. */

	gretl_matrix_kronecker_product(F, F, kh->Svar);
	gretl_matrix_I_minus(kh->Svar);

	if (arma_using_vech(kainfo)) {
	    condense_state_vcv(kh->Svar2, kh->Svar, gretl_matrix_rows(F));
	    gretl_matrix_vectorize_h(kh->vQ, Q);
	    err = gretl_LU_solve(kh->Svar2, kh->vQ);
	    if (!err) {
		gretl_matrix_unvectorize_h(P, kh->vQ);
	    }
	} else {
	    gretl_matrix_vectorize(kh->vQ, Q);
	    err = gretl_LU_solve(kh->Svar, kh->vQ);
	    if (!err) {
		gretl_matrix_unvectorize(P, kh->vQ);
	    }
	}
    }

    if (arima_levels(kainfo)) {
	/* complete the job on F, Q and P */
	gretl_matrix_inscribe_matrix(kh->F, kh->F_, 0, 0, GRETL_MOD_NONE);
	gretl_matrix_inscribe_matrix(kh->Q, kh->Q_, 0, 0, GRETL_MOD_NONE);
	gretl_matrix_inscribe_matrix(kh->P, kh->P_, 0, 0, GRETL_MOD_NONE);
    }

#if ARMA_MDEBUG
    fprintf(stderr, "\n*** after\n");
    gretl_matrix_print(kh->A, "A");
    gretl_matrix_print(kh->P, "P");
    gretl_matrix_print(kh->F, "F");
    gretl_matrix_print(kh->H, "H");
#endif    

    return err;
}

static int rewrite_kalman_matrices (kalman *K, const double *b, int i)
{
    khelper *kh = (khelper *) kalman_get_data(K);
    int err = write_kalman_matrices(kh, b, i);

    if (!err) {
	kalman_set_initial_state_vector(K, kh->S);
	kalman_set_initial_MSE_matrix(K, kh->P);
    }

    return err;
}

/* add covariance matrix and standard errors based on numerical
   approximation to the Hessian
*/

static int arma_hessian_vcv (MODEL *pmod, gretl_matrix *Hinv,
			     const gretl_matrix *E)
{
    int t, i = 0;

    for (t=pmod->t1; t<=pmod->t2; t++) {
	pmod->uhat[t] = gretl_vector_get(E, i++);
    }

    return gretl_model_write_vcv(pmod, Hinv);
}

static const double *kalman_arma_llt_callback (const double *b, int i, 
					       void *data)
{
    kalman *K = (kalman *) data;
    khelper *kh = kalman_get_data(K);
    int err;

    rewrite_kalman_matrices(K, b, i);
    err = kalman_forecast(K, NULL);

#if ARMA_DEBUG
    fprintf(stderr, "kalman_arma_llt: kalman f'cast gave "
	    "err = %d, ll = %#.12g\n", err, kalman_get_loglik(K));
#endif

    return (err)? NULL : kh->E->val;
}

/* add covariance matrix and standard errors based on Outer Product of
   Gradient
*/

static int arma_OPG_vcv (MODEL *pmod, kalman *K, double *b, 
			 double s2, int k, int T,
			 PRN *prn)
{
    khelper *kh = kalman_get_data(K);
    gretl_matrix *G = NULL;
    gretl_matrix *V = NULL;
    double rcond;
    int s, t;
    int err = 0;

    for (t=pmod->t1, s=0; t<=pmod->t2; t++, s++) {
	pmod->uhat[t] = gretl_vector_get(kh->E, s);
    }

    G = build_score_matrix(b, k, T, kalman_arma_llt_callback, 
			   K, &err);
    if (err) {
	goto bailout;
    }

    V = gretl_matrix_alloc(k, k);
    if (V == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    gretl_matrix_multiply_mod(G, GRETL_MOD_NONE,
			      G, GRETL_MOD_TRANSPOSE,
			      V, GRETL_MOD_NONE);

    rcond = gretl_symmetric_matrix_rcond(V, &err);
    if (!err && rcond < 1.0E-10) {
	pprintf(prn, "OPG: rcond = %g; will try Hessian\n", rcond);
	err = 1;
    }

    if (!err) {
	err = gretl_invert_symmetric_matrix(V);
    }

    if (!err) {
	gretl_matrix_multiply_by_scalar(V, s2);
	err = gretl_model_write_vcv(pmod, V);
    }

 bailout:

    gretl_matrix_free(G);
    gretl_matrix_free(V);
    
    return err;
}

#if ARMA_DEBUG

static void debug_print_theta (const double *theta,
			       const double *Theta)
{
    int i, k = 0;

    fprintf(stderr, "kalman_arma_ll():\n");

    for (i=0; i<kainfo->q; i++) {
	if (MA_included(kainfo, i)) {
	    fprintf(stderr, "theta[%d] = %#.12g\n", i+1, theta[k++]);
	}
    }

    for (i=0; i<kainfo->Q; i++) {
	fprintf(stderr, "Theta[%d] = %#.12g\n", i, Theta[i]);
    }   
}

#endif

static int kalman_do_ma_check = 1;

static double kalman_arma_ll (const double *b, void *data)
{
    kalman *K = (kalman *) data;
    khelper *kh = kalman_get_data(K);
    arma_info *kainfo = kh->kainfo;
    int offset = kainfo->ifc + kainfo->np + kainfo->P;
    const double *theta = b + offset;
    const double *Theta = theta + kainfo->nq;
    double ll = NADBL;
    int err = 0;

#if ARMA_DEBUG
    debug_print_theta(theta, Theta);
#endif

    if (kalman_do_ma_check && ma_out_of_bounds(kainfo, theta, Theta)) {
	pputs(kalman_get_printer(K), "arma: MA estimate(s) out of bounds\n");
	return NADBL;
    }

    err = rewrite_kalman_matrices(K, b, KALMAN_ALL);

    if (!err) {
	err = kalman_forecast(K, NULL);
	ll = kalman_get_loglik(K);
#if ARMA_DEBUG
	fprintf(stderr, "kalman_arma_ll: kalman_forecast gave %d, "
		"loglik = %#.12g\n", err, ll);
#endif

    }

    return ll;
}

static int arima_ydiff_only (arma_info *ainfo)
{
    if ((ainfo->d > 0 || ainfo->D > 0) &&
	ainfo->nexo > 0 && !arma_xdiff(ainfo)) {
	return 1;
    } else {
	return 0;
    }
}

static int arma_use_hessian (gretlopt opt)
{
    int ret = 1;

    if (opt & OPT_G) {
	ret = 0;
    } else if (libset_get_int(ARMA_VCV) == VCV_OP) {
	ret = 0;
    }

    return ret;
}

static int kalman_arma_finish (MODEL *pmod, arma_info *ainfo,
			       const double **Z, const DATAINFO *pdinfo, 
			       kalman *K, double *b, 
			       gretlopt opt, PRN *prn)
{
    khelper *kh = kalman_get_data(K);
    int do_hess = arma_use_hessian(opt);
    int kopt, i, k = ainfo->nc;
    double s2;
    int err;

    pmod->t1 = ainfo->t1;
    pmod->t2 = ainfo->t2;
    pmod->nobs = ainfo->T;
    pmod->ncoeff = ainfo->nc;
    pmod->full_n = pdinfo->n;

    /* in the Kalman case the basic model struct is empty, so we 
       have to allocate for coefficients, residuals and so on
    */

    err = gretl_model_allocate_storage(pmod);
    if (err) {
	return err;
    }

    for (i=0; i<k; i++) {
	pmod->coeff[i] = b[i];
    }

    s2 = kalman_get_arma_variance(K);
    pmod->sigma = sqrt(s2);

    pmod->lnL = kalman_get_loglik(K);
    kopt = kalman_get_options(K);

    /* rescale if we're using average loglikelihood */
    if (kopt & KALMAN_AVG_LL) {
	pmod->lnL *= ainfo->T;
    }

#if ARMA_DEBUG
    fprintf(stderr, "kalman_arma_finish: doing VCV, method %s\n",
	    (do_hess)? "Hessian" : "OPG");
#endif

    if (!do_hess) {
	/* try OPG first, hessian as fallback */
	err = arma_OPG_vcv(pmod, K, b, s2, k, ainfo->T, prn);
	if (err) {
	    err = 0;
	    do_hess = 1;
	} else {
	    gretl_model_set_vcv_info(pmod, VCV_ML, VCV_OP);
	    pmod->opt |= OPT_G;
	}
    }	

    if (do_hess) { 
	gretl_matrix *Hinv;

	kalman_do_ma_check = 0;
	Hinv = numerical_hessian(b, ainfo->nc, kalman_arma_ll, K, &err);
	kalman_do_ma_check = 1;
	if (!err) {
	    if (kopt & KALMAN_AVG_LL) {
		gretl_matrix_divide_by_scalar(Hinv, ainfo->T);
	    }
	    err = arma_hessian_vcv(pmod, Hinv, kh->E);
	    if (!err) {
		gretl_model_set_vcv_info(pmod, VCV_ML, VCV_HESSIAN);
	    }
	}
	gretl_matrix_free(Hinv);
    }

    if (!err) {
	write_arma_model_stats(pmod, ainfo, Z, pdinfo);
	arma_model_add_roots(pmod, ainfo, b);
	gretl_model_set_int(pmod, "arma_flags", ARMA_EXACT);
	if (arma_lbfgs(ainfo)) {
	    pmod->opt |= OPT_L;
	}
	if (arima_ydiff_only(ainfo)) {
	    pmod->opt |= OPT_Y;
	}
    }

    return err;
}

static void matrix_NA_to_nan (gretl_matrix *m)
{
    int i, n = m->rows * m->cols;

    for (i=0; i<n; i++) {
	if (na(m->val[i])) {
	    m->val[i] = M_NA;
	}
    }
}

/* for Kalman: convert from full-length y series to
   y vector of length ainfo->T */

static gretl_matrix *form_arma_y_vector (arma_info *ainfo,
					 const double **Z,
					 int *err)
{
    gretl_matrix *yvec;

    yvec = gretl_column_vector_alloc(ainfo->fullT);

    if (yvec == NULL) {
	*err = E_ALLOC;
    } else {
	const double *y;

	y = (ainfo->y != NULL)? ainfo->y : Z[ainfo->yno];
	memcpy(yvec->val, y + ainfo->t1, ainfo->fullT * sizeof *y);
	if (arma_missvals(ainfo)) {
	    matrix_NA_to_nan(yvec);
	}
#if ARMA_DEBUG
	gretl_matrix_print(yvec, "arma y vector");
#endif
    }
    
    return yvec;
}

static gretl_matrix *form_arma_X_matrix (arma_info *ainfo,
					 const double **Z,
					 int *err)
{
    gretl_matrix *X;
    int missop;

#if ARMA_DEBUG
    printlist(ainfo->xlist, "ainfo->xlist (exog vars)");
#endif

    if (arma_na_ok(ainfo)) {
	missop = M_MISSING_OK;
    } else {
	missop = M_MISSING_ERROR;
    }

    X = gretl_matrix_data_subset(ainfo->xlist, Z, ainfo->t1, ainfo->t2, 
				 missop, err);

#if ARMA_DEBUG
    gretl_matrix_print(X, "X");
#endif

    return X;
}

static int kalman_undo_y_scaling (arma_info *ainfo,
				  gretl_matrix *y, double *b, 
				  kalman *K)
{
    double *beta = b + 1 + ainfo->np + ainfo->P +
	ainfo->nq + ainfo->Q;
    int i, t, T = ainfo->t2 - ainfo->t1 + 1;
    int err = 0;

    b[0] *= ainfo->yscale;

    for (i=0; i<ainfo->nexo; i++) {
	beta[i] *= ainfo->yscale;
    }

    i = ainfo->t1;
    for (t=0; t<T; t++) {
	y->val[t] *= ainfo->yscale;
	ainfo->y[i++] *= ainfo->yscale;
    }

    if (na(kalman_arma_ll(b, K))) {
	err = 1;
    }

    return err;
}

static void free_arma_X_matrix (arma_info *ainfo, gretl_matrix *X)
{
    if (X == ainfo->dX) {
	gretl_matrix_free(ainfo->dX);
	ainfo->dX = NULL;
    } else {
	gretl_matrix_free(X);
    }
}

static int kalman_arma (double *coeff, 
			const double **Z, const DATAINFO *pdinfo,
			arma_info *ainfo, MODEL *pmod,
			gretlopt opt)
{
    kalman *K = NULL;
    khelper *kh = NULL;
    gretl_matrix *y = NULL;
    gretl_matrix *X = NULL;
    int r, k = 1 + ainfo->nexo; /* number of exog vars plus space for const */
    int fncount = 0, grcount = 0;
    double *b;
    int i, err = 0;
    
    b = malloc(ainfo->nc * sizeof *b);
    if (b == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<ainfo->nc; i++) {
	b[i] = coeff[i];
    }

#if ARMA_DEBUG
    fputs("initial coefficients:\n", stderr);
    for (i=0; i<ainfo->nc; i++) {
	fprintf(stderr, " b[%d] = % .10E\n", i, b[i]);
    }
#endif

    y = form_arma_y_vector(ainfo, Z, &err);

    if (!err && ainfo->nexo > 0) {
	if (ainfo->dX != NULL) {
	    X = ainfo->dX;
	} else {
	    X = form_arma_X_matrix(ainfo, Z, &err);
	}
    }

    if (!err) {
	err = allocate_ac_mc(ainfo);
    }

    if (err) {
	goto bailout;
    }

    r = ainfo_get_state_size(ainfo);

    /* when should we use vech apparatus? */
    if (r > 4) {
	set_arma_use_vech(ainfo);
    }

    kh = kalman_helper_new(ainfo, r, k);
    if (kh == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    kalman_matrices_init(ainfo, kh);

#if ARMA_DEBUG
    fprintf(stderr, "ready to estimate: ainfo specs:\n"
	    "p=%d, P=%d, q=%d, Q=%d, ifc=%d, nexo=%d, t1=%d, t2=%d\n", 
	    ainfo->p, ainfo->P, ainfo->q, ainfo->Q, ainfo->ifc, 
	    ainfo->nexo, ainfo->t1, ainfo->t2);
    fprintf(stderr, "Kalman dims: r = %d, k = %d, T = %d, ncoeff=%d\n", 
	    r, k, ainfo->T, ainfo->nc);
#endif

    K = kalman_new(kh->S, kh->P, kh->F, kh->A, kh->H, kh->Q, 
		   NULL, y, X, NULL, kh->E, &err);

    if (err) {
	fprintf(stderr, "kalman_new(): err = %d\n", err);
    } else {
	int maxit, save_lbfgs = 0;
	double toler;
	gretl_matrix *A0 = NULL;

	kalman_attach_printer(K, ainfo->prn);
	kalman_attach_data(K, kh);

	if (r > 3) {
	    kalman_set_nonshift(K, 1);
	} else {
	    kalman_set_nonshift(K, r);
	}

	if (getenv("KALMAN_AVG_LL") != NULL) {
	    kalman_set_options(K, KALMAN_ARMA_LL | KALMAN_AVG_LL);
	} else {
	    kalman_set_options(K, KALMAN_ARMA_LL);
	}

	save_lbfgs = libset_get_bool(USE_LBFGS);

	if (save_lbfgs) {
	    ainfo->pflags |= ARMA_LBFGS;
	} else if (opt & OPT_L) {
	    libset_set_bool(USE_LBFGS, 1);
	    ainfo->pflags |= ARMA_LBFGS;
	}

	BFGS_defaults(&maxit, &toler, ARMA);

#if KALMAN_ARMA_INITH
	A0 = kalman_arma_init_H(b, ainfo->nc, ainfo->T, K,
				ainfo, Z, pdinfo);
#endif

	err = BFGS_max(b, ainfo->nc, maxit, toler, 
		       &fncount, &grcount, kalman_arma_ll, C_LOGLIK,
		       NULL, K, A0, opt, ainfo->prn);

	gretl_matrix_free(A0);

	if (err) {
	    fprintf(stderr, "BFGS_max returned %d\n", err);
	} 

	if (!save_lbfgs && (opt & OPT_L)) {
	    libset_set_bool(USE_LBFGS, 0);
	}
    }

    if (!err && ainfo->yscale != 1.0) {
	kalman_undo_y_scaling(ainfo, y, b, K);
    }

    if (!err) {
	gretl_model_set_int(pmod, "fncount", fncount);
	gretl_model_set_int(pmod, "grcount", grcount);
	err = kalman_arma_finish(pmod, ainfo, 
				 Z, pdinfo, K, b, 
				 opt, ainfo->prn);
    } 

 bailout:

    if (err) {
	pmod->errcode = err;
    }

    kalman_free(K);
    kalman_helper_free(kh);

    gretl_matrix_free(y);
    free_arma_X_matrix(ainfo, X);
    free(b);

    return err;
}

/* end of Kalman-specific material */

static int user_arma_init (double *coeff, arma_info *ainfo, int *init_done)
{
    PRN *prn = ainfo->prn;
    int i, nc = n_init_vals();

    if (nc == 0) {
	return 0;
    } else if (nc < ainfo->nc) {
	pprintf(prn, "ARMA initialization: need %d coeffs but got %d\n",
		ainfo->nc, nc);
	return E_DATA;
    }

    if (arma_exact_ml(ainfo)) {
	/* initialization is handled within BFGS */
	for (i=0; i<ainfo->nc; i++) {
	    coeff[i] = 0.0;
	}	
    } else {
	const gretl_matrix *m = get_init_vals();

	pprintf(prn, "\n%s: %s\n\n", _("ARMA initialization"), 
		_("user-specified values"));
	      
	for (i=0; i<ainfo->nc; i++) {
	    coeff[i] = gretl_vector_get(m, i);
	}
	free_init_vals();
    }

    *init_done = 1;

    return 0;
}

#if KALMAN_ARMA_INITH

static void delete_arma_OPG_info (arma_info *ainfo)
{
    free(ainfo->Z);
    ainfo->Z = NULL;

    gretl_matrix_free(ainfo->G);
    ainfo->G = NULL;

    free(ainfo->e);
    ainfo->e = NULL;

    doubles_array_free(ainfo->aux, ainfo->n_aux);
    ainfo->aux = NULL;
    ainfo->n_aux = 0;
}

#endif

/* Should we try Hannan-Rissanen initialization of ARMA
   coefficients? */

static int prefer_hr_init (arma_info *ainfo)
{
    int ret = 0;

    if (ainfo->q > 1 || ainfo->Q > 0) {
	ret = 1;
	if (ainfo->pqspec != NULL && *ainfo->pqspec != '\0') {
	    /* don't use for gappy arma (yet?) */
	    ret = 0;
	} else if (arma_xdiff(ainfo)) {
	    /* don't use for ARIMAX (yet?) */
	    ret = 0;
	} else if (ainfo->T < 100) {
	    /* unlikely to work well with small sample */
	    ret = 0;
	} else if (ainfo->p > 0 && ainfo->P > 0) {
	    /* not sure about this: HR catches the MA terms, but NLS
	       handles the seasonal/non-seasonal AR interactions better
	    */
	    ret = 0;
	} else if ((ainfo->P > 0 && ainfo->p >= ainfo->pd) ||
		   (ainfo->Q > 0 && ainfo->q >= ainfo->pd)) {
	    /* overlapping seasonal/non-seasonal orders screw things up */
	    ret = 0;
	} else if (ret && arma_exact_ml(ainfo)) {
	    /* screen for cases where we'll use NLS */
	    if (ainfo->P > 0) {
		ret = 0;
	    } else if (ainfo->p + ainfo->P > 0 && ainfo->nexo > 0) {
		ret = 0;
	    } else if (ainfo->Q > 0 && arma_missvals(ainfo)) {
		ret = 0;
	    }
	}
    }

#if ARMA_DEBUG
    fprintf(stderr, "prefer_hr_init? %s\n", ret? "yes" : "no");
#endif

    return ret;
}

/* estimate an ARIMA (0,d,0) x (0, D, 0) model via OLS */

static int arima_by_ls (const double **Z, const DATAINFO *pdinfo,
			arma_info *ainfo, MODEL *pmod)
{
    gretl_matrix *X;
    gretl_matrix *b, *u, *V;
    double x, s2;
    int i, t, k = ainfo->dX->cols;
    int err = 0;

    if (ainfo->ifc) {
	/* the constant will not have been included in ainfo->dX */
	X = gretl_matrix_alloc(ainfo->T, k + 1);
	if (X == NULL) {
	    return E_ALLOC;
	}
	for (i=0; i<=k; i++) {
	    for (t=0; t<ainfo->T; t++) {
		if (i == 0) {
		    gretl_matrix_set(X, t, i, 1.0);
		} else {
		    x = gretl_matrix_get(ainfo->dX, t, i-1);
		    gretl_matrix_set(X, t, i, x);
		}
	    }
	}
	k++;
    } else {
	X = ainfo->dX;
    }

    b = gretl_column_vector_alloc(k);
    u = gretl_column_vector_alloc(ainfo->T);
    V = gretl_matrix_alloc(k, k);

    if (b == NULL || u == NULL || V == NULL) {
	err = E_ALLOC;
    } else {
	gretl_vector y;

	y.rows = ainfo->T;
	y.cols = 1;
	y.val = ainfo->y + ainfo->t1;
	y.t1 = ainfo->t1;
	y.t2 = ainfo->t2;

	err = gretl_matrix_ols(&y, X, b, V, u, &s2);
    }

    if (!err) {
	pmod->ncoeff = k;
	pmod->full_n = pdinfo->n;
	err = gretl_model_allocate_storage(pmod);
    }
    
    if (!err) {
	for (i=0; i<k; i++) {
	    pmod->coeff[i] = b->val[i];
	}
	for (t=0; t<ainfo->T; t++) {
	    pmod->uhat[t + ainfo->t1] = u->val[t];
	}	
	err = gretl_model_write_vcv(pmod, V);
    }

    if (!err) {
    	pmod->ybar = gretl_mean(ainfo->t1, ainfo->t2, ainfo->y);
	pmod->sdy = gretl_stddev(ainfo->t1, ainfo->t2, ainfo->y);
	pmod->nobs = ainfo->T;
    }
    
    gretl_matrix_free(b);
    gretl_matrix_free(u);
    gretl_matrix_free(V);

    if (X != ainfo->dX) {
	gretl_matrix_free(X);
    }

    return err;
}

static int arma_via_OLS (arma_info *ainfo, const double *coeff, 
			 const double **Z, const DATAINFO *pdinfo,
			 MODEL *pmod)
{
    int err = 0;

    ainfo->flags |= ARMA_LS;

    if (arma_xdiff(ainfo)) {
	err = arima_by_ls(Z, pdinfo, ainfo, pmod);
    } else {
	err = arma_by_ls(coeff, Z, pdinfo, ainfo, pmod);
    }

    if (!err) {
	ArmaFlags f = (arma_exact_ml(ainfo))? ARMA_OLS : ARMA_LS;

	pmod->t1 = ainfo->t1;
	pmod->t2 = ainfo->t2;
	pmod->full_n = pdinfo->n;
	write_arma_model_stats(pmod, ainfo, Z, pdinfo);
	if (arma_exact_ml(ainfo)) {
	    ls_criteria(pmod);
	} else {
	    arma_model_add_roots(pmod, ainfo, pmod->coeff);
	}
	gretl_model_set_int(pmod, "arma_flags", f);
    }

    return err;
}

/* Set flag to indicate differencing of exogenous regressors, in the
   case of an ARIMAX model using native exact ML -- unless this is
   forbidden by OPT_Y (--y-diff-only).  Note that we don't do this
   when we're using conditional ML (BHHH).
*/

static void maybe_set_xdiff_flag (arma_info *ainfo, gretlopt opt)
{
    if (arma_exact_ml(ainfo) &&
	(ainfo->d > 0 || ainfo->D > 0) &&
	ainfo->nexo > 0 && !(opt & OPT_Y)) {
	ainfo->pflags |= ARMA_XDIFF;
    }
}

/* Set flag to allow NAs within the sample range for an
   ARMA model using native exact ML. FIXME : more work 
   is needed to get this right for the ARIMA case.
*/

static void maybe_allow_missvals (arma_info *ainfo)
{
#if 1 /* experimental: be permissive */
    if (arma_exact_ml(ainfo)) {
	ainfo->pflags |= ARMA_NAOK;
    }
#else /* more restrictive */
    if (arma_exact_ml(ainfo) && !arma_xdiff(ainfo) &&
	ainfo->P == 0 && ainfo->Q == 0) {
	ainfo->pflags |= ARMA_NAOK;
    }
#endif
}

MODEL arma_model (const int *list, const char *pqspec,
		  const double **Z, const DATAINFO *pdinfo, 
		  gretlopt opt, PRN *prn)
{
    double *coeff = NULL;
    MODEL armod;
    arma_info ainfo_s, *ainfo;
    int init_done = 0;
    int missv = 0, misst = 0;
    int err = 0;

    ainfo = &ainfo_s;
    arma_info_init(ainfo, opt, pqspec, pdinfo);
    ainfo->prn = set_up_verbose_printer(opt, prn);

    gretl_model_init(&armod); 

    err = incompatible_options(opt, OPT_C | OPT_L);

    if (!err) {
	ainfo->alist = gretl_list_copy(list);
	if (ainfo->alist == NULL) {
	    err = E_ALLOC;
	}
    } 

    if (!err) {
	err = arma_check_list(ainfo, Z, pdinfo, opt);
    }

    if (!err) {
	/* calculate maximum lag */
	maybe_set_xdiff_flag(ainfo, opt);
	calc_max_lag(ainfo);
    }

    if (!err) {
	/* adjust sample range if need be */
	maybe_allow_missvals(ainfo);
	err = arma_adjust_sample(ainfo, Z, pdinfo, &missv, &misst);
	if (err) {
	    gretl_errmsg_sprintf(_("Missing value encountered for "
				   "variable %d, obs %d"), missv, misst);
	} else if (missv > 0) {
	    set_arma_missvals(ainfo);
	}
    }

    if (!err) {
	/* allocate initial coefficient vector */
	coeff = malloc(ainfo->nc * sizeof *coeff);
	if (coeff == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err && (ainfo->d > 0 || ainfo->D > 0)) {
	err = arima_difference(ainfo, Z, pdinfo);
    }

    if (err) {
	goto bailout;
    }

    if (ainfo->p == 0 && ainfo->P == 0 &&
	ainfo->q == 0 && ainfo->Q == 0 &&
	arma_exact_ml(ainfo) && !arma_missvals(ainfo)) {
	/* pure "I" model, no NAs: OLS provides the MLE */
	err = arma_via_OLS(ainfo, NULL, Z, pdinfo, &armod);
	goto bailout;
    }

    /* initialize the coefficients: there are 3 possible methods */

    /* first pass: see if the user specified some values */
    err = user_arma_init(coeff, ainfo, &init_done);
    if (err) {
	goto bailout;
    }

    if (!arma_exact_ml(ainfo) && ainfo->q == 0 && ainfo->Q == 0) {
	/* for a pure AR model, the conditional MLE is least squares */
	const double *b = (init_done)? coeff : NULL;

	err = arma_via_OLS(ainfo, b, Z, pdinfo, &armod);
	goto bailout;
    }

    /* second pass: try Hannan-Rissanen init, if suitable */
    if (!init_done && prefer_hr_init(ainfo)) {
	hr_arma_init(coeff, Z, pdinfo, ainfo, &init_done);
    }

    /* third pass: estimate pure AR model by OLS or NLS */
    if (!err && !init_done) {
	err = ar_arma_init(coeff, Z, pdinfo, ainfo, &armod);
    }

    if (!err) {
	clear_model_xpx(&armod);
	if (arma_exact_ml(ainfo)) {
	    kalman_arma(coeff, Z, pdinfo, ainfo, &armod, opt);
	} else {
	    bhhh_arma(coeff, Z, pdinfo, ainfo, &armod, opt);
	}
    }

 bailout:

    if (err && !armod.errcode) {
	armod.errcode = err;
    }

    if (!armod.errcode) {
	gretl_model_smpl_init(&armod, pdinfo);
    }

    free(coeff);
    arma_info_cleanup(ainfo);

    /* cleanup in MA roots checker */
    bounds_checker_cleanup();

    if (ainfo->prn != prn) {
	close_down_verbose_printer(ainfo->prn);
    }

    return armod;
}
