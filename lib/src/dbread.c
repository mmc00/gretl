/*
 *   Copyright (c) by Allin Cottrell
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

/* dbread.c for gretl */

#include "libgretl.h"

#ifdef USE_GTK2
# include <glib.h>
#endif

#define DB_DEBUG 0

#define RECNUM long
#define NAMELENGTH 16
#define RATSCOMMENTLENGTH 80
#define RATSCOMMENTS 2
#define RATS_PARSE_ERROR -999

typedef struct {
    long daynumber;                /* Number of days from 1-1-90
				      to year, month, day */
    short panel;                   /* 1 for panel set, 2 for intraday
				      date set , 0 o.w. */
#define LINEAR    0                /* Single time direction */
#define RATSPANEL 1                /* panel:period */    
#define INTRADAY  2                /* date:intraday period */
    long panelrecord;              /* Size of panel or 
				      number of periods per day */
    short dclass;                  /* See definitions below */
#define UNDATEDCLASS   0           /* No time series properties */
#define IRREGULARCLASS 1           /* Time series (irregular) */
#define PERYEARCLASS   2           /* x periods / year */
#define PERWEEKCLASS   3           /* x periods / week */
#define DAILYCLASS     4           /* x days / period */
    long info;                     /* Number of periods per year or
				      per week */
    short digits;                  /* Digits for representing panel
				      or intraday period */
    short year,month,day;          /* Starting year, month, day */
} DATEINFO;

typedef struct {
    RECNUM back_point;             /* Pointer to previous series */
    RECNUM forward_point;          /* Pointer to next series */
    short back_class;              /* Reserved.  Should be 0 */
    short forward_class;           /* Reserved.  Should be 0 */
    RECNUM first_data;             /* First data record */
    char series_name[NAMELENGTH];  /* Series name */
    DATEINFO date_info;            /* Dating scheme for this series */
    long datapoints;               /* Number of data points */
    short data_type;               /* real, char, complex.
                                      Reserved.  Should be 0 */
    short digits;                  /* . + digit count for representation
				      (0 = unspecified) */
    short misc1;                   /* For future expansion should be 0 */
    short misc2;
    short comment_lines;           /* Number of comment lines (0,1,2) */
    char series_class[NAMELENGTH]; /* Series class. Not used, blank */
    char comments[RATSCOMMENTS][RATSCOMMENTLENGTH];
    char pad[10];
} RATSDirect;

typedef struct {
    RECNUM back_point;             /* Previous record (0 for first) */
    RECNUM forward_point;          /* Next record (0 for last) */
    double data[31];               /* Data */
} RATSData;

static char db_name[MAXLEN];
static int db_type;

static int cli_add_db_data (double **dbZ, SERIESINFO *sinfo, 
			    double ***pZ, DATAINFO *pdinfo,
			    CompactMethod method, int dbv);

static FILE *open_binfile (const char *dbbase, int code, int offset, int *err)
{
    char dbbin[MAXLEN];
    FILE *fp = NULL;

    strcpy(dbbin, dbbase);
    if (code == GRETL_NATIVE_DB) {
	if (strstr(dbbin, ".bin") == NULL) {
	    strcat(dbbin, ".bin");
	}
    } else {
	if (strstr(dbbin, ".bn7") == NULL) {
	    strcat(dbbin, ".bn7");
	}
    }	

    fp = gretl_fopen(dbbin, "rb");

    if (fp == NULL) {
	*err = DB_NOT_FOUND;
    } else if (fseek(fp, (long) offset, SEEK_SET)) {
	*err = DB_PARSE_ERROR;
	fclose(fp);
	fp = NULL;
    }

    return fp;
}

int get_native_db_data (const char *dbbase, SERIESINFO *sinfo, 
			double **Z)
{
    char numstr[32];
    FILE *fp;
    dbnumber x;
    int t, err = 0;

    fp = open_binfile(dbbase, GRETL_NATIVE_DB, sinfo->offset, &err);
    if (err) {
	return err;
    }

    for (t=0; t<sinfo->nobs && !err; t++) {
	if (fread(&x, sizeof x, 1, fp) != 1) {
	    err = DB_PARSE_ERROR;
	} else {
	    sprintf(numstr, "%.7g", (double) x); /* N.B. converting a float */
	    Z[1][t] = atof(numstr);
	    if (Z[1][t] == DBNA) {
		Z[1][t] = NADBL;
	    }
	}
    }

    fclose(fp);

    return err;
}

int get_pcgive_db_data (const char *dbbase, SERIESINFO *sinfo, 
			double **Z)
{
    FILE *fp;
    double x;
    int t, err = 0;

    fp = open_binfile(dbbase, GRETL_PCGIVE_DB, sinfo->offset, &err);
    if (err) {
	return err;
    }

    for (t=0; t<sinfo->nobs; t++) {
	if (fread(&x, sizeof x, 1, fp) != 1) {
	    err = E_DATA;
	    break;
	} else if (x == -9999.99 || isnan(x)) {
	    Z[1][t] = NADBL;
	    err = DB_MISSING_DATA;
	} else {
	    Z[1][t] = x;
	}
    }

    fclose(fp);

    return err;
}

static void get_native_series_comment (SERIESINFO *sinfo, const char *s)
{
    size_t n = strlen(sinfo->varname);
    const char *p = s + n + 1;
    int i;

    while (*p) {
	if (isspace(*p)) p++;
	else break;
    }

    *sinfo->descrip = 0;
    strncat(sinfo->descrip, p, MAXLABEL - 1);

    n = strlen(sinfo->descrip) - 1;
    
    for (i=n; i>0; i--) {
	if (isspace(sinfo->descrip[i])) {
	    sinfo->descrip[i] = 0;
	} else if (sinfo->descrip[i] == '\r' || sinfo->descrip[i] == '\n') {
	    sinfo->descrip[i] = 0;
	} else {
	    break;
	}
    }
}

static int get_native_series_pd (SERIESINFO *sinfo, char pdc)
{
    sinfo->pd = 1;
    sinfo->undated = 0;

    if (pdc == 'M') sinfo->pd = 12;
    else if (pdc == 'Q') sinfo->pd = 4;
    else if (pdc == 'B') sinfo->pd = 5;
    else if (pdc == 'S') sinfo->pd = 6;
    else if (pdc == 'D') sinfo->pd = 7;
    else if (pdc == 'U') sinfo->undated = 1;
    else return 1;

    return 0;
}

static int get_native_series_obs (SERIESINFO *sinfo, 
				  const char *stobs,
				  const char *endobs)
{
    if (strchr(stobs, '/')) { /* calendar data */
	const char *q = stobs;
	const char *p = strchr(stobs, '/');

	if (p - q == 4) {
	    strcpy(sinfo->stobs, q + 2);
	}
	q = endobs;
	p = strchr(endobs, '/');
	if (p && p - q == 4) {
	    strcpy(sinfo->endobs, q + 2);
	}
    } else {
	*sinfo->stobs = 0;
	*sinfo->endobs = 0;
	strncat(sinfo->stobs, stobs, 8);
	strncat(sinfo->endobs, endobs, 8);
    }

    return 0;
}

static int 
get_native_series_info (const char *series, SERIESINFO *sinfo)
{
    FILE *fp;
    char dbidx[MAXLEN];
    char sername[VNAMELEN];
    char line1[256], line2[72];
    char stobs[11], endobs[11];
    char *p, pdc;
    int offset = 0;
    int gotit = 0, err = DB_OK;
    int n;

    strcpy(dbidx, db_name);
    p = strstr(dbidx, ".bin");
    if (p != NULL) {
	strcpy(p, ".idx");
    } else {
	strcat(dbidx, ".idx");
    }

    fp = gretl_fopen(dbidx, "r");
    if (fp == NULL) {
	strcpy(gretl_errmsg, _("Couldn't open database index file"));
	return DB_NOT_FOUND;
    }

    while (fgets(line1, sizeof line1, fp) && !gotit) {

	if (*line1 == '#') continue;

	if (sscanf(line1, "%15s", sername) != 1) break;

	if (!strcmp(series, sername)) {
	    gotit = 1;
	    strcpy(sinfo->varname, sername);
	}

	fgets(line2, sizeof line2, fp);

	if (gotit) {
	    get_native_series_comment(sinfo, line1);
	    if (sscanf(line2, "%c %10s %*s %10s %*s %*s %d", 
		       &pdc, stobs, endobs, &(sinfo->nobs)) != 4) {
		strcpy(gretl_errmsg,
		       _("Failed to parse series information"));
		err = DB_PARSE_ERROR;
	    } else {
		get_native_series_pd(sinfo, pdc);
		get_native_series_obs(sinfo, stobs, endobs);
		sinfo->offset = offset;
	    }
	} else {
	    if (sscanf(line2, "%*c %*s %*s %*s %*s %*s %d", &n) != 1) {
		strcpy(gretl_errmsg,
		       _("Failed to parse series information"));
		err = DB_PARSE_ERROR;
	    } else {
		offset += n * sizeof(dbnumber);
	    }
	}
    }

    fclose(fp);

    if (!gotit) {
	sprintf(gretl_errmsg, _("Series not found, '%s'"), series);
	err = DB_NO_SUCH_SERIES;
    }

    return err;
}

static int 
get_pcgive_series_info (const char *series, SERIESINFO *sinfo)
{
    FILE *fp;
    char dbidx[MAXLEN];
    char line[1024];
    char *p;
    int y0, p0, y1, p1;
    int nf, gotit = 0;
    int err = 0;

    strcpy(dbidx, db_name);
    p = strstr(dbidx, ".bn7");
    if (p != NULL) {
	strcpy(p, ".in7");
    } else {
	strcat(dbidx, ".in7");
    }

    fp = gretl_fopen(dbidx, "r");
    if (fp == NULL) {
	strcpy(gretl_errmsg, _("Couldn't open database index file"));
	return DB_NOT_FOUND;
    }

    while (fgets(line, sizeof line, fp) && !gotit) {
	if (*line == '>') {
	    *sinfo->varname = 0;
	    *sinfo->descrip = 0;
	    nf = sscanf(line + 1, "%15s %d %d %d %d %d %d",
			sinfo->varname, &y0, &p0, &y1, &p1, 
			&sinfo->pd, &sinfo->offset);
	    if (!strcmp(sinfo->varname, series)) {
		gotit = 1;
	    }
	    if (nf == 7 && y0 > 0 && p0 > 0 && y1 > 0 && p1 > 0 &&
		sinfo->pd >= 1 && sinfo->offset > 0) {
		while (fgets(line, sizeof line, fp)) {
		    if (*line == ';') {
			int rem = MAXLABEL - strlen(sinfo->descrip) - 1;

			if (rem > 0) {
			    chopstr(line);
			    strncat(sinfo->descrip, line + 1, rem);
			}
		    } else {
			break;
		    }
		}
		/* transcribe info */
		if (sinfo->pd == 1) {
		    sprintf(sinfo->stobs, "%d", y0);
		    sprintf(sinfo->endobs, "%d", y1);
		} else if (sinfo->pd == 4) {
		    sprintf(sinfo->stobs, "%d:%d", y0, p0);
		    sprintf(sinfo->endobs, "%d:%d", y1, p1);
		} else if (sinfo->pd == 12) {
		    sprintf(sinfo->stobs, "%d:%02d", y0, p0);
		    sprintf(sinfo->endobs, "%d:%02d", y1, p1);
		} else {
		    err = E_DATA; /* FIXME? */
		}
	    } else {
		err = E_DATA;
	    }
	}
    }

    fclose(fp);

    if (!err && !gotit) {
	sprintf(gretl_errmsg, _("Series not found, '%s'"), series);
	err = DB_NO_SUCH_SERIES;
    }

    return err;
}

/* Figure the ending observation date of a series */

static int get_endobs (char *datestr, int startyr, int startfrac, 
		       int pd, int n)
{
    int endyr, endfrac;  

    endyr = startyr + n / pd;
    endfrac = startfrac - 1 + n % pd;

    if (endfrac >= pd) {
	endyr++;
	endfrac -= pd;
    }

    if (endfrac == 0) {
	endyr--;
	endfrac = pd;
    }   
 
    if (pd == 1) {
	sprintf(datestr, "%d", endyr);
    } else if (pd == 4) {
	sprintf(datestr, "%d.%d", endyr, endfrac);
    } else if (pd == 12) {
	sprintf(datestr, "%d.%02d", endyr, endfrac);
    }

    return 0;
}

static int dinfo_sanity_check (const DATEINFO *dinfo)
{
    if (dinfo->info < 0 || dinfo->info > 365 ||
	dinfo->year < 0 || dinfo->year > 3000 ||
	dinfo->month < 0 || dinfo->month > 12 ||
	dinfo->day < 0 || dinfo->day > 365) {
	strcpy(gretl_errmsg, _("This is not a valid RATS 4.0 database"));
	fprintf(stderr, "rats database: failed dinfo_sanity_check:\n"
		" info=%ld, year=%d, month=%d, day=%d\n",
		dinfo->info, (int) dinfo->year, (int) dinfo->month, 
		(int) dinfo->day);
	return 1;
    }

    return 0;
}

static int dinfo_to_sinfo (const DATEINFO *dinfo, SERIESINFO *sinfo,
			   const char *varname, const char *comment,
			   int n, int offset)
{
    int startfrac = 0;
    char pdstr[4] = {0}; 
    int err = 0;

    if (dinfo_sanity_check(dinfo)) {
	return 1;
    }

    sprintf(sinfo->stobs, "%d", dinfo->year); 

    if (dinfo->info == 4) {
	sprintf(pdstr, ".%d", dinfo->month);
	if (dinfo->month == 1) {
	    startfrac = 1;
	} else if (dinfo->month > 1 && dinfo->month <= 4) {
	    startfrac = 2;
	} else if (dinfo->month > 4 && dinfo->month <= 7) {
	    startfrac = 3;
	} else {
	    startfrac = 4;
	}
    } else if (dinfo->info == 12) {
	sprintf(pdstr, ".%02d", dinfo->month);
	startfrac = dinfo->month;
    } else if (dinfo->info == 1) {
	startfrac = 0;
    } else {
	fprintf(stderr, I_("frequency (%d) does not make seem to make sense"),
		(int) dinfo->info);
	fputc('\n', stderr);
	sprintf(gretl_errmsg, ("frequency (%d) does not make seem to make sense"), 
		(int) dinfo->info);
	err = 1;
    }   

    if (*pdstr) {
	strcat(sinfo->stobs, pdstr);
    }

    get_endobs(sinfo->endobs, dinfo->year, startfrac, dinfo->info, n);

    sinfo->pd = dinfo->info;
    sinfo->nobs = n;

    *sinfo->varname = 0;
    strncat(sinfo->varname, varname, VNAMELEN - 1);

    *sinfo->descrip = 0;
    strncat(sinfo->descrip, comment, MAXLABEL - 1);

    sinfo->offset = offset;

#if DB_DEBUG
    fprintf(stderr, "dinfo_to_sinfo: '%s': set sinfo->offset = %d\n", varname, 
	    (int) offset);
#endif

    return err;
}

static int in7_to_sinfo (const char *varname, const char *comment,
			 int y0, int p0, int y1, int p1, int pd,
			 int offset, SERIESINFO *sinfo)
{
    int err = 0;

    if (pd == 4) {
	sprintf(sinfo->stobs, "%d.%d", y0, p0);
	sprintf(sinfo->endobs, "%d.%d", y1, p1);
    } else if (pd == 12) {
	sprintf(sinfo->stobs, "%d.%02d", y0, p0);
	sprintf(sinfo->endobs, "%d.%02d", y1, p1);
    } else if (pd == 1) {
	sprintf(sinfo->stobs, "%d", y0);
	sprintf(sinfo->endobs, "%d", y1);
    } else {
	fprintf(stderr, I_("frequency (%d) does not make seem to make sense"),
		pd);
	fputc('\n', stderr);
	sprintf(gretl_errmsg, ("frequency (%d) does not make seem to make sense"), 
		pd);
	err = 1;
    }

    sinfo->nobs = (y1 - y0 + 1) * pd - (p0 - 1) - (pd - p1);
    if (sinfo->nobs <= 0) {
	err = 1;
    }


    if (!err) {
	strcpy(sinfo->varname, varname);
	if (comment != NULL && *comment != 0) {
	    strcpy(sinfo->descrip, comment);
	}
	sinfo->pd = pd;
	sinfo->offset = offset;
    } 

    return err;
}

static RECNUM read_rats_directory (FILE *fp, const char *series_name,
				   SERIESINFO *sinfo) 
{
    RATSDirect rdir;
    DATEINFO dinfo;
    RECNUM ret;
    int i, err = 0;

    memset(rdir.series_name, 0, NAMELENGTH);

    fread(&rdir.back_point, sizeof(RECNUM), 1, fp);
    fread(&rdir.forward_point, sizeof(RECNUM), 1, fp);
    fseek(fp, 4L, SEEK_CUR); /* skip two shorts */
    fread(&rdir.first_data, sizeof(RECNUM), 1, fp);
    fread(rdir.series_name, NAMELENGTH, 1, fp);  
    rdir.series_name[15] = '\0';

    chopstr(rdir.series_name);

#if DB_DEBUG
    fprintf(stderr, "read_rats_directory: name='%s'\n", rdir.series_name);
#endif

    if (series_name != NULL && strcmp(series_name, rdir.series_name)) {
	/* specific series not found yet: keep going */
	return rdir.forward_point;
    }

    /* Now the dateinfo: we can't read this in one go either :-( */

    /* skip long, short, long, short */
    fseek(fp, 12, SEEK_CUR);
    fread(&dinfo.info, sizeof(long), 1, fp);
    fread(&dinfo.digits, sizeof(short), 1, fp);
    fread(&dinfo.year, sizeof(short), 1, fp);
    fread(&dinfo.month, sizeof(short), 1, fp);
    fread(&dinfo.day, sizeof(short), 1, fp);

    fread(&rdir.datapoints, sizeof(long), 1, fp);
    fseek(fp, sizeof(short) * 4L, SEEK_CUR);  /* skip 4 shorts */

#if DB_DEBUG
    fprintf(stderr, "info=%d, digits=%d, year=%d, mon=%d, day=%d\n", 
	    (int) dinfo.info, (int) dinfo.digits, (int) dinfo.year, 
	    (int) dinfo.month, (int) dinfo.day);
    fprintf(stderr, "datapoints = %d\n", (int) rdir.datapoints);
#endif

    fread(&rdir.comment_lines, sizeof(short), 1, fp);
    fseek(fp, 1L, SEEK_CUR); /* skip one char */

    for (i=0; i<2; i++) {
	if (i < rdir.comment_lines) {
	    memset(rdir.comments[i], 0, 80);
	    fread(rdir.comments[i], 80, 1, fp);
	    rdir.comments[i][79] = '\0';
	    chopstr(rdir.comments[i]);
	} else {
	    rdir.comments[i][0] = 0;
	    fseek(fp, 80, SEEK_CUR);
	}
    }

#if DB_DEBUG
    fprintf(stderr, "comment_lines = %d\n", (int) rdir.comment_lines);
    fprintf(stderr, "comment[0] = '%s'\n", rdir.comments[0]);
    fprintf(stderr, "comment[1] = '%s'\n", rdir.comments[1]);
#endif

    err = dinfo_to_sinfo(&dinfo, sinfo, rdir.series_name, rdir.comments[0],
			 rdir.datapoints, rdir.first_data);

    ret = (err)? RATS_PARSE_ERROR : rdir.forward_point;

#if DB_DEBUG
    fprintf(stderr, "read_rats_directory: err = %d, forward_point=%d, first_data=%d\n",
	    err, (int) rdir.forward_point, (int) rdir.first_data);
    fprintf(stderr, "returning %d\n", (int) ret);
#endif

    return ret;
}

static void series_info_init (SERIESINFO *sinfo)
{
    *sinfo->varname = 0;
    *sinfo->descrip = 0;
    sinfo->nobs = 0;
    *sinfo->stobs = 0;
    *sinfo->endobs = 0;
    sinfo->pd = 1;
    sinfo->offset = 0;
    sinfo->err = 0;
    sinfo->undated = 0;
}

#define DB_INIT_ROWS 32

static db_table *db_table_new (int nr)
{
    db_table *tbl;
    int i;

    if (nr == 0) {
	nr = DB_INIT_ROWS;
    }

    tbl = malloc(sizeof *tbl);
    if (tbl == NULL) return NULL;

    tbl->sinfo = malloc(nr * sizeof *tbl->sinfo);

    if (tbl->sinfo == NULL) {
	free(tbl);
	return NULL;
    }

    for (i=0; i<nr; i++) {
	series_info_init(&tbl->sinfo[i]);
    }

    tbl->nvars = 0;
    tbl->nalloc = nr;

    return tbl;
}

static int db_table_expand (db_table *tbl)
{
    SERIESINFO *sinfo;
    int i, newsz;

    newsz = (tbl->nvars / DB_INIT_ROWS) + 1;
    newsz *= DB_INIT_ROWS;

    sinfo = realloc(tbl->sinfo, newsz * sizeof *sinfo);
    if (sinfo == NULL) {
	free(tbl->sinfo);
	tbl->sinfo = NULL;
	return 1;
    }

    tbl->sinfo = sinfo;

    for (i=tbl->nalloc; i<newsz; i++) {
	series_info_init(&tbl->sinfo[i]);
    }

    tbl->nalloc = newsz;

    return 0;
}

static void db_table_free (db_table *tbl)
{
    free(tbl->sinfo);
    free(tbl);
}

static int read_in7_series_info (FILE *fp, db_table *tbl)
{
    char line[1024];
    char sname[VNAMELEN];
    char desc[MAXLABEL];
    int y0, p0, y1, p1;
    int pd, offset, pos;
    int i, nf;
    int err = 0;

    i = 0;
    while (fgets(line, sizeof line, fp) && !err) {
	if (*line == '>') {
	    nf = sscanf(line + 1, "%15s %d %d %d %d %d %d",
			sname, &y0, &p0, &y1, &p1, &pd, &offset);
	    if (nf == 7 && y0 > 0 && p0 > 0 && y1 > 0 && p1 > 0 &&
		pd >= 1 && offset > 0) {
		*desc = 0;
		pos = ftell(fp);
		while (fgets(line, sizeof line, fp)) {
		    if (*line == ';') {
			/* following series description */
			int rem = MAXLABEL - strlen(desc) - 1;

			if (rem > 0) {
			    chopstr(line);
			    strncat(desc, line + 1, rem);
			}
			pos = ftell(fp);
		    } else {
			/* not a description: throw the line back */
			fseek(fp, pos, SEEK_SET);
			break;
		    }
		}
		/* record info */
		err = in7_to_sinfo(sname, desc, y0, p0, y1, p1,
				   pd, offset, &tbl->sinfo[i++]);
		if (!err) {
		    tbl->nvars += 1;
		}
	    }
	}
    }

    return err;
}

static int count_in7_series (FILE *fp)
{
    char line[1024];
    char sname[VNAMELEN];
    int y0, p0, y1, p1;
    int pd, offset;
    int nf, nseries = 0;

    while (fgets(line, sizeof line, fp)) {
	if (*line == '>') {
	    nf = sscanf(line + 1, "%15s %d %d %d %d %d %d",
			sname, &y0, &p0, &y1, &p1, &pd, &offset);
	    if (nf < 7 || y0 < 0 || p0 < 0 || y1 < 0 || p1 < 0 ||
		pd < 1 || offset < 0) {
		fprintf(stderr, "Error reading series info\n");
	    } else {
		nseries++;
	    }
	}
    }

    return nseries;
}

/**
 * read_pcgive_db:
 * @fp: pre-opened stream (caller to close it)
 *
 * Read the series info from a PcGive database, .in7 file
 * 
 * Returns: pointer to a #db_table containing the series info,
 * or NULL in case of failure.
 */

db_table *read_pcgive_db (FILE *fp) 
{
    db_table *tbl;
    int ns, err = 0;

    *gretl_errmsg = 0;

    ns = count_in7_series(fp);
    if (ns == 0) {
	strcpy(gretl_errmsg, _("No valid series found"));
	return NULL;
    }

#if DB_DEBUG
    fprintf(stderr, "in7: found %d series\n", ns);
#endif
    
    /* allocate table for series rows */
    tbl = db_table_new(ns);
    if (tbl == NULL) {
	strcpy(gretl_errmsg, _("Out of memory!"));
	return NULL;
    }

    rewind(fp);
    
    /* Go find the series info */
    err = read_in7_series_info(fp, tbl);

    if (err) {
	db_table_free(tbl);
	return NULL;
    }

    return tbl;
}

/**
 * read_rats_db:
 * @fp: pre-opened stream (caller to close it)
 *
 * Read the series info from a RATS 4.0 database: read the base 
 * block at offset 0 in the data file, and recurse through the 
 * directory entries
 * 
 * Returns: pointer to a #db_table containing the series info,
 * or NULL in case of failure.
 */

db_table *read_rats_db (FILE *fp) 
{
    db_table *tbl;
    long forward;
    int i, err = 0;

    *gretl_errmsg = 0;
    
    /* get into position */
    fseek(fp, 30L, SEEK_SET); /* skip unneeded fields */
    fread(&forward, sizeof forward, 1, fp);
    fseek(fp, 4L, SEEK_CUR);

    /* basic check */
    if (forward <= 0) { 
	strcpy(gretl_errmsg, _("This is not a valid RATS 4.0 database"));
	fprintf(stderr, "rats database: got forward = %ld\n", forward);
	return NULL;
    }

    /* allocate table for series rows */
    tbl = db_table_new(0);
    if (tbl == NULL) {
	strcpy(gretl_errmsg, _("Out of memory!"));
	return NULL;
    }
    
    /* Go find the series */
    i = 0;
    while (forward && !err) {
	tbl->nvars += 1;
#if DB_DEBUG
	fprintf(stderr, "read_rats_db: forward = %d, nvars = %d\n",
		(int) forward, tbl->nvars);
#endif    
	if (tbl->nvars > 0 && tbl->nvars % DB_INIT_ROWS == 0) {
	    err = db_table_expand(tbl);
	    if (err) {
		strcpy(gretl_errmsg, _("Out of memory!"));
	    }
	}
	if (!err) {
	    err = fseek(fp, (forward - 1) * 256L, SEEK_SET);
	    if (!err) {
		forward = read_rats_directory(fp, NULL, &tbl->sinfo[i++]);
		if (forward == RATS_PARSE_ERROR) {
		    err = 1;
		}
	    }
	}
#if DB_DEBUG
	fprintf(stderr, "bottom of loop, err = %d\n", err);
#endif    
    }

#if DB_DEBUG
    fprintf(stderr, "read_rats_db: err = %d, tbl = %p\n",
	    err, (void *) tbl);
#endif    

    if (err) {
	db_table_free(tbl);
	return NULL;
    }

    return tbl;
}

/* retrieve the actual data values from the data blocks */

static int get_rats_series (int offset, SERIESINFO *sinfo, FILE *fp, 
			    double **Z)
{
    RATSData rdata;
    int miss = 0, i, t = 0;
    double val;

    fprintf(stderr, "get_rats_series: starting from offset %d\n", offset);
    
    rdata.forward_point = offset;

    while (rdata.forward_point) {
	fseek(fp, (rdata.forward_point - 1) * 256L, SEEK_SET);
	/* the RATSData struct is actually 256 bytes.  Yay! */
	fread(&rdata, sizeof rdata, 1, fp);
	for (i=0; i<31 && t<sinfo->nobs; i++) {
	    val = rdata.data[i];
	    if (isnan(val)) {
		val = NADBL;
		miss = 1;
	    }
	    Z[1][t++] = val;
	}
    }

    return miss;
}

/**
 * get_rats_db_data:
 * @fname: name of RATS 4.0 database to read from
 * @sinfo: holds info on the given series (input)
 * @Z: data matrix
 *
 * Read the actual data values for a series from a RATS database.
 * 
 * Returns: DB_OK on successful completion, DB_NOT_FOUND if
 * the data could not be read, and DB_MISSING_DATA if the
 * data were found but there were some missing values.
 */

int get_rats_db_data (const char *fname, SERIESINFO *sinfo, double **Z)
{
    FILE *fp;
    int ret = DB_OK;

    fp = gretl_fopen(fname, "rb");
    if (fp == NULL) {
	return DB_NOT_FOUND;
    }
    
#if DB_DEBUG
    fprintf(stderr, "get_rats_db_data: offset = %d\n", sinfo->offset);
#endif
    
    if (get_rats_series(sinfo->offset, sinfo, fp, Z)) {
	ret = DB_MISSING_DATA; /* FIXME? */
    }

    fclose(fp);

    return ret;
}

static int get_rats_series_info (const char *series_name, SERIESINFO *sinfo)
{
    FILE *fp;
    long forward;
    int err = DB_OK;

    *gretl_errmsg = 0;

    fp = gretl_fopen(db_name, "rb");
    if (fp == NULL) {
	return DB_NOT_FOUND;
    }

#if DB_DEBUG
    fprintf(stderr, "Opened %s\n", db_name);
#endif

    /* get into position */
    fseek(fp, 30L, SEEK_SET); 
    fread(&forward, sizeof forward, 1, fp);
    fseek(fp, 4L, SEEK_CUR);

    /* basic check */
    if (forward <= 0) {
	strcpy(gretl_errmsg, _("This is not a valid RATS 4.0 database"));
	fprintf(stderr, "rats database: got forward = %ld\n", forward);
	return DB_PARSE_ERROR;
    }

    sinfo->offset = 0;

    /* Go find the series */
    while (forward) {
	fseek(fp, (forward - 1) * 256L, SEEK_SET);
	forward = read_rats_directory(fp, series_name, sinfo);
	if (forward == RATS_PARSE_ERROR) {
	    sinfo->offset = -1;
	}
	if (sinfo->offset != 0) {
	    break;
	}
    }    

    fclose(fp);

    if (sinfo->offset <= 0) {
	err = DB_NOT_FOUND;
    }

#if DB_DEBUG
    fprintf(stderr, "get_rats_series_info: offset = %d\n", sinfo->offset);
    fprintf(stderr, " pd = %d, nobs = %d\n", sinfo->pd, sinfo->nobs);
#endif

    return err;
}

/* For importation of database series */

static double *get_compacted_xt (const double *src, int n, 
				 CompactMethod method, int compfac,
				 int skip)
{
    int p, t;
    double *x;

    x = malloc(n * sizeof *x);
    if (x == NULL) return NULL;

    for (t=0; t<n; t++) {
	p = (t + 1) * compfac;
	x[t] = 0.0;
	if (method == COMPACT_AVG || method == COMPACT_SUM) { 
	    int i, st;

	    for (i=1; i<=compfac; i++) {
		st = p - i + skip;
		if (na(src[st])) {
		    x[t] = NADBL;
		    break;
		} else {
		    x[t] += src[st];
		}
	    }
	    if (method == COMPACT_AVG) {
		x[t] /= (double) compfac;
	    }
	} else if (method == COMPACT_EOP) {
	    x[t] = src[p - 1 + skip];
	} else if (method == COMPACT_SOP) {
	    x[t] = src[p - compfac + skip];
	}
    }

    return x;
}

/* Compact a single series from a database, for importation 
   into a working dataset of lower frequency.  At present
   this is permitted only for the cases:

     quarterly -> annual
     monthly   -> quarterly
     monthly   -> annual
*/

double *compact_db_series (const double *src, SERIESINFO *sinfo,
			   int target_pd, CompactMethod method)
{
    int p0, y0, endskip, goodobs;
    int skip = 0, compfac = sinfo->pd / target_pd;
    double *x;

    if (target_pd == 1) {
	/* figure the annual dates */
	y0 = atoi(sinfo->stobs);
	p0 = atoi(sinfo->stobs + 5);
	if (p0 != 1) {
	    ++y0;
	    skip = compfac - (p0 + 1);
	}
	sprintf(sinfo->stobs, "%d", y0);
    } else if (target_pd == 4) {
	/* figure the quarterly dates */
	float q;
	int q0;

	y0 = atoi(sinfo->stobs);
	p0 = atoi(sinfo->stobs + 5);
	q = 1.0 + p0 / 3.;
	q0 = q + .5;
	skip = ((q0 - 1) * 3) + 1 - p0;
	if (q0 == 5) {
	    y0++;
	    q0 = 1;
	}
	sprintf(sinfo->stobs, "%d.%d", y0, q0);
    } else {
	return NULL;
    }

    endskip = (sinfo->nobs - skip) % compfac;
    goodobs = (sinfo->nobs - skip - endskip) / compfac;
    sinfo->nobs = goodobs;

#if DB_DEBUG
    fprintf(stderr, "startskip = %d\n", skip);
    fprintf(stderr, "endskip = %d\n", endskip);
    fprintf(stderr, "goodobs = %d\n", goodobs);
    fprintf(stderr, "compfac = %d\n", compfac);
    fprintf(stderr, "starting date = %s\n", sinfo->stobs);
#endif

    x = get_compacted_xt(src, goodobs, method, compfac, skip);

    sinfo->pd = target_pd;

    return x;
}

/* Expand a single series from a database, for importation 
   into a working dataset of higher frequency.  At present
   this is permitted only for the cases:

     annual    -> quarterly
     annual    -> monthly
     quarterly -> monthly
*/

double *expand_db_series (const double *src, SERIESINFO *sinfo,
			  int target_pd)
{
    char stobs[12] = {0};
    int oldn = sinfo->nobs;
    int mult, newn;
    double *x = NULL;
    int j, s, t;

    mult = target_pd / sinfo->pd;
    newn = mult * sinfo->nobs;

    x = malloc(newn * sizeof *x);
    if (x == NULL) {
	return NULL;
    }    

    s = 0;
    for (t=0; t<oldn; t++) {
	for (j=0; j<mult; j++) {
	    x[s++] = src[t];
	}
    }

    if (sinfo->pd == 1) {
	strcpy(stobs, sinfo->stobs);
	if (target_pd == 4) {
	    strcat(stobs, ":1");
	} else {
	    strcat(stobs, ":01");
	}
    } else {
	int yr, qtr, mo;

	sscanf(sinfo->stobs, "%d.%d", &yr, &qtr);
	mo = (qtr - 1) * 3 + 1;
	sprintf(stobs, "%d:%02d", yr, mo);
    }

    strcpy(sinfo->stobs, stobs);
    sinfo->pd = target_pd;
    sinfo->nobs = newn;

    return x;
}

int set_db_name (const char *fname, int filetype, const PATHS *ppaths, 
		 PRN *prn)
{
    FILE *fp;
    int err = 0;

    *db_name = 0;
    strncat(db_name, fname, MAXLEN - 1);

    fp = gretl_fopen(db_name, "rb");

    if (fp == NULL) {
	/* try looking a bit more */
	if (filetype == GRETL_NATIVE_DB && 
	    strstr(db_name, ppaths->binbase) == NULL) {
	    build_path(db_name, ppaths->binbase, fname, NULL);
	} else if (filetype == GRETL_RATS_DB && 
		   strstr(db_name, ppaths->ratsbase) == NULL) {
	    build_path(db_name, ppaths->ratsbase, fname, NULL);
	}
	fp = gretl_fopen(db_name, "rb");
    }
	    
    if (fp == NULL) {
	*db_name = 0;
	pprintf(prn, _("Couldn't open %s\n"), fname);
	err = 1;
    } else {
	fclose(fp);
	db_type = filetype;
	pprintf(prn, "%s\n", db_name);
    }

    return err;
}

int db_set_sample (const char *line, DATAINFO *pdinfo)
{
    char cmd[5], start[OBSLEN], stop[OBSLEN];
    int t1 = 0, t2 = 0;

    if (sscanf(line, "%4s %8s %8s", cmd, start, stop) != 3) {
	sprintf(gretl_errmsg, _("error reading smpl line"));
	return 1;
    }

    if (strcmp(start, ";")) {
	t1 = dateton(start, pdinfo);
	if (t1 < 0 || *gretl_errmsg != '\0') {
	    return 1;
	}
    }

    t2 = dateton(stop, pdinfo);

    if (*gretl_errmsg != '\0') {
	return 1;
    }

    if (t1 > t2) {
	sprintf(gretl_errmsg, _("Invalid null sample"));
	return 1;
    }

    pdinfo->t1 = t1;
    pdinfo->t2 = t2;
    pdinfo->n = t2 - t1 + 1;
    strcpy(pdinfo->endobs, stop);

#if DB_DEBUG
    fprintf(stderr, "db_set_sample: t1=%d, t2=%d, stobs='%s', endobs='%s' "
	    "sd0 = %g, n = %d\n", 
	    pdinfo->t1, pdinfo->t2, 
	    pdinfo->stobs, pdinfo->endobs,
	    pdinfo->sd0, pdinfo->n);
#endif

    return 0;
}

static const char *
get_word_and_advance (const char *s, char *word, size_t maxlen)
{
    size_t i = 0;

    while (isspace(*s)) s++;

    *word = 0;
    while (*s && !isspace(*s)) {
	if (i < maxlen) word[i++] = *s;
	s++;
    }

    word[i] = 0;

    if (*word) return s;
    else return NULL;
}

static const char *
get_compact_method_and_advance (const char *s, CompactMethod *method)
{
    const char *p;

    *method = COMPACT_NONE;

    if ((p = strstr(s, "(compact"))) {
	char comp[8];
	int i;

	p += 8;
	i = 0;
	while (*p && *p != ')' && i < 7) {
	    if (!isspace(*p) && *p != '=') {
		comp[i++] = *p;
	    }
	    p++;
	}
	comp[i] = '\0';

	if (!strcmp(comp, "average")) {
	    *method = COMPACT_AVG;
	} else if (!strcmp(comp, "sum")) {
	    *method = COMPACT_SUM;
	} else if (!strcmp(comp, "first")) {
	    *method = COMPACT_SOP;
	} else if (!strcmp(comp, "last")) {
	    *method = COMPACT_EOP;
	}

	p = strchr(p, ')');
	if (p != NULL) p++;
    } else {
	/* no compaction method given */
	if ((p = strstr(s, "data "))) {
	    p += 5;
	}
    }

    return p;
}

static double **new_dbZ (int n)
{
    double **Z;

    Z = malloc(2 * sizeof *Z);
    if (Z == NULL) return NULL;

    Z[0] = NULL;
    Z[1] = malloc(n * sizeof **Z);

    if (Z[1] == NULL) {
	free(Z);
	return NULL;
    }

    return Z;
}

/* main function for getting a series out of a database, using the
   command-line client or in script or console mode
*/

int db_get_series (const char *line, double ***pZ, DATAINFO *pdinfo, 
		   PRN *prn)
{
    char series[16];
    CompactMethod method;
    SERIESINFO sinfo;
    double **dbZ;
    int err = 0;

#if DB_DEBUG
    fprintf(stderr, "db_get_series: line='%s', pZ=%p, pdinfo=%p\n", 
	    line, (void *) pZ, (void *) pdinfo);
    fprintf(stderr, "db_name = '%s'\n", db_name);
#endif

    if (*db_name == '\0') {
	strcpy(gretl_errmsg, _("No database has been opened"));
	return 1;
    }   

    line = get_compact_method_and_advance(line, &method);

    /* now loop over variable names given on the line */

    while ((line = get_word_and_advance(line, series, 8))) {
	int v, this_var_method = method; 

	/* see if the series is already in the dataset */
	v = varindex(pdinfo, series);
#if DB_DEBUG
	fprintf(stderr, "db_get_series: pdinfo->v = %d, v = %d\n", pdinfo->v, v);
#endif
	if (v < pdinfo->v && method == COMPACT_NONE) {
	    this_var_method = COMPACT_METHOD(pdinfo, v);
	}

#if DB_DEBUG
	fprintf(stderr, "this_var_method = %d\n", this_var_method);
#endif

	/* find the series information in the database */
	if (db_type == GRETL_RATS_DB) {
	    err = get_rats_series_info(series, &sinfo);
	} else if (db_type == GRETL_PCGIVE_DB) {
	    err = get_pcgive_series_info(series, &sinfo);
	} else {
	    err = get_native_series_info(series, &sinfo);
	} 

	if (err) {
	    return 1;
	}

	/* temporary dataset */
	dbZ = new_dbZ(sinfo.nobs);
	if (dbZ == NULL) {
	    strcpy(gretl_errmsg, _("Out of memory!"));
	    return 1;
	}

	if (db_type == GRETL_RATS_DB) {
	    err = get_rats_db_data(db_name, &sinfo, dbZ);
	} else if (db_type == GRETL_PCGIVE_DB) {
	    err = get_pcgive_db_data(db_name, &sinfo, dbZ);
	} else {
	    err = get_native_db_data(db_name, &sinfo, dbZ);
	} 

	if (!err) {
	    err = cli_add_db_data(dbZ, &sinfo, pZ, pdinfo, this_var_method, v);
	}

	/* free up temp stuff */
	free(dbZ[1]);
	free(dbZ);

	if (!err) {
	    pprintf(prn, _("Series imported OK"));
	    pputc(prn, '\n');
	}
    }
    
    return err;
}

void get_db_padding (SERIESINFO *sinfo, DATAINFO *pdinfo, 
		     int *pad1, int *pad2)
{
    *pad1 = dateton(sinfo->stobs, pdinfo); 
    fprintf(stderr, "pad1 = dateton(%s) = %d\n", sinfo->stobs, *pad1);

    *pad2 = pdinfo->n - sinfo->nobs - *pad1;
    fprintf(stderr, "pad2 = pdinfo->n - sinfo->nobs - pad1 = %d - %d - %d = %d\n", 
	    pdinfo->n, sinfo->nobs, *pad1, *pad2);
} 

int check_db_import (SERIESINFO *sinfo, DATAINFO *pdinfo)
{
    double sd0, sdn_new, sdn_old;
    int err = 0;

    if (sinfo->pd < pdinfo->pd) {
	if (sinfo->pd != 1 && sinfo->pd != 4 && 
	    pdinfo->pd != 4 && pdinfo->pd != 12) {
	    strcpy(gretl_errmsg, _("Sorry, can't handle this conversion yet!"));
	    err = 1;
	} 
    }

    if (!err) {
	sd0 = get_date_x(sinfo->pd, sinfo->stobs);
	sdn_new = get_date_x(sinfo->pd, sinfo->endobs);
	sdn_old = get_date_x(pdinfo->pd, pdinfo->endobs);
	if (sd0 > sdn_old || sdn_new < pdinfo->sd0) {
	    strcpy(gretl_errmsg, _("Observation range does not overlap\nwith the working "
				   "data set"));
	    err = 1;
	}
    }
    
    return err;
}

static int cli_add_db_data (double **dbZ, SERIESINFO *sinfo, 
			    double ***pZ, DATAINFO *pdinfo,
			    CompactMethod method, int dbv)
{
    double *xvec = NULL;
    int n, t, start, stop, pad1 = 0, pad2 = 0;
    int new = (dbv == pdinfo->v);

    if (check_db_import(sinfo, pdinfo)) {
	return 1;
    }

    /* the data matrix may still be empty */
    if (pdinfo->v == 0) {
	pdinfo->v = 1;
	dbv = 1;
	if (start_new_Z(pZ, pdinfo, 0)) {
	    strcpy(gretl_errmsg, _("Out of memory adding series"));
	    return 1;
	}
    }

#if DB_DEBUG
    fprintf(stderr, "Z=%p\n", (void *) *pZ);
    fprintf(stderr, "pdinfo->n = %d, pdinfo->v = %d, pdinfo->varname = %p\n",
	    pdinfo->n, pdinfo->v, (void *) pdinfo->varname);
#endif

    if (new && dataset_add_series(1, pZ, pdinfo)) {
	strcpy(gretl_errmsg, _("Out of memory adding series"));
	return 1;
    }

    n = pdinfo->n;

    /* is the frequency of the new var higher? */
    if (sinfo->pd > pdinfo->pd) {
	if (pdinfo->pd != 1 && pdinfo->pd != 4 &&
	    sinfo->pd != 12) {
	    strcpy(gretl_errmsg, _("Sorry, can't handle this conversion yet!"));
	    if (new) {
		dataset_drop_last_variables(1, pZ, pdinfo);
	    }
	    return 1;
	}
	if (method == COMPACT_NONE) {
	    sprintf(gretl_errmsg, _("%s: you must specify a compaction method"), 
		    sinfo->varname);
	    if (new) {
		dataset_drop_last_variables(1, pZ, pdinfo);
	    }
	    return 1;
	}
	xvec = compact_db_series(dbZ[1], sinfo, pdinfo->pd, method);
    } else {  
	/* series does not need compacting */
	xvec = malloc(sinfo->nobs * sizeof *xvec);
	if (xvec != NULL) {
	    for (t=0; t<sinfo->nobs; t++) {
		xvec[t] = dbZ[1][t];
	    }
	} 
    }

    if (xvec == NULL) {
	strcpy(gretl_errmsg, _("Out of memory adding series"));
	if (new) {
	    dataset_drop_last_variables(1, pZ, pdinfo);
	}
	return 1;
    }

    /* common stuff for adding a var */
    strcpy(pdinfo->varname[dbv], sinfo->varname);
    strcpy(VARLABEL(pdinfo, dbv), sinfo->descrip);
    COMPACT_METHOD(pdinfo, dbv) = method;
    get_db_padding(sinfo, pdinfo, &pad1, &pad2);

    if (pad1 > 0) {
#if DB_DEBUG
	fprintf(stderr, "Padding at start, %d obs\n", pad1);
#endif
	for (t=0; t<pad1; t++) {
	    (*pZ)[dbv][t] = NADBL;
	}
	start = pad1;
    } else start = 0;

    if (pad2 > 0) {
#if DB_DEBUG
	fprintf(stderr, "Padding at end, %d obs\n", pad2);
#endif
	for (t=n-1; t>=n-1-pad2; t--) {
	    (*pZ)[dbv][t] = NADBL;
	}
	stop = n - pad2;
    } else stop = n;

    /* fill in actual data values */
#if DB_DEBUG
    fprintf(stderr, "Filling in values from %d to %d\n", start, stop - 1);
#endif
    for (t=start; t<stop; t++) {
	(*pZ)[dbv][t] = xvec[t-pad1];
    }

    free(xvec);

    return 0;
}

/* compact an individual series, in the context of converting an
   entire working dataset to a lower frequency: used in all cases
   except conversion from daily to monthly
*/

static double *compact_series (const double *src, int i, int n, int oldn,
			       int startskip, int min_startskip, int compfac,
			       CompactMethod method)
{
    int t, idx;
    int lead = startskip - min_startskip;
    int to_weekly = (compfac >= 5 && compfac <= 7);
    double *x;

#if DB_DEBUG
    printf("compact_series: startskip=%d, min_startskip=%d, compfac=%d "
	   "lead=%d\n", startskip, min_startskip, compfac, lead);
#endif

    x = malloc(n * sizeof *x);
    if (x == NULL) return NULL;

    for (t=0; t<n; t++) {
	x[t] = (i == 0)? 1.0 : NADBL;
    }

    if (i == 0) {
	return x;
    }

    idx = startskip;

    for (t=lead; t<n && idx<oldn; t++) {
	if (method == COMPACT_SOP) {
	    if (to_weekly && na(src[idx]) && idx < oldn - 1) {
		/* allow one day's slack */
		x[t] = src[idx + 1];
	    } else {
		x[t] = src[idx];
	    }
	} else if (method == COMPACT_EOP) {
	    if (to_weekly && na(src[idx]) && idx > startskip) {
		/* one day's slack */
		x[t] = src[idx - 1];
	    } else {
		x[t] = src[idx];
	    }
	} else if (method == COMPACT_SUM || method == COMPACT_AVG) {
	    int j, st, den = compfac;

	    if (idx + compfac - 1 > oldn - 1) break;

	    x[t] = 0.0;
	    for (j=0; j<compfac; j++) {
		st = idx + j;
		if (na(src[st])) {
		    if (to_weekly) {
			den--;
		    } else {
			x[t] = NADBL;
			break;
		    }
		} else {
		    x[t] += src[st];
		}
	    }
	    if (method == COMPACT_AVG && !na(x[t])) {
		if (den > 0) {
		    x[t] /= den;
		} else {
		    x[t] = NADBL;
		}
	    }
	}
	idx += compfac;
    }

    return x;
}

/* specific apparatus for converting daily time series to monthly */

static double *extend_series (const double *z, int n)
{
    double *x = malloc(n * sizeof *x);

    if (x != NULL) {
	int t;

	x[0] = NADBL;
	for (t=1; t<n; t++) {
	    x[t] = z[t-1];
	}
    }

    return x;
}

static double *
daily_series_to_monthly (const double *src, DATAINFO *pdinfo, int i, 
			 int nm, int yr, int mon, int offset, 
			 int any_eop, CompactMethod method)
{
    double *x;
    const double *z;
    double *tmp = NULL;
    int t, mdbak;
    int sopt, eopt;

    x = malloc(nm * sizeof *x);
    if (x == NULL) return NULL;

    if (i == 0) {
	for (t=0; t<nm; t++) {
	    x[t] = 1.0;
	}
	return x;
    }

    if (offset < 0) {
	tmp = extend_series(src, pdinfo->n + 1);
	if (tmp == NULL) {
	    free(x);
	    return NULL;
	}
	/* this permits use of a negative offset */
	z = tmp + 1;
    } else {
	z = src;
    }

    /* note: we can't necessarily assume that the first obs
       is the first day of a month  
    */

    /* the "one day's slack" business with start-of-period
       and end-of-period compaction is designed to allow for
       the possibility that the first (or last) day of the
       month may not have been a trading day
    */

    mdbak = 0;
    sopt = offset;
    eopt = offset - 1;

#if 0
    printf("offset=%d, sopt=%d\n", offset, sopt);
#endif

    for (t=0; t<nm; t++) {
	int mdays = get_days_in_month(mon, yr, pdinfo->pd);

	sopt += mdbak;
	eopt += mdays;

	if (t == 0 && offset > 0 && any_eop &&
	    method != COMPACT_EOP) {
	    x[t] = NADBL;
	} else if (method == COMPACT_SOP) {
	    /* allow one days's slack */
	    if (na(z[sopt]) && sopt < pdinfo->n - 1) {
		x[t] = z[sopt + 1];
	    } else {
		x[t] = z[sopt];
	    }
	} else if (method == COMPACT_EOP) {
	    if (eopt >= pdinfo->n) {
		x[t] = NADBL;
	    } else {
		/* allow one days's slack */
		if (na(z[eopt]) && eopt > 0) {
		    x[t] = z[eopt - 1];
		} else {
		    x[t] = z[eopt];
		}
	    }
	} else if (method == COMPACT_SUM ||
		   method == COMPACT_AVG) {
	    int j, dayt, den = mdays;

	    x[t] = 0.0;
	    for (j=0; j<mdays; j++) {
		dayt = sopt + j;
		if (dayt >= pdinfo->n) {
		    x[t] = NADBL;
		    break;
		} else if (na(z[dayt]) && method == COMPACT_AVG) {
		    den--;
		} else if (!na(z[dayt])) {
		    x[t] += z[dayt];
		}
	    }
	    if (method == COMPACT_AVG && !na(x[t])) {
		if (den > 0) {
		    x[t] /= (double) den;
		} else {
		    x[t] = NADBL;
		}
	    }
	}

	mdbak = mdays;

	if (mon == 12) {
	    mon = 1;
	    yr++;
	} else {
	    mon++;
	}
    }

    if (tmp != NULL) {
	free(tmp);
    }

    return x;
}

static void 
get_startskip_etc (int compfac, int startmin, int endmin, 
		   int oldn, CompactMethod method, 
		   int *startskip, int *newn) 
{
    int ss = compfac - (startmin % compfac) + 1;
    int es, n;

    ss = ss % compfac;

    if (method == COMPACT_EOP) {
	if (ss > 0) {
	    ss--;
	} else {
	    /* move to end of initial period */
	    ss = compfac - 1;
	}
    }

    es = endmin % compfac;
    if (method == COMPACT_SOP && es > 1) {
	es--;
    }

    n = (oldn - ss - es) / compfac;
    if (ss && method == COMPACT_EOP) n++;
    if (es && method == COMPACT_SOP) n++;

    *startskip = ss;
    *newn = n;
}

/* specific to compaction of daily time series */

static void 
get_daily_compact_params (CompactMethod default_method, 
			  int *any_eop, int *any_sop,
			  int *all_same,
			  const DATAINFO *pdinfo)
{
    int i, n_not_eop = 0, n_not_sop = 0;

    *all_same = 1;
    *any_eop = (default_method == COMPACT_EOP)? 1 : 0;
    *any_sop = (default_method == COMPACT_SOP)? 1 : 0;

    for (i=1; i<pdinfo->v; i++) {
	CompactMethod method = COMPACT_METHOD(pdinfo, i);

	if (method != default_method && method != COMPACT_NONE) {
	    *all_same = 0;
	    if (method == COMPACT_EOP) {
		*any_eop = 1;
	    } else {
		n_not_eop++;
	    }
	    if (method == COMPACT_SOP) {
		*any_sop = 1;
	    } else {
		n_not_sop++;
	    }
	}
    }

    if (n_not_eop == pdinfo->v - 1) {
	*any_eop = 0;
    }

    if (n_not_sop == pdinfo->v - 1) {
	*any_sop = 0;
    }
}

/* specific to non-daily time series (monthly or quarterly) */

static void 
get_global_compact_params (int compfac, int startmin, int endmin,
			   CompactMethod default_method, 
			   int *min_startskip, int *max_n,
			   int *any_eop, int *all_same,
			   DATAINFO *pdinfo)
{
    CompactMethod method;
    int i, startskip, n;
    int n_not_eop = 0;

    for (i=0; i<pdinfo->v; i++) {
	if (i == 0) {
	    get_startskip_etc(compfac, startmin, endmin, pdinfo->n, 
			      default_method, &startskip, &n);
	    if (default_method == COMPACT_EOP) {
		*any_eop = 1;
	    }
	} else {
	    method = COMPACT_METHOD(pdinfo, i);
	    if (method != default_method && method != COMPACT_NONE) {
		get_startskip_etc(compfac, startmin, endmin, pdinfo->n, 
				  method, &startskip, &n);
		*all_same = 0;
		if (method == COMPACT_EOP) {
		    *any_eop = 1;
		} else {
		    n_not_eop++;
		}
	    }
	}
	if (startskip < *min_startskip) {
	    *min_startskip = startskip;
	}
	if (n > *max_n) {
	    *max_n = n;
	}
    }

    if (n_not_eop == pdinfo->v - 1) {
	*any_eop = 0;
    }
}

static int get_obs_maj_min (const char *obs, int *maj, int *min)
{
    int np = sscanf(obs, "%d:%d", maj, min);

    if (np < 2) {
	np = sscanf(obs, "%d.%d", maj, min);
    }

    return (np == 2);
}

/* for daily data, figure the number of observations to
   be skipped at the start of each series 
*/

static int get_daily_offset (const DATAINFO *pdinfo,
			     int y, int m, int d, 
			     int skip)
{
    int ret = 0;

    if (skip) {
	/* moving to start of next month: offset = no. of
	   observations in the first month */
	ret = days_in_month_after(y, m, d, pdinfo->pd) + 1;
    } else {
	/* offset = no. of obs missing at start of first month */
	ret = days_in_month_before(y, m, d, pdinfo->pd);
    }

    return ret;
}

/* for daily data, figure the number of valid monthly 
   observations that can be constructed by compaction
*/

static int get_n_ok_months (const DATAINFO *pdinfo, 
			    CompactMethod default_method,
			    int *startyr, int *startmon,
			    int *endyr, int *endmon,
			    int *offset, int *p_any_eop)
{
    int y1, m1, d1, y2, m2, d2;
    int any_eop, any_sop, all_same;
    int skip = 0, pad = 0, nm = -1;

    if (sscanf(pdinfo->stobs, "%d/%d/%d", &y1, &m1, &d1) != 3) {
	return -1;
    }
    if (sscanf(pdinfo->endobs, "%d/%d/%d", &y2, &m2, &d2) != 3) {
	return -1;
    }

    if (y1 < 100) {
	y1 = FOUR_DIGIT_YEAR(y1);
    }
    if (y2 < 100) {
	y2 = FOUR_DIGIT_YEAR(y2);
    }

    nm = 12 * (y2 - y1) + m2 - m1 + 1;

    get_daily_compact_params(default_method, &any_eop, &any_sop,
			     &all_same, pdinfo);

    *startyr = y1;
    *startmon = m1;
    *endyr = y2;
    *endmon = m2;

    if (!day_starts_month(d1, m1, y1, pdinfo->pd, &pad) && !any_eop) {
	if (*startmon == 12) {
	    *startmon = 1;
	    *startyr += 1;
	} else {
	    *startmon += 1;
	}
	skip = 1;
	nm--;
    }

    if (!day_ends_month(d2, m2, y2, pdinfo->pd) && !any_sop) {
	if (*endmon == 1) {
	    *endmon = 12;
	    *endyr -= 1;
	} else {
	    *endmon -= 1;
	}
	nm--;
    }

    if (pad) {
	*offset = -1;
    } else {
	*offset = get_daily_offset(pdinfo, y1, m1, d1, skip);
    }

    *p_any_eop = any_eop;

    return nm;
}

#define WEEKLY_DEBUG 0

static int 
weeks_to_months_exec (double **mZ, const double **Z, const DATAINFO *pdinfo, int mn)
{ 
    char obsstr[OBSLEN];
    int *den;
    int yr, mon, day;
    int monbak = 0;
    int i, s, t = 0;
    int err = 0;

    den = malloc(pdinfo->v * sizeof *den);
    if (den == NULL) {
	return E_ALLOC;
    }

    for (i=1; i<pdinfo->v; i++) {
	/* initialize all series, first obs */
	if (var_is_series(pdinfo, i)) {
	    mZ[i][0] = 0.0;
	    den[i] = 0;
	}
    }    

    for (s=0; s<pdinfo->n; s++) {
	/* loop across the weekly obs in this month */
#if WEEKLY_DEBUG
	fprintf(stderr, "\n** weekly to monthly monthly loop, s = %d\n", s);
#endif
	ntodate_full(obsstr, s, pdinfo);
	sscanf(obsstr, "%d/%d/%d", &yr, &mon, &day);
#if WEEKLY_DEBUG
	fprintf(stderr, " month = %d\n", mon);
#endif

	if (monbak > 0 && mon != monbak) {
	    /* new month: finalize the previous one */
	    for (i=1; i<pdinfo->v; i++) {
		if (var_is_series(pdinfo, i)) {
#if WEEKLY_DEBUG
		    fprintf(stderr, " finalizing monthly obs %d, var %d, den = %d\n", 
			    t, i, den[i]);
#endif
		    if (den[i] > 0) {
			mZ[i][t] /= (double) den[i];
		    } else {
			mZ[i][t] = NADBL;
		    }
		}
	    }
	    /* and start another? */
	    if (s < pdinfo->n - 1) {
		t++;
		for (i=1; i<pdinfo->v; i++) {
		    /* initialize all series, current obs */
		    if (var_is_series(pdinfo, i)) {
			mZ[i][t] = 0.0;
			den[i] = 0;
		    }
		}  		
	    }
	} 

	/* cumulate non-missing weekly observations */
	for (i=1; i<pdinfo->v; i++) {
	    if (var_is_series(pdinfo, i)) {
		if (!na(Z[i][s])) {
		    mZ[i][t] += Z[i][s];
		    den[i] += 1;
		}
		if (mon == monbak && s == pdinfo->n - 1) {
#if WEEKLY_DEBUG
		    fprintf(stderr, " finalizing monthly obs %d, var %d, den = %d\n", 
			    t, i, den[i]);
#endif
		    /* reached the end: ship out last obs */
		    if (den[i] > 0) {
			mZ[i][t] /= (double) den[i];
		    } else {
			mZ[i][t] = NADBL;
		    }
		}		    
	    }
	}

	monbak = mon;
    }

    free(den);

    return err;
}

static int 
weeks_to_months_check (const DATAINFO *pdinfo, int *startyr, int *endyr,
		       int *startmon, int *endmon)
{ 
    char obsstr[OBSLEN];
    int yr, mon, day;
    int wcount = 0, mcount = 0;
    int monbak = 0;
    int t, err = 0;

    for (t=0; t<pdinfo->n; t++) {
	ntodate_full(obsstr, t, pdinfo);
	if (sscanf(obsstr, "%d/%d/%d", &yr, &mon, &day) != 3) {
	    err = 1;
	    break;
	}
	if (monbak == 0) {
	    /* first obs */
	    fprintf(stderr, "starting month = '%d'\n", mon);
	    *startyr = yr;
	    *startmon = mon;
	    mcount++;
	    wcount = 1;	
	} else if (mon != monbak) {
	    /* got a new month: report on previous one */
#if WEEKLY_DEBUG
	    fprintf(stderr, "month %d ('%d'), weekly obs = %d\n", 
		    mcount, monbak, wcount);
#endif
	    mcount++;
	    wcount = 1;
	} else {
	    /* continuation of current month */
	    wcount++;
	}
	monbak = mon;
    }

    /* flush the last observation */
#if WEEKLY_DEBUG
    fprintf(stderr, "month %d ('%d'), weekly obs = %d\n", 
	    mcount, monbak, wcount);
#endif
    *endyr = yr;
    *endmon = mon;

    return mcount;
}

/* for now, averaging is the only compaction option in this case */

static int weekly_dataset_to_monthly (double ***pZ, DATAINFO *pdinfo,
				      CompactMethod default_method)
{
    double **mZ = NULL;
    DATAINFO minfo;
    int startyr = 1, endyr;
    int startmon = 1, endmon;
    double *x;
    int nseries = 0;
    int i, err = 0;

    minfo.n = weeks_to_months_check(pdinfo, &startyr, &endyr, &startmon, &endmon);
    fprintf(stderr, "Weekly data: found %d months\n", minfo.n);
    if (minfo.n <= 0) {
	return E_DATA;
    }

    minfo.v = pdinfo->v;
    err = allocate_Z(&mZ, &minfo);
    if (err) {
	return err;
    }

    /* handle scalars */
    for (i=1; i<pdinfo->v && !err; i++) {
	if (var_is_scalar(pdinfo, i)) {
	    x = realloc(mZ[i], sizeof *x);
	    if (x == NULL) {
		err = E_ALLOC;
	    } else {
		mZ[i] = x;
		mZ[i][0] = (*pZ)[i][0];
	    }
	} else {
	    nseries++;
	}
    }

    /* compact series */
    if (!err && nseries > 0) {
	err = weeks_to_months_exec(mZ, (const double **) *pZ, pdinfo, minfo.n);
    }

    if (err) {
	free_Z(mZ, &minfo);
    } else {
	free_Z(*pZ, pdinfo);
	*pZ = mZ;

	pdinfo->n = minfo.n;
	pdinfo->pd = 12;
	sprintf(pdinfo->stobs, "%04d:%02d", startyr, startmon);
	sprintf(pdinfo->endobs, "%04d:%02d", endyr, endmon);
	pdinfo->sd0 = get_date_x(pdinfo->pd, pdinfo->stobs);
	pdinfo->t1 = 0;
	pdinfo->t2 = pdinfo->n - 1;
    }
    
    return err;
}

/* conversion to weekly using a "representative day", e.g. use
   each Wednesday value.  repday is 0-based on Sunday.
*/

static int daily_dataset_to_weekly (double **Z, DATAINFO *pdinfo,
				    int repday)
{
    int y1, m1, d1;
    char obs[OBSLEN];
    double *x = NULL;
    int n = 0, n_ok = 0;
    int wday, ok;
    int i, t, err = 0;

    fprintf(stderr, "daily_dataset_to_weekly: repday = %d\n", repday);

    for (t=0; t<pdinfo->n; t++) {
	ntodate_full(obs, t, pdinfo);
	wday = get_day_of_week(obs);
	if (wday == repday) {
	    ok = 0;
	    for (i=1; i<pdinfo->v; i++) {
		if (var_is_series(pdinfo, i) && !na(Z[i][t])) {
		    ok = 1;
		    break;
		}
	    }
	    if (ok) {
		n_ok++;
	    }
	    if (n == 0) {
		sscanf(obs, "%d/%d/%d", &y1, &m1, &d1);
	    }
	    n++;
	}
    }

    if (n_ok == 0) {
	gretl_errmsg_set(_("Compacted dataset would be empty"));
	return 1;
    }	

    fprintf(stderr, "n=%d, n_ok=%d, y1=%d, m1=%d, d1=%d\n", 
	    n, n_ok, y1, m1, d1);

    x = malloc(n * sizeof *x);
    if (x == NULL) {
	return E_ALLOC;
    }

    for (i=1; i<pdinfo->v && !err; i++) {
	double *tmp;
	int s = 0;

	if (i > 0 && var_is_scalar(pdinfo, i)) {
	    continue;
	}
	if (i > 0) {
	    for (t=0; t<pdinfo->n; t++) {
		ntodate_full(obs, t, pdinfo);
		wday = get_day_of_week(obs);
		if (wday == repday) {
		    x[s++] = Z[i][t];
		}
	    }
	}
	tmp = realloc(Z[i], n * sizeof **Z);
	if (tmp == NULL) {
	    err = E_ALLOC;
	} else {
	    Z[i] = tmp;
	    for (t=0; t<n; t++) { 
		Z[i][t] = (i == 0)? 1.0 : x[t];
	    }
	}
    }

    free(x);

    if (!err) {
	pdinfo->n = n;
	pdinfo->pd = 52;
	
	sprintf(pdinfo->stobs, "%04d/%02d/%02d", y1, m1, d1);
	pdinfo->sd0 = get_date_x(pdinfo->pd, pdinfo->stobs);
	pdinfo->t1 = 0;
	pdinfo->t2 = pdinfo->n - 1;
	ntodate_full(pdinfo->endobs, pdinfo->t2, pdinfo);

	dataset_destroy_obs_markers(pdinfo);
    }    

    return err;
}

static int daily_dataset_to_monthly (double ***pZ, DATAINFO *pdinfo,
				     CompactMethod default_method)
{
    int nm, startyr, startmon, endyr, endmon;
    int offset, any_eop;
    int i, err = 0;

    nm = get_n_ok_months(pdinfo, default_method, &startyr, &startmon,
			 &endyr, &endmon, &offset, &any_eop);

    if (nm <= 0) {
	gretl_errmsg_set(_("Compacted dataset would be empty"));
	err = 1;
    } else {
	for (i=0; i<pdinfo->v && !err; i++) {
	    CompactMethod method;
	    double *x;

	    if (i > 0 && var_is_scalar(pdinfo, i)) {
		continue;
	    }

	    method = COMPACT_METHOD(pdinfo, i);
	    if (method == COMPACT_NONE) {
		method = default_method;
	    }

	    x = daily_series_to_monthly((*pZ)[i], pdinfo, i, nm,
					startyr, startmon, 
					offset, any_eop, method);
	    if (x == NULL) {
		err = E_ALLOC;
	    } else {
		free((*pZ)[i]);
		(*pZ)[i] = x;
	    }
	}
    }

    if (!err) {
	pdinfo->n = nm;
	pdinfo->pd = 12;
	sprintf(pdinfo->stobs, "%04d:%02d", startyr, startmon);
	sprintf(pdinfo->endobs, "%04d:%02d", endyr, endmon);
	pdinfo->sd0 = get_date_x(pdinfo->pd, pdinfo->stobs);
	pdinfo->t1 = 0;
	pdinfo->t2 = pdinfo->n - 1;

	dataset_destroy_obs_markers(pdinfo);
    }

    return err;
}

static int get_daily_skip (const DATAINFO *pdinfo, int t)
{
    int dd = calendar_obs_number(pdinfo->S[t], pdinfo) -
	calendar_obs_number(pdinfo->S[t-1], pdinfo);

    if (dd == 0) {
	fprintf(stderr, "get_daily_skip: S[%d] = '%s', S[%d] = '%s'\n", 
		t, pdinfo->S[t], t-1, pdinfo->S[t-1]);
    }

    return dd - 1;
}

static int 
insert_missing_hidden_obs (double ***pZ, DATAINFO *pdinfo,
			   int nmiss)
{
    int oldn = pdinfo->n;
    double *tmp;
    int i, s, t, skip;
    int err = 0;

    tmp = malloc(oldn * sizeof *tmp);
    if (tmp == NULL) {
	return E_ALLOC;
    }

    err = dataset_add_observations(nmiss, pZ, pdinfo, OPT_A);
    if (err) {
	free(tmp);
	return err;
    }

    for (i=1; i<pdinfo->v && !err; i++) {
	for (s=0; s<oldn; s++) {
	    tmp[s] = (*pZ)[i][s];
	}

	(*pZ)[i][0] = tmp[0];
	t = 1;
	for (s=1; s<oldn; s++) {
	    skip = get_daily_skip(pdinfo, s);
	    if (skip < 0) {
		err = E_DATA;
		break;
	    }
	    while (skip--) {
		(*pZ)[i][t++] = NADBL;
	    }
	    (*pZ)[i][t++] = tmp[s];
	}
    }

    free(tmp);

    if (!err) {
	dataset_destroy_obs_markers(pdinfo);
	pdinfo->t2 = pdinfo->n - 1;
	ntodate_full(pdinfo->endobs, pdinfo->n - 1, pdinfo);
    }

    return err;
}

int maybe_expand_daily_data (double ***pZ, DATAINFO *pdinfo)
{
    int nmiss = n_hidden_missing_obs(pdinfo);
    int err = 0;

    fprintf(stderr, "n_hidden_missing_obs: nmiss = %d\n", nmiss);

    if (nmiss < 0) {
	err = 1;
    } else if (nmiss > 0) {
	err = insert_missing_hidden_obs(pZ, pdinfo, nmiss);
    }

    return err;
}

/**
 * compact_data_set:
 * @pZ: pointer to data array.
 * @pdinfo: data information struct.
 * @newpd: target data frequency.
 * @default_method: code for the default compaction method.
 * @monstart: FIXME add explanation.
 * @repday: "representative day" for conversion from daily
 * to weekly data (with method %COMPACT_WDAY only).
 * 
 * Compact the data set from higher to lower frequency.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int compact_data_set (double ***pZ, DATAINFO *pdinfo, int newpd,
		      CompactMethod default_method, int monstart,
		      int repday)
{
    int newn, oldn = pdinfo->n, oldpd = pdinfo->pd;
    int compfac;
    int startmaj, startmin;
    int endmaj, endmin;
    int any_eop, all_same;
    int min_startskip = 0;
    char stobs[OBSLEN];
    int i, err = 0;

    *gretl_errmsg = '\0';

    if (oldpd == 52) {
	return weekly_dataset_to_monthly(pZ, pdinfo, default_method);
    }

    if (dated_daily_data(pdinfo)) {
	/* allow for the possibility that the daily dataset
	   contains "hidden" or suppressed missing observations
	   (holidays are just skipped, not marked as NA)
	*/
	err = maybe_expand_daily_data(pZ, pdinfo);
	if (err) {
	    strcpy(gretl_errmsg, "Error expanding daily data with missing observations");
	    return err;
	}
    }

    if (newpd == 52 && oldpd >= 5 && oldpd <= 7 && 
	default_method == COMPACT_WDAY) {
	/* daily to weekly, using "representative day" */
	return daily_dataset_to_weekly(*pZ, pdinfo, repday);
    } else if (newpd == 12 && oldpd >= 5 && oldpd <= 7) {
	/* daily to monthly: special */
	return daily_dataset_to_monthly(pZ, pdinfo, default_method);
    } else if (oldpd >= 5 && oldpd <= 7) {
	/* daily to weekly */
	compfac = oldpd;
	if (dated_daily_data(pdinfo)) {
	    startmin = get_day_of_week(pdinfo->stobs);
	    if (oldpd == 7) {
		if (monstart) {
		    if (startmin == 0) startmin = 7;
		} else {
		    startmin++;
		}
	    }
	} else {
	    startmin = 1;
	}
    } else {
	compfac = oldpd / newpd;
	/* get starting obs major and minor components */
	if (!get_obs_maj_min(pdinfo->stobs, &startmaj, &startmin)) {
	    return 1;
	}
	/* get ending obs major and minor components */
	if (!get_obs_maj_min(pdinfo->endobs, &endmaj, &endmin)) {
	    return 1;
	} 
    }

    min_startskip = oldpd;
    newn = 0;
    any_eop = 0;
    all_same = 1;
    get_global_compact_params(compfac, startmin, endmin, default_method,
			      &min_startskip, &newn, &any_eop, &all_same, 
			      pdinfo);
    if (newn == 0) {
	gretl_errmsg_set(_("Compacted dataset would be empty"));
	return 1;
    }    

    if (newpd == 1) {
	if (min_startskip > 0 && !any_eop) { 
	    startmaj++;
	}
	sprintf(stobs, "%d", startmaj);
    } else if (newpd == 52) {
	strcpy(stobs, "1");
    } else {
	int m0 = startmin + min_startskip;
	int minor = m0 / compfac + (m0 % compfac > 0);

	if (minor > newpd) {
	    startmaj++;
	    minor -= newpd;
	}
	format_obs(stobs, startmaj, minor, newpd);
    }

    /* revise datainfo members */
    strcpy(pdinfo->stobs, stobs);
    pdinfo->pd = newpd;
    pdinfo->n = newn;
    pdinfo->sd0 = get_date_x(pdinfo->pd, pdinfo->stobs);
    pdinfo->t1 = 0;
    pdinfo->t2 = pdinfo->n - 1;
    ntodate(pdinfo->endobs, pdinfo->t2, pdinfo);
    
    if (oldpd >= 5 && oldpd <= 7) {
	/* remove any daily date strings */
	dataset_destroy_obs_markers(pdinfo);
    }

    /* compact the individual data series */
    for (i=0; i<pdinfo->v && err == 0; i++) {
	if (var_is_series(pdinfo, i)) {
	    CompactMethod this_method = default_method;
	    int startskip = min_startskip;
	    double *x;

	    if (!all_same) {
		if (COMPACT_METHOD(pdinfo, i) != COMPACT_NONE) {
		    this_method = COMPACT_METHOD(pdinfo, i);
		}

		startskip = compfac - (startmin % compfac) + 1;
		startskip = startskip % compfac;
		if (this_method == COMPACT_EOP) {
		    if (startskip > 0) {
			startskip--;
		    } else {
			startskip = compfac - 1;
		    }
		}
	    }

	    x = compact_series((*pZ)[i], i, pdinfo->n, oldn, startskip, 
			       min_startskip, compfac, this_method);
	    if (x == NULL) {
		err = E_ALLOC;
	    } else {
		free((*pZ)[i]);
		(*pZ)[i] = x;
	    }
	}
    }

    return err;
}

/**
 * expand_data_set:
 * @pZ: pointer to data array.
 * @pdinfo: data information struct.
 * @newpd: target data frequency
 * 
 * Expand the data set from lower to higher frequency: an "expert"
 * option.  This is supported at present only for expansion from
 * annual to quarterly or monthly, or from quarterly to monthly.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int expand_data_set (double ***pZ, DATAINFO *pdinfo, int newpd)
{
    char stobs[12];
    int oldn = pdinfo->n;
    int mult, newn, addobs;
    double *x = NULL;
    int i, j, s, t;
    int err = 0;

    if (pdinfo->pd != 1 && pdinfo->pd != 4) {
	return E_PDWRONG;
    } else if (pdinfo->pd == 1 && newpd != 4 && newpd != 12) {
	return E_DATA;
    } else if (pdinfo->pd == 4 && newpd != 12) {
	return E_DATA;
    }

    x = malloc(oldn * sizeof *x);
    if (x == NULL) {
	return E_ALLOC;
    }

    mult = newpd / pdinfo->pd;
    newn = mult * pdinfo->n;
    addobs = newn - oldn;

    err = dataset_add_observations(addobs, pZ, pdinfo, OPT_NONE);
    if (err) {
	goto bailout;
    }

    for (i=1; i<pdinfo->v; i++) {
	if (var_is_scalar(pdinfo, i)) {
	    continue;
	}
	for (t=0; t<oldn; t++) {
	    x[t] = (*pZ)[i][t];
	}
	s = 0;
	for (t=0; t<oldn; t++) {
	    for (j=0; j<mult; j++) {
		(*pZ)[i][s++] = x[t];
	    }
	}
    }

     if (pdinfo->pd == 1) {
	strcpy(stobs, pdinfo->stobs);
	if (newpd == 4) {
	    strcat(stobs, ":1");
	} else {
	    strcat(stobs, ":01");
	}
    } else {
	int yr, qtr, mo;

	sscanf(pdinfo->stobs, "%d:%d", &yr, &qtr);
	mo = (qtr - 1) * 3 + 1;
	sprintf(stobs, "%d:%02d", yr, mo);
    }

    strcpy(pdinfo->stobs, stobs);
    pdinfo->pd = newpd;
    pdinfo->sd0 = get_date_x(pdinfo->pd, pdinfo->stobs);

    if (pdinfo->markers) {
	dataset_destroy_obs_markers(pdinfo);
    }

 bailout:
    
    free(x);

    return err;
}
