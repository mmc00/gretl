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

#include "gretl.h"
#include "gretl_ipc.h"

#if defined(__linux) || defined(linux)
# include <dirent.h>
# include <signal.h>
#elif defined WIN32
# include <windows.h>
# include <tlhelp32.h>
#elif defined (MAC_NATIVE)
# include <sys/proc_info.h>
# include <libproc.h>
#endif

static int my_sequence_number;

int gretl_sequence_number (void)
{
    return my_sequence_number;
}

#if defined(__linux) || defined(linux)

/* Check for a prior running gretl instance. This is not as clever as
   it should be, since it ignores UID.  On a true multi-user system it
   will count gretl instances being run by other users. Returns the
   pid of the prior instance or 0 if none.
*/

long gretl_prior_instance (void) 
{
    DIR *dir;
    struct dirent *ent;
    char buf[128];
    char pname[32] = {0};
    char state;
    FILE *fp;
    long pid, mypid;
    long gpid = 0;

    if ((dir = opendir("/proc")) == NULL) {
        perror("can't open /proc");
        return 0;
    }

    mypid = (long) getpid();

    while ((ent = readdir(dir)) != NULL && gpid == 0) {
        long lpid = atol(ent->d_name);

        if (lpid < 0 || lpid == mypid) {
            continue;
	}

        snprintf(buf, sizeof buf, "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp != NULL) {
            if ((fscanf(fp, "%ld (%31[^)]) %c", &pid, pname, &state)) != 3) {
                printf("proc fscanf failed\n");
                fclose(fp);
                closedir(dir);
                return 0; 
            }
            if (!strcmp(pname, "gretl_x11") || !strcmp(pname, "lt-gretl_x11")) {
		gpid = lpid;
            }
            fclose(fp);
        }
    }

    closedir(dir);

    return gpid;
}

static int invalid_pid (long test)
{
    char buf[128];
    char pname[32] = {0};
    FILE *fp;
    int err = 0;

    snprintf(buf, sizeof buf, "/proc/%ld/stat", test);
    fp = fopen(buf, "r");
    
    if (fp == NULL) {
	/* no such pid? */
	return 1;
    } else {
	char state;
	long pid;

	if ((fscanf(fp, "%ld (%31[^)]) %c", &pid, pname, &state)) != 3) {
	    /* huh? */
	    err = 1;
	} else if (strcmp(pname, "gretl_x11") && strcmp(pname, "lt-gretl_x11")) {
	    /* it's not gretl */
	    err = 1;
	}
	fclose(fp);
    }

    return err;
}

/* Signal handler for the case where a newly started gretl process
   hands off to a previously running process, passing the name of a
   file to be opened via a small file in the user's dotdir.
*/

static void open_handler (int sig, siginfo_t *sinfo, void *context)
{
    int try_open = 0;

    if (sig == SIGUSR1 && sinfo->si_code == SI_QUEUE) {
	if (sinfo->si_uid == getuid() &&
	    sinfo->si_value.sival_ptr == (void *) 0xf0) {
	    char fname[FILENAME_MAX];
	    char path[FILENAME_MAX];
	    long spid = sinfo->si_pid;
	    FILE *fp;

	    sprintf(fname, "%s/open-%ld", gretl_dotdir(), spid);
	    fp = fopen(fname, "r");
	    if (fp != NULL) {
		if (fgets(path, sizeof path, fp)) {
		    tailstrip(path);
		    if (strcmp(path, "none")) {
			*tryfile = '\0';
			strncat(tryfile, path, MAXLEN - 1);
			try_open = 1;
		    }
		}	    
		fclose(fp);
	    }
	    remove(fname);
	}
    }

    gtk_window_present(GTK_WINDOW(mdata->main));

    if (try_open) {
	real_open_tryfile();
    }
}

/* at start-up, install a handler for SIGUSR1 */

int install_open_handler (void)
{
    static struct sigaction action;

    action.sa_sigaction = open_handler;
    sigemptyset(&action.sa_mask);    
    action.sa_flags = SA_SIGINFO;

    return sigaction(SIGUSR1, &action, NULL);
}

/* For the case where a new gretl process has been started
   (possibly in response to double-clicking a suitable
   filename), but the user doesn't really want a new
   process and would prefer that the file be opened in
   a previously running gretl instance. We send SIGUSR1
   to the prior instance and exit.
*/

gboolean forward_open_request (long gpid, const char *fname)
{
    gboolean ret = FALSE;
    char tmpname[FILENAME_MAX];
    FILE *fp;

    sprintf(tmpname, "%s/open-%ld", gretl_dotdir(), (long) getpid());
    fp = fopen(tmpname, "w");

    if (fp != NULL) {
	union sigval data;
	int err;

	fprintf(fp, "%s\n", *fname ? fname : "none");
	fclose(fp);
	data.sival_ptr = (void *) 0xf0;
	err = sigqueue(gpid, SIGUSR1, data);
	ret = !err;
    }

    return ret;
}

#elif defined WIN32

/* Below: since POSIX signals are not supported on Windows,
   we implement the equivalent of the above functionality
   for Linux using the Windows messaging API.
*/

/* message number for inter-program communication */
static UINT WM_GRETL;

/* Look for an already running gretlw32 instance: if we find
   one, return its pid, otherwise return 0. While we're at it
   we register our custom IPC message ID if that's not
   already done.
*/

long gretl_prior_instance (void) 
{
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    long mypid, gpid = 0;

    if (WM_GRETL == 0) {
	WM_GRETL = RegisterWindowMessage((LPCTSTR) "gretl_message");
    }

    mypid = (long) GetCurrentProcessId();

    if (hsnap) {
        PROCESSENTRY32 pe;
	int match;
	char *s;

        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hsnap, &pe)) {
            do {
		if (pe.th32ProcessID != mypid) {
		    s = strrchr(pe.szExeFile, '\\');
		    if (s != NULL) {
			match = !strcmp(s + 1, "gretlw32.exe");
		    } else {
			match = !strcmp(pe.szExeFile, "gretlw32.exe");
		    }
		    if (match) {
			gpid = pe.th32ProcessID;
		    }
		}		    
            } while (gpid == 0 && Process32Next(hsnap, &pe));
	}
	CloseHandle(hsnap);
    }

    return gpid;
}

static int invalid_pid (long test)
{
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    long mypid, gpid = 0;
    int err = 0;

    if (hsnap) {
        PROCESSENTRY32 pe;
	int match = 0;
	char *s;

        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hsnap, &pe)) {
            do {
		if (pe.th32ProcessID == test) {
		    match = 1;
		    s = strrchr(pe.szExeFile, '\\');
		    if (s != NULL) {
			err = strcmp(s + 1, "gretlw32.exe");
		    } else {
			err = strcmp(pe.szExeFile, "gretlw32.exe");
		    }
		}		    
            } while (!match && Process32Next(hsnap, &pe));
	}
	CloseHandle(hsnap);
    }

    if (!err && !match) {
	/* test pid not found */
	err = 1;
    }

    return err;
}

/* Process a message coming from another gretlw32 instance (with pid
   @gotpid). Such a message calls for a hand-off to "this" instance,
   and some information regarding the hand-off is contained in a
   little file in the user's dotdir, with a name on the pattern
   "open-<pid>" where <pid> should equal @gotpid. Call this the IPC
   file.

   If the hand-off involves opening a file (which will be the case 
   if the trigger is double-clicking on a gretl-associated file),
   the name of this file is written into the IPC file; otherwise
   the IPC file contains the word "none".
*/

static void win32_handle_message (long gotpid)
{
    char fname[FILENAME_MAX];
    int try_open = 0;
    FILE *fp;

    sprintf(fname, "%sopen-%ld", gretl_dotdir(), gotpid);
    fprintf(stderr, "handle_message: trying '%s'\n", fname);
    fp = fopen(fname, "r");

    if (fp != NULL) {
	char path[FILENAME_MAX];

	fprintf(stderr, " file opened OK\n");
	if (fgets(path, sizeof path, fp) != NULL) {
	    tailstrip(path);
	    fprintf(stderr, " path = '%s'\n", path);
	    if (strcmp(path, "none")) {
		*tryfile = '\0';
		strncat(tryfile, path, MAXLEN - 1);
		try_open = 1;
	    }
	}	    
	fclose(fp);
    }
    remove(fname);

    gtk_window_present(GTK_WINDOW(mdata->main));

    if (try_open) {
	/* we picked up the name of a file to open */
	real_open_tryfile();
    }
}

/* If we receive a special WM_GRETL message from another gretlw32
   instance, process it and remove it from the message queue;
   otherwise hand the message off to GDK.
*/

static GdkFilterReturn mdata_filter (GdkXEvent *xevent,
				     GdkEvent *event,
				     gpointer data)
{
    MSG *msg = (MSG *) xevent;

    if (msg->message == WM_GRETL && msg->wParam == 0xf0) {
	fprintf(stderr, "mdata_filter: got WM_GRETL\n");
	win32_handle_message((long) msg->lParam);
	return GDK_FILTER_REMOVE;
    }

    return GDK_FILTER_CONTINUE;
}

/* Add a filter to intercept WM_GRETL messages, which would
   otherwise get discarded by GDK */

int install_open_handler (void)
{
    gdk_window_add_filter(NULL, mdata_filter, NULL);
    return 0;
}

/* Find the window handle associated with a given pid:
   this is used when we've found the pid of a running
   gretlw32 instance and we need its window handle for
   use with SendMessage().
*/

static HWND get_hwnd_for_pid (long gpid)
{
    HWND hw = GetTopWindow(NULL);
    DWORD pid;

    while (hw) {
	GetWindowThreadProcessId(hw, &pid);
	if (pid == gpid) {
	    break;
	}
	hw = GetNextWindow(hw, GW_HWNDNEXT);
    }

    return hw;
}

/* Try forwarding a request to the prior gretlw32 instance with 
   pid @gpid, either to open a file or just to show itself. 

   Return TRUE if we're able to do this, otherwise FALSE.
*/

gboolean forward_open_request (long gpid, const char *fname)
{
    HWND hw = get_hwnd_for_pid(gpid);
    gboolean ret = FALSE;

    if (!hw) {
	fprintf(stderr, "forward_open_request: couldn't find HWND\n");
    } else {
	long mypid = (long) GetCurrentProcessId();
	char tmpname[FILENAME_MAX];
	FILE *fp;

	sprintf(tmpname, "%sopen-%ld", gretl_dotdir(), mypid);
	fp = fopen(tmpname, "w");
	
	if (fp == NULL) {
	    fprintf(stderr, "forward_open_request: couldn't write '%s'\n", tmpname);
	} else {
	    fprintf(fp, "%s\n", *fname ? fname: "none");
	    fclose(fp);
	    SendMessage(hw, WM_GRETL, 0xf0, mypid);
	    ret = TRUE;
	}
    }

    return ret;
}

#elif defined(MAC_NATIVE)

/* Note: for the Mac we don't do the relatively fancy stuff that we
   attempt above for Linux and Windows, because the default on Mac
   is the other way round: the OS sticks with a single instance of
   gretl unless we specifically request another instance.

   Right now, all we use gretl_prior_instance() for on the Mac is
   to adjust the window title when the user chooses to run more than 
   one gretl instance.
*/

long gretl_prior_instance (void)
{
    int nproc = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    size_t psize;
    pid_t *pids;
    pid_t mypid;
    int i, match;
    long gpid = 0;
    
    psize = nproc * sizeof *pids;
    pids = malloc(psize);
    if (pids == NULL) {
	return 0;
    }

    mypid = getpid();

    for (i=0; i<nproc; i++) {
	pids[i] = 0;
    }
    
    proc_listpids(PROC_ALL_PIDS, 0, pids, psize);

    for (i=0; i < nproc && gpid == 0; i++) {
	if (pids[i] == 0 || pids[i] == mypid) { 
	    continue; 
	}
	memset(buf, 0, sizeof buf);
	proc_pidpath(pids[i], buf, sizeof buf);
	if (*buf != '\0') {
	    char *s = strrchr(buf, '/');

	    if (s != NULL) {
	        match = !strcmp("gretl", s + 1);
            } else {
	        match = !strcmp("gretl", buf);
            }
	    if (match) {
		gpid = (long) pids[i];
	    }
	} 
    }

    free(pids);

    return gpid;
}

static int invalid_pid (long test)
{
    int nproc = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    size_t psize;
    pid_t *pids;
    int i, match = 0;
    int err = 0;
    
    psize = nproc * sizeof *pids;
    pids = malloc(psize);
    if (pids == NULL) {
	return 0;
    }

    for (i=0; i<nproc; i++) {
	pids[i] = 0;
    }
    
    proc_listpids(PROC_ALL_PIDS, 0, pids, psize);

    for (i=0; i < nproc && !match; i++) {
	if (pids[i] == test) {
	    match = 1;
	    memset(buf, 0, sizeof buf);
	    proc_pidpath(pids[i], buf, sizeof buf);
	    if (*buf != '\0') {
		char *s = strrchr(buf, '/');

		if (s != NULL) {
		    err = strcmp("gretl", s + 1);
		} else {
		    err = strcmp("gretl", buf);
		}
	    } 
	} 
    }

    free(pids);

    if (!err && !match) {
	err = 1;
    }

    return err;
}

#else /* none of the above */

long gretl_prior_instance (void) 
{
    return 0;
}

#endif /* OS variations */

int write_pid_to_file (void)
{
    char pidfile[FILENAME_MAX];
    FILE *f1;
    long mypid;
    int err = 0;

#ifdef G_OS_WIN32
    mypid = (long) GetCurrentProcessId();
#else
    mypid = (long) getpid();
#endif

    sprintf(pidfile, "%sgretl.pid", gretl_dotdir());
    f1 = gretl_fopen(pidfile, "r");

    if (f1 == NULL) {
	/* no pid file, starting from scratch */
	f1 = gretl_fopen(pidfile, "w");
	if (f1 == NULL) {
	    err = E_FOPEN;
	} else {
	    fprintf(f1, "%ld 1\n", mypid);
	    fclose(f1);
	}
    } else {
	/* we already have a pid file */
	char newfile[FILENAME_MAX];
	char buf[32];
	FILE *f2;
	long pid;
	int m, n = 0;

	sprintf(newfile, "%sgretl.new", gretl_dotdir());
	f2 = gretl_fopen(newfile, "w");
	if (f2 == NULL) {
	    err = E_FOPEN;
	} else {
	    while (fgets(buf, sizeof buf, f1)) {
		sscanf(buf, "%ld %d", &pid, &m);
		if (!invalid_pid(pid)) {
		    fputs(buf, f2);
		    n = m;
		}
	    }
	    n++;
	    fprintf(f2, "%ld %d\n", mypid, n);
	    fclose(f2);
	    my_sequence_number = n;
	}
	fclose(f1);
	err = gretl_rename(newfile, pidfile);
    }

    return err;
}

int delete_pid_from_file (void)
{
    char pidfile[FILENAME_MAX];
    FILE *f1, *f2;
    long mypid;
    int err = 0;

#ifdef G_OS_WIN32
    mypid = (long) GetCurrentProcessId();
#else
    mypid = (long) getpid();
#endif

    sprintf(pidfile, "%sgretl.pid", gretl_dotdir());

    f1 = fopen(pidfile, "r");
    if (f1 == NULL) {
	err = E_FOPEN;
    } else {
	char newfile[FILENAME_MAX];
	char buf[32];
	long pid;
	int nleft = 0;

	sprintf(newfile, "%sgretl.new", gretl_dotdir());
	f2 = gretl_fopen(newfile, "w");
	if (f2 == NULL) {
	    err = E_FOPEN;
	} else {
	    while (fgets(buf, sizeof buf, f1)) {
		sscanf(buf, "%ld", &pid);
		if (pid != mypid) {
		    fputs(buf, f2);
		    nleft++;
		}
	    }
	    fclose(f2);
	}
	fclose(f1);
	if (nleft > 0) {
	    err = gretl_rename(newfile, pidfile);
	} else {
	    gretl_remove(newfile);
	    gretl_remove(pidfile);
	}
    }

    return err;
}
