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
#include "gretl_mpi.h"
#include <mpi.h>

#ifdef WIN32
# include <windows.h>
#else
# include <dlfcn.h>
#endif

/* Support for MPI in libgretl. On systems other than MS Windows
   We get the MPI symbols that we need from the address space of
   the calling program to avoid introducing a hard dependency on 
   libmpi; on Windows we call LoadLibrary() on msmpi.dll to the
   same effect.

   To use functions in this translation unit from elsewhere in
   libgretl one must first call gretl_MPI_init(), and then
   guard subsequent calls with "if (gretl_mpi_initialized())".
   Otherwise you'll get a segfault.

   For future reference, the MPI variants define the following
   specific symbols in mpi.h:

     Open MPI: OMPI_MAJOR_VERSION
     MPICH:    MPICH_VERSION
     MS-MPI:   MSMPI_VER
*/

#ifdef OMPI_MAJOR_VERSION
/* external constants that need to be loaded from the 
   Open MPI library */
static MPI_Comm mpi_comm_world;
static MPI_Datatype mpi_double;
static MPI_Datatype mpi_int;
static MPI_Op mpi_sum;
static MPI_Op mpi_prod;
static MPI_Op mpi_max;
static MPI_Op mpi_min;
#else
/* It seems that MPICH and MS-MPI just define these symbols
   as integer values in the header */
# define mpi_comm_world MPI_COMM_WORLD
# define mpi_double     MPI_DOUBLE
# define mpi_int        MPI_INT
# define mpi_sum        MPI_SUM
# define mpi_prod       MPI_PROD
# define mpi_max        MPI_MAX
# define mpi_min        MPI_MIN
#endif

#define MPI_DEBUG 0

static void *MPIhandle;       /* handle to the MPI library */
static int gretl_MPI_err;     /* initialization error record */
static int gretl_MPI_initted; /* are we initialized or not? */

/* renamed, pointerized versions of the MPI functions we need */
static int (*mpi_comm_rank) (MPI_Comm, int *);
static int (*mpi_comm_size) (MPI_Comm, int *);
static int (*mpi_error_class) (int, int *);
static int (*mpi_error_string) (int, char *, int *);
static int (*mpi_reduce) (void *, void *, int, MPI_Datatype, MPI_Op,
			  int, MPI_Comm);
static int (*mpi_bcast) (void *, int, MPI_Datatype, int, MPI_Comm);
static int (*mpi_send) (void *, int, MPI_Datatype, int, int, MPI_Comm);	       
static int (*mpi_recv) (void *, int, MPI_Datatype, int, int, MPI_Comm,
			MPI_Status *);
static int (*mpi_probe) (int, int, MPI_Comm, MPI_Status *);
static double (*mpi_wtime) (void);
static int (*mpi_initialized) (int *);

static void *mpiget (void *handle, const char *name, int *err)
{
#ifdef WIN32
    void *p = GetProcAddress(handle, name);
#else
    void *p = dlsym(handle, name);
#endif
    
    if (p == NULL) {
	printf("mpi_dlget: couldn't find '%s'\n", name);
	*err += 1;
    }

#if MPI_DEBUG
    else {
	printf("mpi_dlget: '%s' -> %p\n", name, p);
    }
#endif

    return p;
}

int gretl_MPI_init (void)
{
    int err = 0;

    if (gretl_MPI_initted) {
	return gretl_MPI_err;
    }

#if MPI_DEBUG
    printf("Loading MPI symbols...\n");
#endif

#ifdef WIN32
    MPIhandle = LoadLibrary("msmpi.dll");
#else
    MPIhandle = dlopen(NULL, RTLD_NOW);
#endif

    if (MPIhandle == NULL) {
	err = E_EXTERNAL;
	goto bailout;
    } 

    mpi_comm_rank    = mpiget(MPIhandle, "MPI_Comm_rank", &err);
    mpi_comm_size    = mpiget(MPIhandle, "MPI_Comm_size", &err);
    mpi_error_class  = mpiget(MPIhandle, "MPI_Error_class", &err);
    mpi_error_string = mpiget(MPIhandle, "MPI_Error_string", &err);
    mpi_reduce       = mpiget(MPIhandle, "MPI_Reduce", &err);
    mpi_bcast        = mpiget(MPIhandle, "MPI_Bcast", &err);
    mpi_send         = mpiget(MPIhandle, "MPI_Send", &err);
    mpi_recv         = mpiget(MPIhandle, "MPI_Recv", &err);
    mpi_probe        = mpiget(MPIhandle, "MPI_Probe", &err);
    mpi_wtime        = mpiget(MPIhandle, "MPI_Wtime", &err);
    mpi_initialized  = mpiget(MPIhandle, "MPI_Initialized", &err);

#ifdef OMPI_MAJOR_VERSION
    if (!err) {
	mpi_comm_world = (MPI_Comm) mpiget(MPIhandle, "ompi_mpi_comm_world", &err);
	mpi_double     = (MPI_Datatype) mpiget(MPIhandle, "ompi_mpi_double", &err);
	mpi_int        = (MPI_Datatype) mpiget(MPIhandle, "ompi_mpi_int", &err);
	mpi_sum        = (MPI_Op) mpiget(MPIhandle, "ompi_mpi_op_sum", &err);
	mpi_prod       = (MPI_Op) mpiget(MPIhandle, "ompi_mpi_op_prod", &err);
	mpi_max        = (MPI_Op) mpiget(MPIhandle, "ompi_mpi_op_max", &err);
	mpi_min        = (MPI_Op) mpiget(MPIhandle, "ompi_mpi_op_min", &err);
    }
#endif

    if (err) {
	close_plugin(MPIhandle);
	MPIhandle = NULL;
	err = E_EXTERNAL;
    }

 bailout:

#if MPI_DEBUG
    printf("load_MPI_symbols: returning %d\n", err);
#endif

    gretl_MPI_err = err;
    gretl_MPI_initted = 1;

    return err;
}

static void gretl_mpi_error (int *err)
{
    char msg1[BUFSIZ], msg2[BUFSIZ];
    int len, errclass;
    int id = 0;

    mpi_comm_rank(mpi_comm_world, &id);
    mpi_error_class(*err, &errclass);
    mpi_error_string(errclass, msg1, &len);
    mpi_error_string(*err, msg2, &len);
    gretl_errmsg_sprintf("%3d: %s %s\n", id, msg1, msg2);

    *err = E_EXTERNAL;
}

static int matrix_dims_check (int *rows, int *cols, int n,
			      Gretl_MPI_Op op)
{
    int i;

    for (i=0; i<n; i++) {
	if (rows[i] <= 0 || cols[i] <= 0) {
	    return E_DATA;
	}
	if (i > 0) {
	    if (op == GRETL_MPI_SUM || op == GRETL_MPI_HCAT) {
		if (rows[i] != rows[i-1]) {
		    return E_NONCONF;
		}
	    }
	    if (op == GRETL_MPI_SUM || op == GRETL_MPI_VCAT) {
		if (cols[i] != cols[i-1]) {
		    return E_NONCONF;
		}
	    }
	}
    }	    

    return 0;
}

static int matrix_reduce_alloc (int *rows, int *cols,
				int n, Gretl_MPI_Op op,
				gretl_matrix **pm,
				double **px)
{
    int i, r, c;
    int xsize = 0;
    int err = 0;

    if (op == GRETL_MPI_SUM) {
	r = rows[0]; 
	c = cols[0];
	xsize = r * c;
    } else if (op == GRETL_MPI_HCAT) {
	int cmax = 0;

	r = rows[0];
	c = 0;
	for (i=0; i<n; i++) {
	    c += cols[i];
	    if (cols[i] > cmax) {
		cmax = cols[i];
	    }
	}
	xsize = r * cmax;
    } else {
	int rmax = 0;

	c = cols[0];
	r = 0;
	for (i=0; i<n; i++) {
	    r += rows[i];
	    if (rows[i] > rmax) {
		rmax = rows[i];
	    }	    
	}
	xsize = rmax * c;
    }

    *pm = gretl_zero_matrix_new(r, c);

    if (*pm == NULL) {
	err = E_ALLOC;
    } else {
	*px = malloc(xsize * sizeof **px);
	if (*px == NULL) {
	    gretl_matrix_free(*pm);
	    *pm = NULL;
	    err = E_ALLOC;
	}
    }

    return err;
}

static int matrix_reduce_step (gretl_matrix *m, double *val,
			       int n, Gretl_MPI_Op op,
			       int *offset)
{
    int i;

    if (op == GRETL_MPI_SUM) {
	for (i=0; i<n; i++) {
	    m->val[i] += val[i];
	}
    } else if (op == GRETL_MPI_HCAT) {
	int k = *offset;

	for (i=0; i<n; i++) {
	    m->val[k++] = val[i];
	}
	*offset = k;
    } else {
	/* GRETL_MPI_VCAT */
	int rmin = *offset;
	int nrows = n / m->cols;
	int rmax = rmin + nrows;
	int j, k = 0;

	for (j=0; j<m->cols; j++) {
	    for (i=rmin; i<rmax; i++) {
		gretl_matrix_set(m, i, j, val[k++]);
	    }
	}
	*offset += nrows;
    }

    return 0;
}

static int gretl_matrix_reduce (void *sendp, 
				void *recvp,
				Gretl_MPI_Op op,
				int root,
				int id)
{
    gretl_matrix *sm = NULL;
    gretl_matrix *rm = NULL;
    double *val = NULL;
    int *rows = NULL;
    int *cols = NULL;
    int rc[2] = {0};
    int np = 0;
    int err = 0;

    if (op != GRETL_MPI_SUM &&
	op != GRETL_MPI_HCAT &&
	op != GRETL_MPI_VCAT) {
	return E_DATA;
    }

    if (id != root) {
	/* worker: send matrix dimensions to master */
	sm = sendp;
	rc[0] = sm->rows;
	rc[1] = sm->cols;
	err = mpi_send(rc, 2, mpi_int, 0, 0, mpi_comm_world);
    } else {
	/* root: gather dimensions from workers, check for
	   conformability and allocate storage
	*/
	int i;

	mpi_comm_size(mpi_comm_world, &np);

	rows = malloc((np-1) * sizeof *rows);
	cols = malloc((np-1) * sizeof *cols);
	if (rows == NULL || cols == NULL) {
	    err = E_ALLOC;
	}

	for (i=1; i<np && !err; i++) {
	    err = mpi_recv(rc, 2, mpi_int, i, 0, mpi_comm_world, 
			   MPI_STATUS_IGNORE);
	    if (!err) {
		rows[i-1] = rc[0];
		cols[i-1] = rc[1];
	    }
	}

	if (!err) {
	    err = matrix_dims_check(rows, cols, np-1, op);
	}

	if (!err) {
	    err = matrix_reduce_alloc(rows, cols, np-1, op, 
				      &rm, &val);
	}
    }

    if (!err) {
	if (id != root) {
	    /* workers send their data */
	    int sendsize = rc[0] * rc[1];

	    err = mpi_send(sm->val, sendsize, mpi_double, 0,
			   0, mpi_comm_world);
	} else {
	    /* root gathers data */
	    int i, recvsize, offset = 0;

	    for (i=1; i<np && !err; i++) {
		recvsize = rows[i-1] * cols[i-1];
		err = mpi_recv(val, recvsize, mpi_double, i, 0, 
			       mpi_comm_world, MPI_STATUS_IGNORE);
		if (!err) {
		    err = matrix_reduce_step(rm, val, recvsize, op,
					     &offset);
		}
	    }
	}
    }

    if (id == root) {
	/* clean up and set return value */
	free(val);
	free(rows);
	free(cols);
	if (!err) {
	    gretl_matrix **pm = recvp;

	    *pm = rm;
	} else {
	    gretl_matrix_free(rm);
	}
    }

    return err;
}

static int gretl_scalar_reduce (void *sendp, 
				void *recvp,
				Gretl_MPI_Op op,
				int root,
				int id)
{
    double local_x = 0.0;
    MPI_Op mpi_op;

    if (op == GRETL_MPI_SUM) {
	mpi_op = mpi_sum;
    } else if (op == GRETL_MPI_PROD) {
	mpi_op = mpi_prod;
    } else if (op == GRETL_MPI_MAX) {
	mpi_op = mpi_max;
    } else if (op == GRETL_MPI_MIN) {
	mpi_op = mpi_min;
    } else {
	return E_DATA;
    }

    if (id != root) {
	local_x = *(double *) sendp;
    }

    return mpi_reduce(&local_x, recvp, 1, mpi_double, 
		      mpi_op, 0, mpi_comm_world);
}

/**
 * gretl_mpi_reduce:
 * @sendp: address of object to be broadcast.
 * @recvp: location to receive the reduced value.
 * @type: the type of the object to which @p points.
 * @op: the reduction operation to perform.
 *
 * Reduces to @recvp the value referenced by @sendp, from 
 * MPI_COMM_WORLD. At present @type must be GRETL_TYPE_MATRIX, 
 * in which case @sendp should be a (*gretl_matrix) pointer
 * and @recvp a (**gretl_matrix) pointer, or GRETL_TYPE_DOUBLE,
 * in which case both @sendp and @recvp should be (*double) 
 * pointers.
 *
 * Returns: 0 on successful completion, non-zero code otherwise.
 **/

int gretl_mpi_reduce (void *sendp, void *recvp,
		      GretlType type, Gretl_MPI_Op op)
{
    int self, root = 0;
    int err = 0;

    if (type != GRETL_TYPE_DOUBLE && 
	type != GRETL_TYPE_MATRIX) {
	return E_DATA;
    }

    mpi_comm_rank(mpi_comm_world, &self);

    if (type == GRETL_TYPE_DOUBLE) {
	err = gretl_scalar_reduce(sendp, recvp, op, root, self);
    } else {
	err = gretl_matrix_reduce(sendp, recvp, op, root, self);
    }

    return err;
}

int gretl_matrix_mpi_bcast (gretl_matrix **pm)
{
    gretl_matrix *m = NULL;
    int rc[2];
    int root = 0;
    int id, err;

    mpi_comm_rank(mpi_comm_world, &id);

    if (id == root) {
	m = *pm;
	rc[0] = m->rows;
	rc[1] = m->cols;
    }

    /* broadcast the matrix dimensions first */
    err = mpi_bcast(rc, 2, mpi_int, root, mpi_comm_world);

    if (!err && id > 0) {
	/* everyone but root needs to allocate space */
	*pm = m = gretl_matrix_alloc(rc[0], rc[1]);
	if (m == NULL) {
	    return E_ALLOC;
	}
    }

    if (!err) {
	/* broadcast the matrix content */
	int n = rc[0] * rc[1];

	err = mpi_bcast(m->val, n, mpi_double, root, 
			mpi_comm_world);
    }

    if (err) {
	gretl_mpi_error(&err);
    }

    return err;
}

static int gretl_scalar_mpi_bcast (double *px)
{
    int err, root = 0;

    err = mpi_bcast(px, 1, mpi_double, root, mpi_comm_world);

    if (err) {
	gretl_mpi_error(&err);
    }

    return err;
}

/**
 * gretl_mpi_bcast:
 * @p: the location of the object to be broadcast.
 * @type: the type of the object to which @p points.
 *
 * Broadcasts the value referenced by @p to MPI_COMM_WORLD.
 * At present @type must be GRETL_TYPE_MATRIX, in which case
 * @p should be a (**gretl_matrix) pointer, or GRETL_TYPE_DOUBLE,
 * in which case @p should be a (*double) pointer.
 *
 * Returns: 0 on successful completion, non-zero code otherwise.
 **/

int gretl_mpi_bcast (void *p, GretlType type)
{
    if (type == GRETL_TYPE_DOUBLE) {
	return gretl_scalar_mpi_bcast((double *) p);
    } else if (type == GRETL_TYPE_MATRIX) {
	return gretl_matrix_mpi_bcast((gretl_matrix **) p);
    } else {
	return E_DATA;
    }
}

enum {
    TAG_MATRIX_DIM = 1,
    TAG_MATRIX_VAL,
    TAG_SCALAR_VAL
};

int gretl_matrix_mpi_send (const gretl_matrix *m, int dest)
{
    int rc[2] = {m->rows, m->cols};
    int err;

    err = mpi_send(rc, 2, mpi_int, dest, TAG_MATRIX_DIM, 
		   mpi_comm_world);

    if (!err) {
	int n = m->rows * m->cols;

	err = mpi_send(m->val, n, mpi_double, dest, TAG_MATRIX_VAL,
		       mpi_comm_world);
    }

    if (err) {
	gretl_mpi_error(&err);
    }    

    return err;
}

static int gretl_scalar_mpi_send (double *px, int dest)
{
    int err;

    err = mpi_send(px, 1, mpi_double, dest, TAG_SCALAR_VAL, 
		   mpi_comm_world);

    if (err) {
	gretl_mpi_error(&err);
    }    

    return err;
}

/**
 * gretl_mpi_send:
 * @p: pointer to the object to be sent.
 * @type: the type of the object.
 * @dest: the MPI rank of the destination.
 *
 * Sends the value referenced by @p to the MPI process with
 * rank @dest. At present @type must be GRETL_TYPE_MATRIX, 
 * in which case  @p should be a (*gretl_matrix) pointer, or
 * GRETL_TYPE_DOUBLE, in which case @p should be a (*double)
 * pointer.
 *
 * Returns: 0 on successful completion, non-zero code otherwise.
 **/

int gretl_mpi_send (void *p, GretlType type, int dest)
{
    if (type == GRETL_TYPE_DOUBLE) {
	return gretl_scalar_mpi_send((double *) p, dest);
    } else if (type == GRETL_TYPE_MATRIX) {
	return gretl_matrix_mpi_send((gretl_matrix *) p, dest);
    } else {
	return E_DATA;
    }
}

gretl_matrix *gretl_matrix_mpi_receive (int source, 
					int *err)
{
    gretl_matrix *m = NULL;
    int rc[2];

    *err = mpi_recv(rc, 2, mpi_int, source, TAG_MATRIX_DIM, 
		    mpi_comm_world, MPI_STATUS_IGNORE);

    if (!*err) {
	m = gretl_matrix_alloc(rc[0], rc[1]);
	if (m == NULL) {
	    *err = E_ALLOC;
	} else {
	    int n = rc[0] * rc[1];

	    *err = mpi_recv(m->val, n, mpi_double, source,
			    TAG_MATRIX_VAL, mpi_comm_world,
			    MPI_STATUS_IGNORE);
	    if (*err) {
		gretl_mpi_error(err);
	    }
	}
    }

    return m;
}

double gretl_scalar_mpi_receive (int source, int *err)
{
    double x = NADBL;

    *err = mpi_recv(&x, 1, mpi_double, source, TAG_SCALAR_VAL,
		    mpi_comm_world, MPI_STATUS_IGNORE);
    if (*err) {
	gretl_mpi_error(err);
    }

    return x;
}

int gretl_mpi_receive (int source, GretlType *type, 
		       gretl_matrix **pm,
		       double *px)
{
    MPI_Status status;
    int err = 0;

    mpi_probe(source, MPI_ANY_TAG, mpi_comm_world, &status);

    if (status.MPI_TAG == TAG_SCALAR_VAL) {
	*px = gretl_scalar_mpi_receive(source, &err);
	*type = GRETL_TYPE_DOUBLE;
    } else if (status.MPI_TAG == TAG_MATRIX_DIM) {
	*pm = gretl_matrix_mpi_receive(source, &err);
	*type = GRETL_TYPE_MATRIX;
    } else {
	err = E_DATA;
    }

    return err;
}

#if 0 /* do we want this? */

int gretl_matrix_mpi_send_cols (const gretl_matrix *m, 
				int j, int ncols)
{
    int rc[2] = {m->rows, ncols};
    int n = m->rows * ncols;
    int err;

    err = mpi_send(rc, 2, mpi_int, j, 0, mpi_comm_world);

    if (!err) {
	void *ptr = m->val + (j-1)*n;

	err = mpi_send(ptr, n, mpi_double, j, 0, mpi_comm_world);
    }

    if (err) {
	gretl_mpi_error(&err);
    }

    return err;
}

#endif

/* MPI timer */

static double mpi_dt0;

void gretl_mpi_stopwatch_init (void)
{
    mpi_dt0 = mpi_wtime();
}

double gretl_mpi_stopwatch (void)
{
    double dt1 = mpi_wtime();
    double x = dt1 - mpi_dt0;

    mpi_dt0 = dt1;

    return x;
}

/* end MPI timer */

int gretl_mpi_rank (void)
{
    int id;

    mpi_comm_rank(mpi_comm_world, &id);
    return id;
}

int gretl_mpi_initialized (void)
{
    static int ret = -1;

    if (!gretl_MPI_initted) {
	return 0;
    }

    if (ret < 0) {
	mpi_initialized(&ret);
	ret = ret > 0;
    }

    return ret;
}


