/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2006 by Allin Cottrell
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

#ifdef USE_LIBGSF

/* not ready yet */

#else

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "zipunzip.h"

int gretl_unzip_file (const char *fname, GError **gerr)
{
    return zipfile_extract_files(fname, NULL, 0, gerr);
}

int gretl_make_zipfile (const char *fname, const char *path,
			GError **gerr)
{
    const char *array[2];

    array[0] = path;
    array[1] = NULL;

    return zipfile_archive_files(fname, array, 9, 
				 ZIP_RECURSE_DIRS,
				 gerr);
}

int gretl_is_zipfile (const char *fname)
{
    zipinfo *zinfo;
    int ret = 0;

    zinfo = zipfile_get_info(fname, 0, NULL);
    if (zinfo != NULL) {
	zipinfo_destroy(zinfo);
	ret = 1;
    }

    return ret;
}

#endif
