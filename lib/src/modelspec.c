/*
 *  Copyright (c) 2004 by Allin Cottrell
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

#include "libgretl.h"
#include "modelspec.h"
#include "gretl_private.h"

struct MODELSPEC_ {
    int ID;
    char *cmd;
    char *submask;
};

/**
 * model_ci_from_modelspec:
 * @spec: pointer to array of model specifications.
 * @i: index number of model within array.
 *
 * Returns: the command index (e.g. OLS, CORC) associated
 * with the model specification.
 */

int model_ci_from_modelspec (const MODELSPEC *spec, int i)
{
    char mword[9];
    int ci;

    if (spec[i].cmd == NULL) {
	fputs("Internal error: got NULL string in model_ci_from_modelspec\n",
	      stderr);
	return -1;
    }

    if (!sscanf(spec[i].cmd, "%8s", mword)) {
	ci = -1;
    } else {
	ci = gretl_command_number(mword);
    }

    return ci;
}

static int modelspec_n_allocated (const MODELSPEC *spec)
{
    int n = 0;

    if (spec != NULL) {
	while (spec[n].cmd != NULL) {
	    n++;
	}
	n++;
    }
    
    return n;
}

int modelspec_last_index (const MODELSPEC *spec)
{
    return modelspec_n_allocated(spec) - 2;
}

int modelspec_index_from_model_id (const MODELSPEC *spec, int ID)
{
    int i = 0, idx = -1;

    if (spec != NULL) {
	while (spec[i].cmd != NULL) {
	    if (spec[i].ID == ID) {
		idx = i;
		break;
	    }
	    i++;
	}
    }    

    return idx;
}

char *modelspec_get_command_by_id (MODELSPEC *spec, int ID)
{
    int i = modelspec_index_from_model_id(spec, ID);
    
    if (i < 0) return NULL;

    return spec[i].cmd;
}

void free_modelspec (MODELSPEC *spec)
{
    int i = 0;

    if (spec != NULL) {
	while (spec[i].cmd != NULL) {
	    free(spec[i].cmd);
	    if (spec[i].submask != NULL) {
		free(spec[i].submask);
	    }
	    i++;
	}
	free(spec);
    }
}

static int modelspec_expand (MODELSPEC **pmspec, int *idx)
{
    MODELSPEC *mspec;
    int m;

    if (*pmspec == NULL) {
	m = 0;
	mspec = malloc(2 * sizeof *mspec);
    } else {
	m = modelspec_n_allocated(*pmspec);
	mspec = realloc(*pmspec, (m + 1) * sizeof *mspec);
	m--;
    }
    if (mspec == NULL) return E_ALLOC;

    *pmspec = mspec;

    mspec[m].cmd = malloc(MAXLINE);
    if (mspec[m].cmd == NULL) return E_ALLOC;

    mspec[m].submask = NULL;

#ifdef MSPEC_DEBUG
    fprintf(stderr, "malloced modelspec[%d].cmd\n", m);
#endif

    /* sentinel */
    mspec[m+1].cmd = NULL;
    mspec[m+1].submask = NULL;

    *idx = m;

    return 0;
}

int modelspec_save (MODEL *pmod, MODELSPEC **pmspec)
{
    MODELSPEC *spec;
    int i;

    if (pmod->list == NULL) return 1;

    if (modelspec_expand(pmspec, &i)) {
	return E_ALLOC;
    }

    spec = *pmspec;

    sprintf(spec[i].cmd, "%s ", gretl_command_word(pmod->ci));
    
    if (pmod->ci == AR) {
	model_list_to_string(pmod->arinfo->arlist, spec[i].cmd);
	strcat(spec[i].cmd, "; ");
    }

    model_list_to_string(pmod->list, spec[i].cmd);

    if (pmod->submask != NULL) {
	int n = get_full_length_n();

	spec[i].submask = copy_submask(pmod->submask, n);
	if (spec[i].submask == NULL) return 1;
    }

    spec[i].ID = pmod->ID;

#ifdef MSPEC_DEBUG
    fprintf(stderr, "save_model_spec: cmd='%s', ID=%d\n", 
	    spec[i].cmd, spec[i].ID);
#endif

    return 0;
}

static int submask_match (char *s1, char *s2, int n)
{
    int t;

    for (t=0; t<n; t++) {
	if (s1[t] != s2[t]) return 0;
    }

    return 1;
}

/* check a model (or modelspec) against the datainfo to see if it may
   have been estimated on a different (subsampled) data set from the
   current one
*/

int model_sample_issue (const MODEL *pmod, MODELSPEC *spec, int i,
			const DATAINFO *pdinfo)
{
    int n = pdinfo->n;
    char *submask;

    if (pmod == NULL && spec == NULL) {
	return 0;
    }

    if (pmod != NULL) {
	submask = pmod->submask;
    } else {
	submask = spec[i].submask;
    }

    /* case: model (or modelspec) has no sub-sampling info recorded */
    if (submask == NULL) {
	/* if data set is not sub-sampled either, we're OK */
	if (pdinfo->submask == NULL) {
	    return 0;
	} else {
	    fputs(I_("dataset is subsampled, model is not\n"), stderr);
	    strcpy(gretl_errmsg, _("dataset is subsampled, model is not\n"));
	    return 1;
	}
    }

    /* case: model (or modelspec) has sub-sampling info recorded */
    if (pdinfo->submask == NULL) {
	fputs(I_("model is subsampled, dataset is not\n"), stderr);
	strcpy(gretl_errmsg, _("model is subsampled, dataset is not\n"));
	return 1;
    } else { 
	/* do the subsamples (model and current data set) agree? */
	if (submask_match(pdinfo->submask, submask, n)) {
	    return 0;
	} else {
	    strcpy(gretl_errmsg, _("model and dataset subsamples not the same\n"));
	    return 1;
	}
    }

    /* not reached */
    return 1;
}
