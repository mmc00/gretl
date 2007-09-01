/* private stuff for gretl CMD structure */

#ifndef CMD_PRIVATE_H
#define CMD_PRIVATE_H

#include "gretl_restrict.h"

typedef struct Laginfo_ Laginfo;

enum {
    CMD_NOLIST = 1 << 0,
    CMD_IGNORE = 1 << 1
};

#define cmd_nolist(c) (c->flags & CMD_NOLIST)
#define cmd_ignore(c) (c->flags & CMD_IGNORE)

struct CMD_ {
    char word[9];               /* command word */
    int ci;                     /* command index number */
    int context;                /* context for subsetted commands */
    int order;                  /* lag order, for various commands */
    int aux;                    /* auxiliary int (e.g. for VECM rank) */
    gretlopt opt;               /* option flags */
    char flags;                 /* internal flags */
    char savename[MAXSAVENAME]; /* name used to save an object from the command */
    int *list;                  /* list of variables by ID number */
    char *param;                /* general-purpose parameter to command */
    char *extra;                /* second parameter for some special uses */
    int err;                    /* error code */
    Laginfo *linfo;             /* struct for recording info on automatically
                                   generated lags */
};

typedef void (*EXEC_CALLBACK) (ExecState *, double ***, DATAINFO *);

struct ExecState_ {
    ExecFlags flags;
    CMD *cmd;
    PRN *prn;
    char *line;
    char runfile[MAXLEN];
    MODEL **models;
    equation_system *sys;
    gretl_restriction *rset;
    GRETL_VAR *var;
    DATAINFO *subinfo; /* record of incoming sub-sample for functions */
    int alt_model;
    int in_comment;
    EXEC_CALLBACK callback;
};

void gretl_exec_state_init (ExecState *s,
			    ExecFlags flags,
			    char *line,
			    CMD *cmd,
			    MODEL **models, 
			    PRN *prn);

void gretl_exec_state_clear (ExecState *s);

int maybe_exec_line (ExecState *s, double ***pZ, DATAINFO **ppdinfo,
		     int *funcerr);

#endif /* CMD_PRIVATE_H */
