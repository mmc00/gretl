#ifndef CEPHES_H
#define CEPHES_H

extern double MACHEP;
extern double MINLOG;
extern double MAXLOG;
extern double MAXNUM;

extern double PI, PIO4;
extern double SQRTH;
 
double cephes_exp (double);              /* unity.c */
double expx2 (double, int);              /* expx2.c */
double cephes_gamma (double);            /* gamma.c */
double igam (double, double);            /* igam.c */
double igamc (double, double);           /* igam.c */
double igami (double, double);           /* igami.c */
double incbet (double, double, double);  /* incbet.c */
double incbi (double, double, double);   /* incbi.c */
double lgam (double);                    /* gamma.c */
double cephes_log (double);              /* unity.c */
double ndtri (double);                   /* ndtri.c */
double p1evl (double, double *, int);    /* polevl.c */
double polevl (double, double *, int);   /* polevl.c */

int airy (double x, double *ai, double *aip, double *bi, double *bip);
double hyp2f1 (double a, double b, double c, double x);
double cephes_bessel_Jn (int n, double x);
double cephes_bessel_Yn (int n, double x);
double cephes_bessel_Jv (double n, double x);
double cephes_bessel_Yv (double n, double x);
double cephes_bessel_Iv (double v, double x);

#ifndef INFINITY
extern double INFINITY;
#endif

#ifdef INFINITIES
int isfinite (double);
#endif

#ifndef NAN
extern double NAN;
#endif

#ifdef NANS
int isnan (double);
#endif
 
#endif /* CEPHES_H */

