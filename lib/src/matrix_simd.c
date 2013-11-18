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

/* All code here is conditional on either AVX or SSE */

#define SHOW_SIMD 0

static double *mval_realloc (gretl_matrix *m, int n)
{
    double *newval = mval_malloc(n * sizeof(double));

    if (newval != NULL && m->val != NULL) {
	int old_n = m->rows * m->cols;

	n = n < old_n ? n : old_n;
	memcpy(newval, m->val, n * sizeof(double));
	mval_free(m->val);
    }

    return newval;
}

#ifdef USE_AVX

static void gretl_matrix_simd_add_to (gretl_matrix *a,
				      const gretl_matrix *b,
				      int n)
{
    double *ax = a->val;
    const double *bx = b->val;
    int i, imax = n / 4;
    int rem = n % 4;

#if SHOW_SIMD
    fprintf(stderr, "AVX: gretl_matrix_simd_add_to (%d x %d)\n",
	    a->rows, a->cols);
#endif

    for (i=0; i<imax; i++) {
	/* add 4 doubles in parallel */
	__m256d Ymm_A = _mm256_load_pd(ax);
	__m256d Ymm_B = _mm256_load_pd(bx);
	__m256d Ymm_C = _mm256_add_pd(Ymm_A, Ymm_B);
	_mm256_store_pd(ax, Ymm_C);
	ax += 4;
	bx += 4;
    }

    for (i=0; i<rem; i++) {
	ax[i] += bx[i];
    }
}

static void gretl_matrix_simd_subt_from (gretl_matrix *a,
					 const gretl_matrix *b,
					 int n)
{
    double *ax = a->val;
    const double *bx = b->val;
    int i, imax = n / 4;
    int rem = n % 4;

#if SHOW_SIMD
    fprintf(stderr, "AVX: gretl_matrix_simd_subt_from (%d x %d)\n",
	    a->rows, a->cols);
#endif

    for (i=0; i<imax; i++) {
	/* subtract 4 doubles in parallel */
	__m256d Ymm_A = _mm256_load_pd(ax);
	__m256d Ymm_B = _mm256_load_pd(bx);
	__m256d Ymm_C = _mm256_sub_pd(Ymm_A, Ymm_B);
	_mm256_store_pd(ax, Ymm_C);
	ax += 4;
	bx += 4;
    }

    for (i=0; i<rem; i++) {
	ax[i] -= bx[i];
    }
}

#else /* SSE */

static void gretl_matrix_simd_add_to (gretl_matrix *a,
				      const gretl_matrix *b,
				      int n)
{
    double *ax = a->val;
    const double *bx = b->val;
    int i, imax = n / 2;

#if SHOW_SIMD
    fprintf(stderr, "SSE: gretl_matrix_simd_add_to\n");
#endif

    for (i=0; i<imax; i++) {
	/* add 2 doubles in parallel */
	__m128d Xmm_A = _mm_load_pd(ax);
	__m128d Xmm_B = _mm_load_pd(bx);
	__m128d Xmm_C = _mm_add_pd(Xmm_A, Xmm_B);
	_mm_store_pd(ax, Xmm_C);
	ax += 2;
	bx += 2;
    }

    if (n % 2) {
	ax[0] += bx[0];
    }
}

static void gretl_matrix_simd_subt_from (gretl_matrix *a,
					 const gretl_matrix *b,
					 int n)
{
    double *ax = a->val;
    const double *bx = b->val;
    int i, imax = n / 2;

#if SHOW_SIMD
    fprintf(stderr, "SSE: gretl_matrix_simd_subt_from\n");
#endif

    for (i=0; i<imax; i++) {
	/* subtract 2 doubles in parallel */
	__m128d Xmm_A = _mm_load_pd(ax);
	__m128d Xmm_B = _mm_load_pd(bx);
	__m128d Xmm_C = _mm_sub_pd(Xmm_A, Xmm_B);
	_mm_store_pd(ax, Xmm_C);
	ax += 2;
	bx += 2;
    }

    if (n % 2) {
	ax[0] -= bx[0];
    }
}

#endif /* AVX vs SSE */

#ifdef USE_AVX

static void gretl_matrix_simd_add (const gretl_matrix *a,
				   const gretl_matrix *b,
				   gretl_matrix *c)
{
    const double *ax = a->val;
    const double *bx = b->val;
    double *cx = c->val;
    int i, n = a->rows * a->cols;
    int imax = n / 4;
    int rem = n % 4;

#if SHOW_SIMD
    fprintf(stderr, "AVX: gretl_matrix_simd_add (%d x %d)\n",
	    a->rows, a->cols);
#endif

    for (i=0; i<imax; i++) {
	/* process 4 doubles in parallel */
	__m256d Ymm_A = _mm256_load_pd(ax);
	__m256d Ymm_B = _mm256_load_pd(bx);
	__m256d Ymm_C = _mm256_add_pd(Ymm_A, Ymm_B);
	_mm256_store_pd(cx, Ymm_C);
	cx += 4;
	ax += 4;
	bx += 4;
    }

    for (i=0; i<rem; i++) {
	cx[i] = ax[i] + bx[i];
    }
}

static void gretl_matrix_simd_subtract (const gretl_matrix *a,
					const gretl_matrix *b,
					gretl_matrix *c)
{
    const double *ax = a->val;
    const double *bx = b->val;
    double *cx = c->val;
    int i, n = a->rows * a->cols;
    int imax = n / 4;
    int rem = n % 4;

#if SHOW_SIMD
    fprintf(stderr, "AVX: gretl_matrix_simd_subtract (%d x %d)\n",
	    a->rows, a->cols);
#endif

    for (i=0; i<imax; i++) {
	/* process 4 doubles in parallel */
	__m256d Ymm_A = _mm256_load_pd(ax);
	__m256d Ymm_B = _mm256_load_pd(bx);
	__m256d Ymm_C = _mm256_sub_pd(Ymm_A, Ymm_B);
	_mm256_store_pd(cx, Ymm_C);
	cx += 4;
	ax += 4;
	bx += 4;
    }

    for (i=0; i<rem; i++) {
	cx[i] = ax[i] - bx[i];
    }
}

#else /* SSE */

static void gretl_matrix_simd_add (const gretl_matrix *a,
				   const gretl_matrix *b,
				   gretl_matrix *c)
{
    const double *ax = a->val;
    const double *bx = b->val;
    double *cx = c->val;
    int i, n = a->rows * a->cols;
    int imax = n / 2;

#if SHOW_SIMD
    fprintf(stderr, "SSE: gretl_matrix_simd_add\n");
#endif
			
    for (i=0; i<imax; i++) {
	/* process 2 doubles in parallel */
	__m128d Xmm_A = _mm_load_pd(ax);
	__m128d Xmm_B = _mm_load_pd(bx);
	__m128d Xmm_C = _mm_add_pd(Xmm_A, Xmm_B);
	_mm_store_pd(cx, Xmm_C);
	cx += 2;
	ax += 2;
	bx += 2;
    }

    if (n % 2) {
	cx[0] = ax[0] + bx[0];
    }
}

static void gretl_matrix_simd_subtract (const gretl_matrix *a,
					const gretl_matrix *b,
					gretl_matrix *c)
{
    const double *ax = a->val;
    const double *bx = b->val;
    double *cx = c->val;
    int i, n = a->rows * a->cols;
    int imax = n / 2;

#if SHOW_SIMD
    fprintf(stderr, "SSE: gretl_matrix_simd_subtract\n");
#endif
			
    for (i=0; i<imax; i++) {
	/* process 2 doubles in parallel */
	__m128d Xmm_A = _mm_load_pd(ax);
	__m128d Xmm_B = _mm_load_pd(bx);
	__m128d Xmm_C = _mm_sub_pd(Xmm_A, Xmm_B);
	_mm_store_pd(cx, Xmm_C);
	cx += 2;
	ax += 2;
	bx += 2;
    }

    if (n % 2) {
	cx[0] = ax[0] - bx[0];
    }
}

#endif /* AVX vs SSE */

#ifdef USE_AVX

/* fast but restrictive: both A and B must be 4 x 4 */

void gretl_matrix_avx_mul4 (const double *aval,
			    const double *bval,
			    double *cval)
{
    __m256d b1, b2, b3, b4;
    __m256d mul, col;
    int j;

    /* load the columns of A */
    __m256d a1 = _mm256_load_pd(aval);
    __m256d a2 = _mm256_load_pd(aval + 4);
    __m256d a3 = _mm256_load_pd(aval + 8);
    __m256d a4 = _mm256_load_pd(aval + 12);

    for (j=0; j<4; j++) {
	/* broadcast the elements of col j of B */
        b1 = _mm256_broadcast_sd(&bval[4*j]);
        b2 = _mm256_broadcast_sd(&bval[4*j + 1]);
        b3 = _mm256_broadcast_sd(&bval[4*j + 2]);
        b4 = _mm256_broadcast_sd(&bval[4*j + 3]);

	mul = _mm256_mul_pd(b1, a1);
	b1  = _mm256_mul_pd(b2, a2);
	col = _mm256_add_pd(mul, b1);
	mul = _mm256_mul_pd(b3, a3);
        b2  = _mm256_mul_pd(b4, a4);
	b3  = _mm256_add_pd(mul, b2);
	col = _mm256_add_pd(col, b3);

	_mm256_store_pd(&cval[4*j], col);
    }
}

/* Note: this is probably usable only for k <= 8 (shortage of AVX
   registers). It is much faster if m is a multiple of 4; n is
   unconstrained.
*/

static void gretl_matrix_simd_mul (const gretl_matrix *A,
				   const gretl_matrix *B,
				   gretl_matrix *C)
{
    int m = A->rows;
    int n = B->cols;
    int k = A->cols;
    const double *aval = A->val;
    const double *bval = B->val;
    double *cval = C->val;
    int hmax, hrem, i, j;

#if SHOW_SIMD
    fprintf(stderr, "AVX: gretl_matrix_simd_mul (MNK = %d, %d, %d)\n",
	    m, n, k);
#endif

    if (m == 4 && n == 4 && k == 4) {
	gretl_matrix_avx_mul4(aval, bval, cval);
	return;
    }

    hmax = m / 4;
    hrem = m % 4;

    if (m >= 4) {
	__m256d a[k], b[k];
	__m256d mult, ccol;
	int h;

	for (h=0; h<hmax; h++) {
	    /* loop across the k columns of A, loading
	       4 elements from each
	     */
	    if (hrem) {
		/* unaligned */
		for (j=0; j<k; j++) {
		    a[j] = _mm256_loadu_pd(aval + j*m);
		}
	    } else {
		/* aligned */
		for (j=0; j<k; j++) {
		    a[j] = _mm256_load_pd(aval + j*m);
		}
	    }

	    /* loop across the n columns of B */
	    for (j=0; j<n; j++) {
		/* broadcast the k elements of col j of B */
		for (i=0; i<k; i++) {
		    b[i] = _mm256_broadcast_sd(&bval[j*k + i]);
		}

		/* cumulate the products */
		ccol = _mm256_setzero_pd();
		for (i=0; i<k; i++) {
		    mult = _mm256_mul_pd(b[i], a[i]);
		    ccol = _mm256_add_pd(ccol, mult);
		}

		/* write 4 rows of each column of C */
		if (hrem) {
		    /* unaligned */
		    _mm256_storeu_pd(&cval[m*j], ccol);
		} else {
		    /* aligned */
		    _mm256_store_pd(&cval[m*j], ccol);
		}
		    
	    }

	    /* advance by the number of doubles we can store in
	       one go */
	    aval += 4;
	    cval += 4;
	}
    }
    
    if (hrem >= 2) {
	/* do a single 128-bit run */
	__m128d a[k], b[k];
	__m128d mult, ccol;
	int realign = m % 2;

	if (realign) {
	    for (j=0; j<k; j++) {
		a[j] = _mm_loadu_pd(aval + j*m);
	    }
	} else {
	    for (j=0; j<k; j++) {
		a[j] = _mm_load_pd(aval + j*m);
	    }
	}

	for (j=0; j<n; j++) {
	    for (i=0; i<k; i++) {
		b[i] = _mm_set1_pd(bval[j*k + i]);
	    }

	    ccol = _mm_setzero_pd();
	    for (i=0; i<k; i++) {
		mult = _mm_mul_pd(b[i], a[i]);
		ccol = _mm_add_pd(ccol, mult);
	    }

	    if (realign) {
		_mm_storeu_pd(&cval[m*j], ccol);
	    } else {
		_mm_store_pd(&cval[m*j], ccol);
	    }
	}
	hrem -= 2;
	aval += 2;
	cval += 2;
    }
    
    if (hrem) {
	/* odd-valued m: compute the last row */
	double ccol, a[k];

	for (j=0; j<k; j++) {
	    a[j] = aval[j*m];
	}

	for (j=0; j<n; j++) {
	    ccol = 0.0; 
	    for (i=0; i<k; i++) {
		ccol += bval[j*k + i] * a[i];
	    }
	    cval[m*j] = ccol;
	}
    }
}

#else /* SSE */

/* Note: this is probably usable only for k <= 8 (shortage of SSE
   registers). It is faster if m is a multiple of 2; but n is
   unconstrained.
*/

static void gretl_matrix_simd_mul (const gretl_matrix *A,
				   const gretl_matrix *B,
				   gretl_matrix *C)
{
    int m = A->rows;
    int n = B->cols;
    int k = A->cols;
    const double *aval = A->val;
    const double *bval = B->val;
    double *cval = C->val;
    int hmax = m / 2;
    int hrem = m % 2;
    int i, j;

#if SHOW_SIMD
    fprintf(stderr, "SSE: gretl_matrix_simd_mul\n");
#endif

    if (m >= 2) {
	__m128d a[k], b[k];
	__m128d mult, ccol;
	int h;

	for (h=0; h<hmax; h++) {
	    /* loop across the k columns of A, loading
	       2 elements from each
	    */
	    if (hrem) {
		for (j=0; j<k; j++) {
		    a[j] = _mm_loadu_pd(aval + j*m);
		}
	    } else {
		for (j=0; j<k; j++) {
		    a[j] = _mm_load_pd(aval + j*m);
		}
	    }

	    /* loop across the n columns of B */
	    for (j=0; j<n; j++) {
		/* broadcast the k elements of col j of B */
		for (i=0; i<k; i++) {
		    b[i] = _mm_set1_pd(bval[j*k + i]);
		}

		/* cumulate the products */
		ccol = _mm_setzero_pd();
		for (i=0; i<k; i++) {
		    mult = _mm_mul_pd(b[i], a[i]);
		    ccol = _mm_add_pd(ccol, mult);
		}

		/* write 2 rows of each column of C */
		if (hrem) {
		    _mm_storeu_pd(&cval[m*j], ccol);
		} else {
		    _mm_store_pd(&cval[m*j], ccol);
		}
	    }

	    /* advance by the number of doubles we can store in
	       one go */
	    aval += 2;
	    cval += 2;
	}
    }

    if (hrem) {
	/* odd-valued m: compute the last row */
	double ccol, a[k];

	for (j=0; j<k; j++) {
	    a[j] = aval[j*m];
	}

	for (j=0; j<n; j++) {
	    ccol = 0.0; 
	    for (i=0; i<k; i++) {
		ccol += bval[j*k + i] * a[i];
	    }
	    cval[m*j] = ccol;
	}
    }
}

#endif /* AVX vs SIMD */
