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

#if defined(__linux) || defined(linux)

/* Get a count of currently running gretl instances. This
   is not as clever as it should be, since it ignores UID.
   On a true multi-user system it will count gretl
   instances being run by other users. 
*/

int get_instance_count (long *ppid) 
{
    DIR *dir;
    struct dirent *ent;
    char buf[128];
    long pid;
    char pname[32] = {0};
    char state;
    long mypid;
    FILE *fp;
    int count = 0;

    if ((dir = opendir("/proc")) == NULL) {
        perror("can't open /proc");
        return -1;
    }

    mypid = (long) getpid();

    while ((ent = readdir(dir)) != NULL) {
        long lpid = atol(ent->d_name);

        if (lpid < 0) {
            continue;
	}

        snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp != NULL) {
            if ((fscanf(fp, "%ld (%31[^)]) %c", &pid, pname, &state)) != 3) {
                printf("proc fscanf failed\n");
                fclose(fp);
                closedir(dir);
                return -1; 
            }
            if (!strcmp(pname, "gretl_x11") || !strcmp(pname, "lt-gretl_x11")) {
		if (ppid != NULL && lpid != mypid && *ppid <= 0) {
		    *ppid = lpid;
		}
		count++;
            }
            fclose(fp);
        }
    }

    closedir(dir);

    return count;
}

/* Signal handler for the case where a newly started gretl
   process hands off to a previously running process, 
   passing the name of a file to be opened via a small file
   in the user's dotdir.
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
   to the previous instance and exit.
*/

int try_forwarding_open_request (long gpid, const char *fname)
{
    char tmpname[FILENAME_MAX];
    FILE *fp;
    int err = 0;

    sprintf(tmpname, "%s/open-%ld", gretl_dotdir(), (long) getpid());
    fp = fopen(tmpname, "w");

    if (fp != NULL) {
	union sigval data;

	fprintf(fp, "%s\n", (*fname == '\0')? "none" : fname);
	fclose(fp);
	data.sival_ptr = (void *) 0xf0;
	err = sigqueue(gpid, SIGUSR1, data);
    } else {
	err = 1;
    }

    return err;
}

#elif defined(MAC_NATIVE)

int get_instance_count (long **ppid)
{
    int nproc = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    size_t psize;
    pid_t *pids;
    int i, count = 0;
    
    psize = nproc * sizeof *pids;
    pids = malloc(psize);
    if (pids == NULL) {
	return -1;
    }

    for (i=0; i<nproc; i++) {
	pids[i] = 0;
    }
    
    proc_listpids(PROC_ALL_PIDS, 0, pids, psize);

    for (i=0; i<nproc; i++) {
	if (pids[i] == 0) { 
	    continue; 
	}
	memset(buf, 0, sizeof buf);
	proc_pidpath(pids[i], buf, sizeof buf);
	if (*buf != '\0') {
	    char *s = strrchr(buf, '/');
	    
	    if (s != NULL) {
	        count += !strcmp("gretl", s + 1);
            } else {
	        count += !strcmp("gretl", buf);
            }
	} 
    }

    free(pids);
    
    return count;
}

#elif defined WIN32

int get_instance_count (long *ppid) 
{
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    long mypid;
    int count = 0;

    mypid = (long) GetCurrentProcessId();

    if (hsnap) {
        PROCESSENTRY32 pe;
	int match = 0;
	char *s;

        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hsnap, &pe)) {
            do {
		s = strrchr(pe.szExeFile, '\\');
		if (s != NULL) {
		    match = !strcmp(s + 1, "gretlw32.exe");
		} else {
		    match = !strcmp(pe.szExeFile, "gretlw32.exe");
		}
		if (match) {
		    if (ppid != NULL && pe.th32ProcessID != mypid && *ppid <= 0) {
			*ppid = pe.th32ProcessID;
		    }
		    count++;
		}		    
            } while (Process32Next(hsnap, &pe));
	}
	CloseHandle(hsnap);
    }

    return count;
}

static HWND get_hwnd_for_pid (long gpid)
{
    HWND hw = GetTopWindow(0);

    while (hw) {
	DWORD pid;
	
	GetWindowThreadProcessId(hw, &pid);
	if (pid == gpid) {
	    break;
	}
	hw = GetNextWindow(hw, GW_HWNDNEXT);
    }

    fprintf(stderr, "get_hwnd_for_pid: gpid=%d -> hw=%p\n",
	    gpid, (void *) hw);

    return hw;
}

#include <gdk/gdkwin32.h>

static gboolean win32_peek_message (gpointer data)
{
    static HWND hw;
    MSG msg;

    if (!hw) {
	GdkWindow *w = gtk_widget_get_window(mdata->main);
	
	hw = GDK_WINDOW_HWND(w);
	fprintf(stderr, "peek_message: my hw = %p\n", (void *) hw);
    }
 
    if (hw && PeekMessage(&msg, hw, WM_APP, WM_APP, PM_REMOVE)) {
	int wp = msg.wParam;
	int try_open = 0;

	if (wp == 0xf0) {
	    long gotpid = msg.lParam;
	    char fname[FILENAME_MAX];
	    char path[FILENAME_MAX];
	    FILE *fp;

	    fprintf(stderr, "Got message 0xf0\n");

	    sprintf(fname, "%s/open-%ld", gretl_dotdir(), gotpid);
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

	gtk_window_present(GTK_WINDOW(mdata->main));

	if (try_open) {
	    real_open_tryfile();
	}
    }

    return TRUE;
}

int install_open_handler (void)
{
    g_idle_add(win32_peek_message, NULL);
    return 0;
}

int try_forwarding_open_request (long gpid, const char *fname)
{
    HWND hw = get_hwnd_for_pid(gpid);
    int err = 0;

    fprintf(stderr, "try_forwarding: gpid=%ld, fname='%s'\n", 
	    gpid, fname);

    if (!hw) {
	fprintf(stderr, "Couldn't find HWND\n");
	err = 1;
    } else {
	long mypid = (long) GetCurrentProcessId();
	char tmpname[FILENAME_MAX];
	FILE *fp;

	sprintf(tmpname, "%s\\open-%ld", gretl_dotdir(), mypid);
	fp = fopen(tmpname, "w");
	
	if (fp != NULL) {
	    fprintf(fp, "%s\n", (*fname == '\0')? "none" : fname);
	    fclose(fp);
	    fprintf(stderr, "Calling SendMessage\n");
	    SendMessage(hw, WM_APP, (WPARAM) 0xf0, 
			(LPARAM) mypid);
	} else {
	    fprintf(stderr, "Couldn't write '%s'\n", tmpname);
	    err = 1;
	}
    }

    return err;
}

#else /* none of the above */

int get_instance_count (long *ppid) 
{
    return 0;
}

#endif /* OS variations */
