//Libraries listed alphabetically
#include <assert.h>
#include <ctype.h> /* isspace */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h> /* open */
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <limits.h> /* UINT_MAX */
#include <locale.h> /* setlocale */
#include <signal.h>
#include <stdarg.h> /* va_list */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h> /* strdup */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h> /* gettimeofday */
#include <sys/types.h> /* off_t */
#include <sys/utsname.h> /* uname */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <unistd.h>
#include <wchar.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h> * tcflush */
#endif

#include <alpm.h>
#include <alpm_list.h>

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Callback.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////

/* download progress bar */
static int total_enabled = 0;
static off_t list_total = 0.0;
static size_t list_total_pkgs = 0;
static struct pacman_progress_bar *totalbar;

/* delayed output during progress bar */
static int on_progress = 0;
static alpm_list_t *output = NULL;

/* update speed for the fill_progress based functions */
#define UPDATE_SPEED_MS 200

#if !defined(CLOCK_MONOTONIC_COARSE) && defined(CLOCK_MONOTONIC)
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif

struct pacman_progress_bar {
	const char *filename;
	off_t xfered; /* Current amount of transferred data */
	off_t total_size;
	size_t downloaded;
	size_t howmany;
	uint64_t init_time; /* Time when this download started doing any progress */
	uint64_t sync_time; /* Last time we updated the bar info */
	off_t sync_xfered; /* Amount of transferred data at the `sync_time` timestamp. It can be
	                      smaller than `xfered` if we did not update bar UI for a while. */
	double rate;
	unsigned int eta; /* ETA in seconds */
	bool completed; /* transfer is completed */
};

/* This datastruct represents the state of multiline progressbar UI */
struct pacman_multibar_ui {
	/* List of active downloads handled by multibar UI.
	 * Once the first download in the list is completed it is removed
	 * from this list and we never redraw it anymore.
	 * If the download is in this list, then the UI can redraw the progress bar or change
	 * the order of the bars (e.g. moving completed bars to the top of the list)
	 */
	alpm_list_t *active_downloads; /* List of type 'struct pacman_progress_bar' */

	/* Number of active download bars that multibar UI handles. */
	size_t active_downloads_num;

	/* Specifies whether a completed progress bar need to be reordered and moved
	 * to the top of the list.
	 */
	bool move_completed_up;

	/* Cursor position relative to the first active progress bar,
	 * e.g. 0 means the first active progress bar, active_downloads_num-1 means the last bar,
	 * active_downloads_num - is the line below all progress bars.
	 */
	int cursor_lineno;
};

struct pacman_multibar_ui multibar_ui = {0};

static int dload_progressbar_enabled(void);
static void init_total_progressbar(void);
static void update_bar_finalstats(struct pacman_progress_bar *bar);
static void draw_pacman_progress_bar(struct pacman_progress_bar *bar);

void multibar_move_completed_up(bool value) {
	multibar_ui.move_completed_up = value;
}

static int64_t get_time_ms(void)
{
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0) && defined(CLOCK_MONOTONIC_COARSE)
	struct timespec ts = {0, 0};
	clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
	return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#else
	/* darwin doesn't support clock_gettime, fallback to gettimeofday */
	struct timeval tv = {0, 0};
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

/**
 * Silly little helper function, determines if the caller needs a visual update
 * since the last time this function was called.
 * This is made for the two progress bar functions, to prevent flicker.
 * @param first_call 1 on first call for initialization purposes, 0 otherwise
 * @return number of milliseconds since last call
 */
static int64_t get_update_timediff(int first_call)
{
	int64_t retval = 0;
	static int64_t last_time = 0;

	/* on first call, simply set the last time and return */
	if(first_call) {
		last_time = get_time_ms();
	} else {
		int64_t this_time = get_time_ms();
		retval = this_time - last_time;

		/* do not update last_time if interval was too short */
		if(retval < 0 || retval >= UPDATE_SPEED_MS) {
			last_time = this_time;
		}
	}

	return retval;
}

/* refactored from cb_trans_progress */
static void fill_progress(const int bar_percent, const int disp_percent,
		const int proglen)
{
	/* 8 = 1 space + 1 [ + 1 ] + 5 for percent */
	const int hashlen = proglen > 8 ? proglen - 8 : 0;
	const int hash = bar_percent * hashlen / 100;
	static int lasthash = 0, mouth = 0;
	int i;

	if(bar_percent == 0) {
		lasthash = 0;
		mouth = 0;
	}

	if(hashlen > 0) {
		fputs(" [", stdout);
		for(i = hashlen; i > 0; --i) {
			/* if special progress bar enabled */
			if(config->chomp) {
				if(i > hashlen - hash) {
					putchar('-');
				} else if(i == hashlen - hash) {
					if(lasthash == hash) {
						if(mouth) {
							fputs("\033[1;33mC\033[m", stdout);
						} else {
							fputs("\033[1;33mc\033[m", stdout);
						}
					} else {
						lasthash = hash;
						mouth = mouth == 1 ? 0 : 1;
						if(mouth) {
							fputs("\033[1;33mC\033[m", stdout);
						} else {
							fputs("\033[1;33mc\033[m", stdout);
						}
					}
				} else if(i % 3 == 0) {
					fputs("\033[0;37mo\033[m", stdout);
				} else {
					fputs("\033[0;37m \033[m", stdout);
				}
			} /* else regular progress bar */
			else if(i > hashlen - hash) {
				putchar('#');
			} else {
				putchar('-');
			}
		}
		putchar(']');
	}
	/* print display percent after progress bar */
	/* 5 = 1 space + 3 digits + 1 % */
	if(proglen >= 5) {
		printf(" %3d%%", disp_percent);
	}

	putchar('\r');
	fflush(stdout);
}

static void flush_output_list(void) {
	alpm_list_t *i = NULL;
	fflush(stdout);
	for(i = output; i; i = i->next) {
		fputs((const char *)i->data, stderr);
	}
	fflush(stderr);
	FREELIST(output);
}

static int number_length(size_t n)
{
	int digits = 1;
	while((n /= 10)) {
		++digits;
	}

	return digits;
}

/* callback to handle messages/notifications from libalpm transactions */
void cb_event(void *ctx, alpm_event_t *event)
{
	(void)ctx;
	if(config->print) {
		console_cursor_move_end();
		return;
	}
	switch(event->type) {
		case ALPM_EVENT_HOOK_START:
			if(event->hook.when == ALPM_HOOK_PRE_TRANSACTION) {
				colon_printf(_("Running pre-transaction hooks...\n"));
			} else {
				colon_printf(_("Running post-transaction hooks...\n"));
			}
			break;
		case ALPM_EVENT_HOOK_RUN_START:
			{
				alpm_event_hook_run_t *e = &event->hook_run;
				int digits = number_length(e->total);
				printf("(%*zu/%*zu) %s\n", digits, e->position,
						digits, e->total, 
						e->desc ? e->desc : e->name);
			}
			break;
		case ALPM_EVENT_CHECKDEPS_START:
			printf(_("checking dependencies...\n"));
			break;
		case ALPM_EVENT_FILECONFLICTS_START:
			if(config->noprogressbar) {
				printf(_("checking for file conflicts...\n"));
			}
			break;
		case ALPM_EVENT_RESOLVEDEPS_START:
			printf(_("resolving dependencies...\n"));
			break;
		case ALPM_EVENT_INTERCONFLICTS_START:
			printf(_("looking for conflicting packages...\n"));
			break;
		case ALPM_EVENT_TRANSACTION_START:
			colon_printf(_("Processing package changes...\n"));
			break;
		case ALPM_EVENT_PACKAGE_OPERATION_START:
			if(config->noprogressbar) {
				alpm_event_package_operation_t *e = &event->package_operation;
				switch(e->operation) {
					case ALPM_PACKAGE_INSTALL:
						printf(_("installing %s...\n"), alpm_pkg_get_name(e->newpkg));
						break;
					case ALPM_PACKAGE_UPGRADE:
						printf(_("upgrading %s...\n"), alpm_pkg_get_name(e->newpkg));
						break;
					case ALPM_PACKAGE_REINSTALL:
						printf(_("reinstalling %s...\n"), alpm_pkg_get_name(e->newpkg));
						break;
					case ALPM_PACKAGE_DOWNGRADE:
						printf(_("downgrading %s...\n"), alpm_pkg_get_name(e->newpkg));
						break;
					case ALPM_PACKAGE_REMOVE:
						printf(_("removing %s...\n"), alpm_pkg_get_name(e->oldpkg));
						break;
				}
			}
			break;
		case ALPM_EVENT_PACKAGE_OPERATION_DONE:
			{
				alpm_event_package_operation_t *e = &event->package_operation;
				switch(e->operation) {
					case ALPM_PACKAGE_INSTALL:
						display_optdepends(e->newpkg);
						break;
					case ALPM_PACKAGE_UPGRADE:
					case ALPM_PACKAGE_DOWNGRADE:
						display_new_optdepends(e->oldpkg, e->newpkg);
						break;
					case ALPM_PACKAGE_REINSTALL:
					case ALPM_PACKAGE_REMOVE:
						break;
				}
			}
			break;
		case ALPM_EVENT_INTEGRITY_START:
			if(config->noprogressbar) {
				printf(_("checking package integrity...\n"));
			}
			break;
		case ALPM_EVENT_KEYRING_START:
			if(config->noprogressbar) {
				printf(_("checking keyring...\n"));
			}
			break;
		case ALPM_EVENT_KEY_DOWNLOAD_START:
			printf(_("downloading required keys...\n"));
			break;
		case ALPM_EVENT_LOAD_START:
			if(config->noprogressbar) {
				printf(_("loading package files...\n"));
			}
			break;
		case ALPM_EVENT_SCRIPTLET_INFO:
			fputs(event->scriptlet_info.line, stdout);
			break;
		case ALPM_EVENT_DB_RETRIEVE_START:
			on_progress = 1;
			break;
		case ALPM_EVENT_PKG_RETRIEVE_START:
			colon_printf(_("Retrieving packages...\n"));
			on_progress = 1;
			list_total_pkgs = event->pkg_retrieve.num;
			list_total = event->pkg_retrieve.total_size;
			total_enabled = list_total && list_total_pkgs > 1 && dload_progressbar_enabled();

			if(total_enabled) {
				init_total_progressbar();
			}
			break;
		case ALPM_EVENT_DISKSPACE_START:
			if(config->noprogressbar) {
				printf(_("checking available disk space...\n"));
			}
			break;
		case ALPM_EVENT_OPTDEP_REMOVAL:
			{
				alpm_event_optdep_removal_t *e = &event->optdep_removal;
				char *dep_string = alpm_dep_compute_string(e->optdep);
				colon_printf(_("%s optionally requires %s\n"),
						alpm_pkg_get_name(e->pkg),
						dep_string);
				free(dep_string);
			}
			break;
		case ALPM_EVENT_DATABASE_MISSING:
			if(!config->op_s_sync) {
				pm_printf(ALPM_LOG_WARNING,
						"database file for '%s' does not exist (use '%s' to download)\n",
						event->database_missing.dbname,
						config->op == PM_OP_FILES ? "-Fy": "-Sy");
			}
			break;
		case ALPM_EVENT_PACNEW_CREATED:
			{
				alpm_event_pacnew_created_t *e = &event->pacnew_created;
				if(on_progress) {
					char *string = NULL;
					pm_sprintf(&string, ALPM_LOG_WARNING, _("%s installed as %s.pacnew\n"),
							e->file, e->file);
					if(string != NULL) {
						output = alpm_list_add(output, string);
					}
				} else {
					pm_printf(ALPM_LOG_WARNING, _("%s installed as %s.pacnew\n"),
							e->file, e->file);
				}
			}
			break;
		case ALPM_EVENT_PACSAVE_CREATED:
			{
				alpm_event_pacsave_created_t *e = &event->pacsave_created;
				if(on_progress) {
					char *string = NULL;
					pm_sprintf(&string, ALPM_LOG_WARNING, _("%s saved as %s.pacsave\n"),
							e->file, e->file);
					if(string != NULL) {
						output = alpm_list_add(output, string);
					}
				} else {
					pm_printf(ALPM_LOG_WARNING, _("%s saved as %s.pacsave\n"),
							e->file, e->file);
				}
			}
			break;
		case ALPM_EVENT_DB_RETRIEVE_DONE:
		case ALPM_EVENT_DB_RETRIEVE_FAILED:
		case ALPM_EVENT_PKG_RETRIEVE_DONE:
		case ALPM_EVENT_PKG_RETRIEVE_FAILED:
			console_cursor_move_end();
			if(total_enabled) {
				update_bar_finalstats(totalbar);
				draw_pacman_progress_bar(totalbar);
				free(totalbar);
				printf("\n");
			}
			total_enabled = 0;
			flush_output_list();
			on_progress = 0;
			break;
		/* all the simple done events, with fallthrough for each */
		case ALPM_EVENT_FILECONFLICTS_DONE:
		case ALPM_EVENT_CHECKDEPS_DONE:
		case ALPM_EVENT_RESOLVEDEPS_DONE:
		case ALPM_EVENT_INTERCONFLICTS_DONE:
		case ALPM_EVENT_TRANSACTION_DONE:
		case ALPM_EVENT_INTEGRITY_DONE:
		case ALPM_EVENT_KEYRING_DONE:
		case ALPM_EVENT_KEY_DOWNLOAD_DONE:
		case ALPM_EVENT_LOAD_DONE:
		case ALPM_EVENT_DISKSPACE_DONE:
		case ALPM_EVENT_HOOK_DONE:
		case ALPM_EVENT_HOOK_RUN_DONE:
			/* nothing */
			break;
	}
	fflush(stdout);
}

/* callback to handle questions from libalpm transactions (yes/no) */
void cb_question(void *ctx, alpm_question_t *question)
{
	(void)ctx;
	if(config->print) {
		switch(question->type) {
			case ALPM_QUESTION_INSTALL_IGNOREPKG:
			case ALPM_QUESTION_REPLACE_PKG:
				question->any.answer = 1;
				break;
			default:
				question->any.answer = 0;
				break;
		}
		return;
	}
	switch(question->type) {
		case ALPM_QUESTION_INSTALL_IGNOREPKG:
			{
				alpm_question_install_ignorepkg_t *q = &question->install_ignorepkg;
				if(!config->op_s_downloadonly) {
					q->install = yesno(_("%s is in IgnorePkg/IgnoreGroup. Install anyway?"),
							alpm_pkg_get_name(q->pkg));
				} else {
					q->install = 1;
				}
			}
			break;
		case ALPM_QUESTION_REPLACE_PKG:
			{
				alpm_question_replace_t *q = &question->replace;
				q->replace = yesno(_("Replace %s with %s/%s?"),
						alpm_pkg_get_name(q->oldpkg),
						alpm_db_get_name(q->newdb),
						alpm_pkg_get_name(q->newpkg));
			}
			break;
		case ALPM_QUESTION_CONFLICT_PKG:
			{
				alpm_question_conflict_t *q = &question->conflict;
				/* print conflict only if it contains new information */
				if(strcmp(q->conflict->package1, q->conflict->reason->name) == 0
						|| strcmp(q->conflict->package2, q->conflict->reason->name) == 0) {
					q->remove = noyes(_("%s and %s are in conflict. Remove %s?"),
							q->conflict->package1,
							q->conflict->package2,
							q->conflict->package2);
				} else {
					q->remove = noyes(_("%s and %s are in conflict (%s). Remove %s?"),
							q->conflict->package1,
							q->conflict->package2,
							q->conflict->reason->name,
							q->conflict->package2);
				}
			}
			break;
		case ALPM_QUESTION_REMOVE_PKGS:
			{
				alpm_question_remove_pkgs_t *q = &question->remove_pkgs;
				alpm_list_t *namelist = NULL, *i;
				size_t count = 0;
				for(i = q->packages; i; i = i->next) {
					namelist = alpm_list_add(namelist,
							(char *)alpm_pkg_get_name(i->data));
					count++;
				}
				colon_printf(_n(
							"The following package cannot be upgraded due to unresolvable dependencies:\n",
							"The following packages cannot be upgraded due to unresolvable dependencies:\n",
							count));
				list_display("     ", namelist, getcols());
				printf("\n");
				q->skip = noyes(_n(
							"Do you want to skip the above package for this upgrade?",
							"Do you want to skip the above packages for this upgrade?",
							count));
				alpm_list_free(namelist);
			}
			break;
		case ALPM_QUESTION_SELECT_PROVIDER:
			{
				alpm_question_select_provider_t *q = &question->select_provider;
				size_t count = alpm_list_count(q->providers);
				char *depstring = alpm_dep_compute_string(q->depend);
				colon_printf(_n("There is %zu provider available for %s\n",
						"There are %zu providers available for %s:\n", count),
						count, depstring);
				free(depstring);
				select_display(q->providers);
				q->use_index = select_question(count);
			}
			break;
		case ALPM_QUESTION_CORRUPTED_PKG:
			{
				alpm_question_corrupted_t *q = &question->corrupted;
				q->remove = yesno(_("File %s is corrupted (%s).\n"
							"Do you want to delete it?"),
						q->filepath,
						alpm_strerror(q->reason));
			}
			break;
		case ALPM_QUESTION_IMPORT_KEY:
			{
				alpm_question_import_key_t *q = &question->import_key;
				/* the uid is unknown with db signatures */
				if (q->uid == NULL) {
					q->import = yesno(_("Import PGP key %s?"),
							q->fingerprint);
				} else {
					q->import = yesno(_("Import PGP key %s, \"%s\"?"),
							q->fingerprint, q->uid);
				}
			}
			break;
	}
	if(config->noask) {
		if(config->ask & question->type) {
			/* inverse the default answer */
			question->any.answer = !question->any.answer;
		}
	}
}

/* callback to handle display of transaction progress */
void cb_progress(void *ctx, alpm_progress_t event, const char *pkgname,
		int percent, size_t howmany, size_t current)
{
	static int prevpercent;
	static size_t prevcurrent;
	/* size of line to allocate for text printing (e.g. not progressbar) */
	int infolen;
	int digits, textlen;
	char *opr = NULL;
	/* used for wide character width determination and printing */
	int len, wclen, wcwid, padwid;
	wchar_t *wcstr;

	const unsigned short cols = getcols();

	(void)ctx;

	if(config->noprogressbar || cols == 0) {
		return;
	}

	if(percent == 0) {
		get_update_timediff(1);
	} else if(percent == 100) {
		/* no need for timediff update, but unconditionally continue unless we
		 * already completed on a previous call */
		if(prevpercent == 100) {
			return;
		}
	} else {
		if(current != prevcurrent) {
			/* update always */
		} else if(!pkgname || percent == prevpercent ||
				get_update_timediff(0) < UPDATE_SPEED_MS) {
			/* only update the progress bar when we have a package name, the
			 * percentage has changed, and it has been long enough. */
			return;
		}
	}

	prevpercent = percent;
	prevcurrent = current;

	/* set text of message to display */
	switch(event) {
		case ALPM_PROGRESS_ADD_START:
			opr = _("installing");
			break;
		case ALPM_PROGRESS_UPGRADE_START:
			opr = _("upgrading");
			break;
		case ALPM_PROGRESS_DOWNGRADE_START:
			opr = _("downgrading");
			break;
		case ALPM_PROGRESS_REINSTALL_START:
			opr = _("reinstalling");
			break;
		case ALPM_PROGRESS_REMOVE_START:
			opr = _("removing");
			break;
		case ALPM_PROGRESS_CONFLICTS_START:
			opr = _("checking for file conflicts");
			break;
		case ALPM_PROGRESS_DISKSPACE_START:
			opr = _("checking available disk space");
			break;
		case ALPM_PROGRESS_INTEGRITY_START:
			opr = _("checking package integrity");
			break;
		case ALPM_PROGRESS_KEYRING_START:
			opr = _("checking keys in keyring");
			break;
		case ALPM_PROGRESS_LOAD_START:
			opr = _("loading package files");
			break;
		default:
			return;
	}

	infolen = cols * 6 / 10;
	if(infolen < 50) {
		infolen = 50;
	}

	/* find # of digits in package counts to scale output */
	digits = number_length(howmany);

	/* determine room left for non-digits text [not ( 1/12) part] */
	textlen = infolen - 3 /* (/) */ - (2 * digits) - 1 /* space */;

	/* In order to deal with characters from all locales, we have to worry
	 * about wide characters and their column widths. A lot of stuff is
	 * done here to figure out the actual number of screen columns used
	 * by the output, and then pad it accordingly so we fill the terminal.
	 */
	/* len = opr len + pkgname len (if available) + space + null */
	len = strlen(opr) + ((pkgname) ? strlen(pkgname) : 0) + 2;
	wcstr = calloc(len, sizeof(wchar_t));
	/* print our strings to the alloc'ed memory */
#if defined(HAVE_SWPRINTF)
	wclen = swprintf(wcstr, len, L"%s %s", opr, pkgname);
#else
	/* because the format string was simple, we can easily do this without
	 * using swprintf, although it is probably not as safe/fast. The max
	 * chars we can copy is decremented each time by subtracting the length
	 * of the already printed/copied wide char string. */
	wclen = mbstowcs(wcstr, opr, len);
	wclen += mbstowcs(wcstr + wclen, " ", len - wclen);
	wclen += mbstowcs(wcstr + wclen, pkgname, len - wclen);
#endif
	wcwid = wcswidth(wcstr, wclen);
	padwid = textlen - wcwid;
	/* if padwid is < 0, we need to trim the string so padwid = 0 */
	if(padwid < 0) {
		int i = textlen - 3;
		wchar_t *p = wcstr;
		/* grab the max number of char columns we can fill */
		while(i > wcwidth(*p)) {
			i -= wcwidth(*p);
			p++;
		}
		/* then add the ellipsis and fill out any extra padding */
		wcscpy(p, L"...");
		padwid = i;

	}

	printf("(%*zu/%*zu) %ls%-*s", digits, current,
			digits, howmany, wcstr, padwid, "");

	free(wcstr);

	/* call refactored fill progress function */
	fill_progress(percent, percent, cols - infolen);

	if(percent == 100) {
		putchar('\n');
		flush_output_list();
		on_progress = 0;
	} else {
		on_progress = 1;
	}
}

static int dload_progressbar_enabled(void)
{
	return !config->noprogressbar && (getcols() != 0);
}

/* Goto the line that corresponds to num-th active download */
static void console_cursor_goto_bar(int num)
{
	if(num > multibar_ui.cursor_lineno) {
		console_cursor_move_down(num - multibar_ui.cursor_lineno);
	} else if(num < multibar_ui.cursor_lineno) {
		console_cursor_move_up(multibar_ui.cursor_lineno - num);
	}
	multibar_ui.cursor_lineno = num;
}

/* Goto the line *after* the last active progress bar */
void console_cursor_move_end(void)
{
	console_cursor_goto_bar(multibar_ui.active_downloads_num);
}

/* Returns true if element with the specified name is found, false otherwise */
static bool find_bar_for_filename(const char *filename, int *index, struct pacman_progress_bar **bar)
{
	int i = 0;
	alpm_list_t *listitem = multibar_ui.active_downloads;
	for(; listitem; listitem = listitem->next, i++) {
		struct pacman_progress_bar *b = listitem->data;
		if (strcmp(b->filename, filename) == 0) {
			/* we found a progress bar with the given name */
			*index = i;
			*bar = b;
			return true;
		}
	}

	return false;
}

static void init_total_progressbar(void)
{
	totalbar = calloc(1, sizeof(struct pacman_progress_bar));
	assert(totalbar);
	totalbar->filename = _("Total");
	totalbar->init_time = get_time_ms();
	totalbar->total_size = list_total;
	totalbar->howmany = list_total_pkgs;
	totalbar->rate = 0.0;
}

static char *clean_filename(const char *filename)
{
	int len = strlen(filename);
	char *p;
	char *fname = malloc(len + 1);
	memcpy(fname, filename, len + 1);
	/* strip package or DB extension for cleaner look */
	if((p = strstr(fname, ".pkg")) || (p = strstr(fname, ".db")) || (p = strstr(fname, ".files"))) {
		len = p - fname;
		fname[len] = '\0';
	}

	return fname;
}

static void draw_pacman_progress_bar(struct pacman_progress_bar *bar)
{
	int infolen, len;
	int filenamelen;
	char *fname;
	/* used for wide character width determination and printing */
	int wclen, wcwid, padwid;
	wchar_t *wcfname;
	unsigned int eta_h = 0, eta_m = 0, eta_s = bar->eta;
	double rate_human, xfered_human;
	const char *rate_label, *xfered_label;
	int file_percent = 0;

	const unsigned short cols = getcols();

	if(bar->total_size) {
		file_percent = (bar->sync_xfered * 100) / bar->total_size;
	} else {
		file_percent = 100;
	}

	/* fix up time for display */
	eta_h = eta_s / 3600;
	eta_s -= eta_h * 3600;
	eta_m = eta_s / 60;
	eta_s -= eta_m * 60;

	fname = clean_filename(bar->filename);

	if(bar->howmany > 0) {
		short digits = number_length(bar->howmany);
		// fname + digits +  ( /) + \0
		size_t needed = strlen(fname) + (digits * 2) + 4 + 1;
		char *name = malloc(needed);
		sprintf(name, "%s (%*zu/%*zu)", fname, digits, bar->downloaded, digits, bar->howmany);
		free(fname);
		fname = name;
	}

	len = strlen(fname);
	infolen = cols * 6 / 10;
	if(infolen < 50) {
		infolen = 50;
	}

	/* 1 space + filenamelen + 1 space + 6 for size + 1 space + 3 for label +
	 * + 2 spaces + 4 for rate + 1 space + 3 for label + 2 for /s + 1 space +
	 * 5 for eta, gives us the magic 30 */
	filenamelen = infolen - 30;

	/* In order to deal with characters from all locales, we have to worry
	 * about wide characters and their column widths. A lot of stuff is
	 * done here to figure out the actual number of screen columns used
	 * by the output, and then pad it accordingly so we fill the terminal.
	 */
	/* len = filename len + null */
	wcfname = calloc(len + 1, sizeof(wchar_t));
	wclen = mbstowcs(wcfname, fname, len);
	wcwid = wcswidth(wcfname, wclen);
	padwid = filenamelen - wcwid;
	/* if padwid is < 0, we need to trim the string so padwid = 0 */
	if(padwid < 0) {
		int i = filenamelen - 3;
		wchar_t *wcp = wcfname;
		/* grab the max number of char columns we can fill */
		while(wcwidth(*wcp) < i) {
			i -= wcwidth(*wcp);
			wcp++;
		}
		/* then add the ellipsis and fill out any extra padding */
		wcscpy(wcp, L"...");
		padwid = i;

	}

	rate_human = humanize_size((off_t)bar->rate, '\0', -1, &rate_label);
	xfered_human = humanize_size(bar->sync_xfered, '\0', -1, &xfered_label);

	printf(" %ls%-*s ", wcfname, padwid, "");
	/* We will show 1.62 MiB/s, 11.6 MiB/s, but 116 KiB/s and 1116 KiB/s */
	if(rate_human < 9.995) {
		printf("%6.1f %3s  %4.2f %3s/s ",
				xfered_human, xfered_label, rate_human, rate_label);
	} else if(rate_human < 99.95) {
		printf("%6.1f %3s  %4.1f %3s/s ",
				xfered_human, xfered_label, rate_human, rate_label);
	} else {
		printf("%6.1f %3s  %4.f %3s/s ",
				xfered_human, xfered_label, rate_human, rate_label);
	}
	if(eta_h == 0) {
		printf("%02u:%02u", eta_m, eta_s);
	} else if(eta_h == 1 && eta_m < 40) {
		printf("%02u:%02u", eta_m + 60, eta_s);
	} else {
		fputs("--:--", stdout);
	}

	free(fname);
	free(wcfname);

	fill_progress(file_percent, file_percent, cols - infolen);
	return;
}

static void dload_init_event(const char *filename, alpm_download_event_init_t *data)
{
	(void)data;
	char *cleaned_filename = clean_filename(filename);

	if(!dload_progressbar_enabled()) {
		printf(_(" %s downloading...\n"), cleaned_filename);
		free(cleaned_filename);
		return;
	}

	struct pacman_progress_bar *bar = calloc(1, sizeof(struct pacman_progress_bar));
	assert(bar);
	bar->filename = filename;
	bar->init_time = get_time_ms();
	bar->rate = 0.0;
	multibar_ui.active_downloads = alpm_list_add(multibar_ui.active_downloads, bar);

	console_cursor_move_end();
	printf(" %s\n", cleaned_filename);
	multibar_ui.cursor_lineno++;
	multibar_ui.active_downloads_num++;

	if(total_enabled) {
		/* redraw the total download progress bar */
		draw_pacman_progress_bar(totalbar);
		printf("\n");
		multibar_ui.cursor_lineno++;
	}
}

/* Update progress bar rate/eta stats.
 * Returns true if the bar redraw is required, false otherwise
 */
static bool update_bar_stats(struct pacman_progress_bar *bar)
{
	int64_t timediff;
	off_t last_chunk_amount;
	double last_chunk_rate;
	int64_t curr_time = get_time_ms();

	/* compute current average values */
	timediff = curr_time - bar->sync_time;

	if(timediff < UPDATE_SPEED_MS) {
		/* return if the calling interval was too short */
		return false;
	}
	last_chunk_amount = bar->xfered - bar->sync_xfered;
	bar->sync_xfered = bar->xfered;
	bar->sync_time = curr_time;

	last_chunk_rate = (double)last_chunk_amount * 1000 / timediff;
	/* average rate to reduce jumpiness */
	bar->rate = (last_chunk_rate + 2 * bar->rate) / 3;
	if(bar->rate > 0.0) {
		bar->eta = (bar->total_size - bar->sync_xfered) / bar->rate;
	} else {
		bar->eta = UINT_MAX;
	}

	return true;
}

static void update_bar_finalstats(struct pacman_progress_bar *bar)
{
	int64_t timediff;

	/* compute final values */
	bar->xfered = bar->total_size;
	bar->sync_xfered = bar->total_size;
	timediff = get_time_ms() - bar->init_time;

	/* if transfer was too fast, treat it as a 1ms transfer, for the sake
	* of the rate calculation */
	if(timediff < 1)
		timediff = 1;

	bar->rate = (double)bar->total_size * 1000 / timediff;
	/* round elapsed time (in ms) to the nearest second */
	bar->eta = (unsigned int)(timediff + 500) / 1000;
}

/* Handles download progress event */
static void dload_progress_event(const char *filename, alpm_download_event_progress_t *data)
{
	int index;
	struct pacman_progress_bar *bar;
	bool ok;
	off_t last_chunk_amount;

	if(!dload_progressbar_enabled()) {
		return;
	}

	ok = find_bar_for_filename(filename, &index, &bar);
	assert(ok);

	/* Total size is received after the download starts. */
	last_chunk_amount = data->downloaded - bar->xfered;
	bar->xfered = data->downloaded;
	bar->total_size = data->total;

	if(update_bar_stats(bar)) {
		console_cursor_goto_bar(index);
		draw_pacman_progress_bar(bar);
	}

	if(total_enabled) {
		totalbar->xfered += last_chunk_amount;
		if(update_bar_stats(totalbar)) {
			console_cursor_move_end();
			draw_pacman_progress_bar(totalbar);
		}
	}

	fflush(stdout);
}

/* download retried */
static void dload_retry_event(const char *filename, alpm_download_event_retry_t *data) {
	if(!dload_progressbar_enabled()) {
		return;
	}

	int index;
	struct pacman_progress_bar *bar;
	bool ok = find_bar_for_filename(filename, &index, &bar);
	assert(ok);

	if(!data->resume) {
		if(total_enabled) {
			/* note total download does not reflect partial downloads that are restarted */
			totalbar->xfered -= bar->xfered;
		}
	}

	bar->xfered = 0;
	bar->total_size = 0;
	bar->init_time = get_time_ms();
	bar->sync_time = 0;
	bar->sync_xfered = 0;
	bar->rate = 0.0;
	bar->eta = 0.0;
}


/* download completed */
static void dload_complete_event(const char *filename, alpm_download_event_completed_t *data)
{
	int index;
	struct pacman_progress_bar *bar;
	bool ok;

	if(!dload_progressbar_enabled()) {
		return;
	}

	if(total_enabled) {
		totalbar->downloaded++;
	}

	ok = find_bar_for_filename(filename, &index, &bar);
	assert(ok);
	bar->completed = true;

	/* This may not have been initialized if the download finished before
	 * an alpm_download_event_progress_t event happened */
	bar->total_size = data->total;

	if(data->result == 1) {
		console_cursor_goto_bar(index);
		char *cleaned_filename = clean_filename(filename);
		printf(_(" %s is up to date"), cleaned_filename);
		free(cleaned_filename);
		/* The line contains text from previous status. Erase these leftovers. */
		console_erase_line();
	} else if(data->result == 0) {
		update_bar_finalstats(bar);

		if(multibar_ui.move_completed_up && index != 0) {
			/* If this item completed then move it to the top.
			 * Swap 0-th bar data with `index`-th one
			 */
			struct pacman_progress_bar *former_topbar = multibar_ui.active_downloads->data;
			alpm_list_t *baritem = alpm_list_nth(multibar_ui.active_downloads, index);
			multibar_ui.active_downloads->data = bar;
			baritem->data = former_topbar;

			console_cursor_goto_bar(index);
			draw_pacman_progress_bar(former_topbar);

			index = 0;
		}

		console_cursor_goto_bar(index);
		draw_pacman_progress_bar(bar);
	} else {
		console_cursor_goto_bar(index);
		printf(_(" %s failed to download"), bar->filename);
		console_erase_line();
	}
	fflush(stdout);

	/* If the first bar is completed then there is no reason to keep it
	 * in the list as we are not going to redraw it anymore.
	 */
	while(multibar_ui.active_downloads) {
		alpm_list_t *head = multibar_ui.active_downloads;
		struct pacman_progress_bar *j = head->data;
		if(j->completed) {
			multibar_ui.cursor_lineno--;
			multibar_ui.active_downloads_num--;
			multibar_ui.active_downloads = alpm_list_remove_item(
				multibar_ui.active_downloads, head);
			free(head);
			free(j);
		} else {
			break;
		}
	}
}

static int strendswith(const char *haystack, const char *needle)
{
	size_t hlen = strlen(haystack), nlen = strlen(needle);
	return hlen >= nlen && strcmp(haystack + hlen - nlen, needle) == 0;
}

/* Callback to handle display of download progress */
void cb_download(void *ctx, const char *filename, alpm_download_event_type_t event, void *data)
{
	(void)ctx;

	/* do not print signature files progress bar */
	if(strendswith(filename, ".sig")) {
		return;
	}

	if(event == ALPM_DOWNLOAD_INIT) {
		dload_init_event(filename, data);
	} else if(event == ALPM_DOWNLOAD_PROGRESS) {
		dload_progress_event(filename, data);
	} else if(event == ALPM_DOWNLOAD_RETRY) {
		dload_retry_event(filename, data);
	} else if(event == ALPM_DOWNLOAD_COMPLETED) {
		dload_complete_event(filename, data);
	} else {
		pm_printf(ALPM_LOG_ERROR, _("unknown callback event type %d for %s\n"),
				event, filename);
	}
}

/* Callback to handle notifications from the library */
void cb_log(void *ctx, alpm_loglevel_t level, const char *fmt, va_list args)
{
	(void)ctx;
	if(!fmt || strlen(fmt) == 0) {
		return;
	}

	if(on_progress) {
		char *string = NULL;
		pm_vasprintf(&string, level, fmt, args);
		if(string != NULL) {
			output = alpm_list_add(output, string);
		}
	} else {
		pm_vfprintf(stderr, level, fmt, args);
	}
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Check.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////

static int check_file_exists(const char *pkgname, char *filepath, size_t rootlen,
		struct stat *st)
{
	/* use lstat to prevent errors from symlinks */
	if(llstat(filepath, st) != 0) {
		if(alpm_option_match_noextract(config->handle, filepath + rootlen) == 0) {
			/* NoExtract */
			return -1;
		} else {
			if(config->quiet) {
				printf("%s %s\n", pkgname, filepath);
			} else {
				pm_printf(ALPM_LOG_WARNING, "%s: %s (%s)\n",
						pkgname, filepath, strerror(errno));
			}
			return 1;
		}
	}

	return 0;
}

static int check_file_type(const char *pkgname, const char *filepath,
		struct stat *st, struct archive_entry *entry)
{
	mode_t archive_type = archive_entry_filetype(entry);
	mode_t file_type = st->st_mode;

	if((archive_type == AE_IFREG && !S_ISREG(file_type)) ||
			(archive_type == AE_IFDIR && !S_ISDIR(file_type)) ||
			(archive_type == AE_IFLNK && !S_ISLNK(file_type))) {
		if(config->quiet) {
			printf("%s %s\n", pkgname, filepath);
		} else {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (File type mismatch)\n"),
					pkgname, filepath);
		}
		return 1;
	}

	return 0;
}

static int check_file_permissions(const char *pkgname, const char *filepath,
		struct stat *st, struct archive_entry *entry)
{
	int errors = 0;
	mode_t fsmode;

	/* uid */
	if(st->st_uid != archive_entry_uid(entry)) {
		errors++;
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (UID mismatch)\n"),
					pkgname, filepath);
		}
	}

	/* gid */
	if(st->st_gid != archive_entry_gid(entry)) {
		errors++;
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (GID mismatch)\n"),
					pkgname, filepath);
		}
	}

	/* mode */
	fsmode = st->st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);
	if(fsmode != (~AE_IFMT & archive_entry_mode(entry))) {
		errors++;
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (Permissions mismatch)\n"),
					pkgname, filepath);
		}
	}

	return (errors != 0 ? 1 : 0);
}

static int check_file_time(const char *pkgname, const char *filepath,
		struct stat *st, struct archive_entry *entry, int backup)
{
	if(st->st_mtime != archive_entry_mtime(entry)) {
		if(backup) {
			if(!config->quiet) {
				printf("%s%s%s: ", config->colstr.title, _("backup file"),
						config->colstr.nocolor);
				printf(_("%s: %s (Modification time mismatch)\n"),
						pkgname, filepath);
			}
			return 0;
		}
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (Modification time mismatch)\n"),
					pkgname, filepath);
		}
		return 1;
	}

	return 0;
}

static int check_file_link(const char *pkgname, const char *filepath,
		struct stat *st, struct archive_entry *entry)
{
	size_t length = st->st_size + 1;
	char link[length];

	if(readlink(filepath, link, length) != st->st_size) {
		/* this should not happen */
		pm_printf(ALPM_LOG_ERROR, _("unable to read symlink contents: %s\n"), filepath);
		return 1;
	}
	link[length - 1] = '\0';

	if(strcmp(link, archive_entry_symlink(entry)) != 0) {
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (Symlink path mismatch)\n"),
					pkgname, filepath);
		}
		return 1;
	}

	return 0;
}

static int check_file_size(const char *pkgname, const char *filepath,
		struct stat *st, struct archive_entry *entry, int backup)
{
	if(st->st_size != archive_entry_size(entry)) {
		if(backup) {
			if(!config->quiet) {
				printf("%s%s%s: ", config->colstr.title, _("backup file"),
						config->colstr.nocolor);
				printf(_("%s: %s (Size mismatch)\n"),
						pkgname, filepath);
			}
			return 0;
		}
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (Size mismatch)\n"),
					pkgname, filepath);
		}
		return 1;
	}

	return 0;
}

#if ARCHIVE_VERSION_NUMBER >= 3005000
static int check_file_cksum(const char *pkgname, const char *filepath,
		int backup, const char *cksum_name, const char *cksum_calc, const char *cksum_mtree)
{
	if(!cksum_calc) {
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (failed to calculate %s checksum)\n"),
					pkgname, filepath, cksum_name);
		}
		return 1;
	}

	if(!cksum_mtree) {
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (%s checksum information not available)\n"),
					pkgname, filepath, cksum_name);
		}
		return 1;
	}

	if(strcmp(cksum_calc, cksum_mtree) != 0) {
		if(backup) {
			if(!config->quiet) {
				printf("%s%s%s: ", config->colstr.title, _("backup file"),
						config->colstr.nocolor);
				printf(_("%s: %s (%s checksum mismatch)\n"),
						pkgname, filepath, cksum_name);
			}
			return 0;
		}
		if(!config->quiet) {
			pm_printf(ALPM_LOG_WARNING, _("%s: %s (%s checksum mismatch)\n"),
					pkgname, filepath, cksum_name);
		}
		return 1;
	}

	return 0;
}
#endif

static int check_file_md5sum(const char *pkgname, const char *filepath,
		struct archive_entry *entry, int backup)
{
	int errors = 0;
#if ARCHIVE_VERSION_NUMBER >= 3005000
	char *cksum_calc = alpm_compute_md5sum(filepath);
	char *cksum_mtree = hex_representation(archive_entry_digest(entry,
													ARCHIVE_ENTRY_DIGEST_MD5), 16);
	errors = check_file_cksum(pkgname, filepath, backup, "MD5", cksum_calc,
									cksum_mtree);
	free(cksum_mtree);
	free(cksum_calc);
#endif
	return (errors != 0 ? 1 : 0);
}

static int check_file_sha256sum(const char *pkgname, const char *filepath,
		struct archive_entry *entry, int backup)
{
	int errors = 0;
#if ARCHIVE_VERSION_NUMBER >= 3005000
	char *cksum_calc = alpm_compute_sha256sum(filepath);
	char *cksum_mtree = hex_representation(archive_entry_digest(entry,
													ARCHIVE_ENTRY_DIGEST_SHA256), 32);
	errors = check_file_cksum(pkgname, filepath, backup, "SHA256", cksum_calc,
									cksum_mtree);
	free(cksum_mtree);
	free(cksum_calc);
#endif
	return (errors != 0 ? 1 : 0);
}

/* Loop through the files of the package to check if they exist. */
int check_pkg_fast(alpm_pkg_t *pkg)
{
	const char *root, *pkgname;
	size_t errors = 0;
	size_t rootlen;
	char filepath[PATH_MAX];
	alpm_filelist_t *filelist;
	size_t i;

	root = alpm_option_get_root(config->handle);
	rootlen = strlen(root);
	if(rootlen + 1 > PATH_MAX) {
		/* we are in trouble here */
		pm_printf(ALPM_LOG_ERROR, _("path too long: %s%s\n"), root, "");
		return 1;
	}
	strcpy(filepath, root);

	pkgname = alpm_pkg_get_name(pkg);
	filelist = alpm_pkg_get_files(pkg);
	for(i = 0; i < filelist->count; i++) {
		const alpm_file_t *file = filelist->files + i;
		struct stat st;
		int exists;
		const char *path = file->name;
		size_t plen = strlen(path);

		if(rootlen + 1 + plen > PATH_MAX) {
			pm_printf(ALPM_LOG_WARNING, _("path too long: %s%s\n"), root, path);
			continue;
		}
		strcpy(filepath + rootlen, path);

		exists = check_file_exists(pkgname, filepath, rootlen, &st);
		if(exists == 0) {
			int expect_dir = path[plen - 1] == '/' ? 1 : 0;
			int is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
			if(expect_dir != is_dir) {
				pm_printf(ALPM_LOG_WARNING, _("%s: %s (File type mismatch)\n"),
						pkgname, filepath);
				++errors;
			}
		} else if(exists == 1) {
			++errors;
		}
	}

	if(!config->quiet) {
		printf(_n("%s: %jd total file, ", "%s: %jd total files, ",
					(unsigned long)filelist->count), pkgname, (intmax_t)filelist->count);
		printf(_n("%jd missing file\n", "%jd missing files\n",
					(unsigned long)errors), (intmax_t)errors);
	}

	return (errors != 0 ? 1 : 0);
}

/* Loop though files in a package and perform full file property checking. */
int check_pkg_full(alpm_pkg_t *pkg)
{
	const char *root, *pkgname;
	size_t errors = 0;
	size_t rootlen;
	struct archive *mtree;
	struct archive_entry *entry = NULL;
	size_t file_count = 0;
	const alpm_list_t *lp;

	root = alpm_option_get_root(config->handle);
	rootlen = strlen(root);
	if(rootlen + 1 > PATH_MAX) {
		/* we are in trouble here */
		pm_printf(ALPM_LOG_ERROR, _("path too long: %s%s\n"), root, "");
		return 1;
	}

	pkgname = alpm_pkg_get_name(pkg);
	mtree = alpm_pkg_mtree_open(pkg);
	if(mtree == NULL) {
		/* TODO: check error to confirm failure due to no mtree file */
		if(!config->quiet) {
			printf(_("%s: no mtree file\n"), pkgname);
		}
		return 0;
	}

	while(alpm_pkg_mtree_next(pkg, mtree, &entry) == 0) {
		struct stat st;
		const char *path = archive_entry_pathname(entry);
		char filepath[PATH_MAX];
		int filepath_len;
		mode_t type;
		size_t file_errors = 0;
		int backup = 0;
		int exists;

		/* strip leading "./" from path entries */
		if(path[0] == '.' && path[1] == '/') {
			path += 2;
		}

		if(*path == '.') {
			const char *dbfile = NULL;

			if(strcmp(path, ".INSTALL") == 0) {
				dbfile = "install";
			} else if(strcmp(path, ".CHANGELOG") == 0) {
				dbfile = "changelog";
			} else {
				continue;
			}

			/* Do not append root directory as alpm_option_get_dbpath is already
			 * an absoute path */
			filepath_len = snprintf(filepath, PATH_MAX, "%slocal/%s-%s/%s",
					alpm_option_get_dbpath(config->handle),
					pkgname, alpm_pkg_get_version(pkg), dbfile);
			if(filepath_len >= PATH_MAX) {
				pm_printf(ALPM_LOG_WARNING, _("path too long: %slocal/%s-%s/%s\n"),
						alpm_option_get_dbpath(config->handle),
						pkgname, alpm_pkg_get_version(pkg), dbfile);
				continue;
			}
		} else {
			filepath_len = snprintf(filepath, PATH_MAX, "%s%s", root, path);
			if(filepath_len >= PATH_MAX) {
				pm_printf(ALPM_LOG_WARNING, _("path too long: %s%s\n"), root, path);
				continue;
			}
		}

		file_count++;

		exists = check_file_exists(pkgname, filepath, rootlen, &st);
		if(exists == 1) {
			errors++;
			continue;
		} else if(exists == -1) {
			/* NoExtract */
			continue;
		}

		type = archive_entry_filetype(entry);

		if(type != AE_IFDIR && type != AE_IFREG && type != AE_IFLNK) {
			pm_printf(ALPM_LOG_WARNING, _("file type not recognized: %s%s\n"), root, path);
			continue;
		}

		if(check_file_type(pkgname, filepath, &st, entry) == 1) {
			errors++;
			continue;
		}

		file_errors += check_file_permissions(pkgname, filepath, &st, entry);

		if(type == AE_IFLNK) {
			file_errors += check_file_link(pkgname, filepath, &st, entry);
		}

		/* the following checks are expected to fail if a backup file has been
		   modified */
		for(lp = alpm_pkg_get_backup(pkg); lp; lp = lp->next) {
			alpm_backup_t *bl = lp->data;

			if(strcmp(path, bl->name) == 0) {
				backup = 1;
				break;
			}
		}

		if(type != AE_IFDIR) {
			/* file or symbolic link */
			file_errors += check_file_time(pkgname, filepath, &st, entry, backup);
		}

		if(type == AE_IFREG) {
			file_errors += check_file_size(pkgname, filepath, &st, entry, backup);
			file_errors += check_file_md5sum(pkgname, filepath, entry, backup);
			file_errors += check_file_sha256sum(pkgname, filepath, entry, backup);
		}

		if(config->quiet && file_errors) {
			printf("%s %s\n", pkgname, filepath);
		}

		errors += (file_errors != 0 ? 1 : 0);
	}

	alpm_pkg_mtree_close(pkg, mtree);

	if(!config->quiet) {
		printf(_n("%s: %jd total file, ", "%s: %jd total files, ",
					(unsigned long)file_count), pkgname, (intmax_t)file_count);
		printf(_n("%jd altered file\n", "%jd altered files\n",
					(unsigned long)errors), (intmax_t)errors);
	}

	return (errors != 0 ? 1 : 0);
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Conf.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////

/* global config variable */
config_t *config = NULL;

#define NOCOLOR       "\033[0m"

#define BOLD          "\033[0;1m"

#define BLACK         "\033[0;30m"
#define RED           "\033[0;31m"
#define GREEN         "\033[0;32m"
#define YELLOW        "\033[0;33m"
#define BLUE          "\033[0;34m"
#define MAGENTA       "\033[0;35m"
#define CYAN          "\033[0;36m"
#define WHITE         "\033[0;37m"

#define BOLDBLACK     "\033[1;30m"
#define BOLDRED       "\033[1;31m"
#define BOLDGREEN     "\033[1;32m"
#define BOLDYELLOW    "\033[1;33m"
#define BOLDBLUE      "\033[1;34m"
#define BOLDMAGENTA   "\033[1;35m"
#define BOLDCYAN      "\033[1;36m"
#define BOLDWHITE     "\033[1;37m"
#define GREY46        "\033[38;5;243m"

void enable_colors(int colors)
{
	colstr_t *colstr = &config->colstr;

	if(colors == PM_COLOR_ON) {
		colstr->colon   = BOLDBLUE "::" BOLD " ";
		colstr->title   = BOLD;
		colstr->repo    = BOLDMAGENTA;
		colstr->version = BOLDGREEN;
		colstr->groups  = BOLDBLUE;
		colstr->meta    = BOLDCYAN;
		colstr->warn    = BOLDYELLOW;
		colstr->err     = BOLDRED;
		colstr->faint   = GREY46;
		colstr->nocolor = NOCOLOR;
	} else {
		colstr->colon   = ":: ";
		colstr->title   = "";
		colstr->repo    = "";
		colstr->version = "";
		colstr->groups  = "";
		colstr->meta    = "";
		colstr->warn    = "";
		colstr->err     = "";
		colstr->faint   = "";
		colstr->nocolor = "";
	}
}

config_t *config_new(void)
{
	config_t *newconfig = calloc(1, sizeof(config_t));
	if(!newconfig) {
		pm_printf(ALPM_LOG_ERROR,
				_n("malloc failure: could not allocate %zu byte\n",
				   "malloc failure: could not allocate %zu bytes\n", sizeof(config_t)),
				sizeof(config_t));
		return NULL;
	}
	/* defaults which may get overridden later */
	newconfig->op = PM_OP_MAIN;
	newconfig->logmask = ALPM_LOG_ERROR | ALPM_LOG_WARNING;
	newconfig->configfile = strdup(CONFFILE);
	if(alpm_capabilities() & ALPM_CAPABILITY_SIGNATURES) {
		newconfig->siglevel = ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL |
			ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;
		newconfig->localfilesiglevel = ALPM_SIG_USE_DEFAULT;
		newconfig->remotefilesiglevel = ALPM_SIG_USE_DEFAULT;
	}

	/* by default use 1 download stream */
	newconfig->parallel_downloads = 1;
	newconfig->colstr.colon   = ":: ";
	newconfig->colstr.title   = "";
	newconfig->colstr.repo    = "";
	newconfig->colstr.version = "";
	newconfig->colstr.groups  = "";
	newconfig->colstr.meta    = "";
	newconfig->colstr.warn    = "";
	newconfig->colstr.err     = "";
	newconfig->colstr.faint   = "";
	newconfig->colstr.nocolor = "";

	return newconfig;
}

int config_free(config_t *oldconfig)
{
	if(oldconfig == NULL) {
		return -1;
	}

	alpm_list_free(oldconfig->explicit_adds);
	alpm_list_free(oldconfig->explicit_removes);

	alpm_list_free_inner(config->repos, (alpm_list_fn_free) config_repo_free);
	alpm_list_free(config->repos);

	FREELIST(oldconfig->holdpkg);
	FREELIST(oldconfig->ignorepkg);
	FREELIST(oldconfig->ignoregrp);
	FREELIST(oldconfig->assumeinstalled);
	FREELIST(oldconfig->noupgrade);
	FREELIST(oldconfig->noextract);
	FREELIST(oldconfig->overwrite_files);
	free(oldconfig->configfile);
	free(oldconfig->rootdir);
	free(oldconfig->dbpath);
	free(oldconfig->logfile);
	free(oldconfig->gpgdir);
	FREELIST(oldconfig->hookdirs);
	FREELIST(oldconfig->cachedirs);
	free(oldconfig->xfercommand);
	free(oldconfig->print_format);
	FREELIST(oldconfig->architectures);
	wordsplit_free(oldconfig->xfercommand_argv);
	free(oldconfig);

	return 0;
}

void config_repo_free(config_repo_t *repo)
{
	if(repo == NULL) {
		return;
	}
	free(repo->name);
	FREELIST(repo->servers);
	free(repo);
}

/** Helper function for download_with_xfercommand() */
static char *get_filename(const char *url)
{
	char *filename = strrchr(url, '/');
	if(filename != NULL) {
		filename++;
	}
	return filename;
}

/** Helper function for download_with_xfercommand() */
static char *get_destfile(const char *path, const char *filename)
{
	char *destfile;
	/* len = localpath len + filename len + null */
	size_t len = strlen(path) + strlen(filename) + 1;
	destfile = calloc(len, sizeof(char));
	snprintf(destfile, len, "%s%s", path, filename);

	return destfile;
}

/** Helper function for download_with_xfercommand() */
static char *get_tempfile(const char *path, const char *filename)
{
	char *tempfile;
	/* len = localpath len + filename len + '.part' len + null */
	size_t len = strlen(path) + strlen(filename) + 6;
	tempfile = calloc(len, sizeof(char));
	snprintf(tempfile, len, "%s%s.part", path, filename);

	return tempfile;
}

/* system()/exec() hybrid function allowing exec()-style direct execution
 * of a command with the simplicity of system()
 * - not thread-safe
 * - errno may be set by fork(), pipe(), or execvp()
 */
static int systemvp(const char *file, char *const argv[])
{
	int pid, err = 0, ret = -1, err_fd[2];
	sigset_t oldblock;
	struct sigaction sa_ign = { .sa_handler = SIG_IGN }, oldint, oldquit;

	if(pipe(err_fd) != 0) {
		return -1;
	}

	sigaction(SIGINT, &sa_ign, &oldint);
	sigaction(SIGQUIT, &sa_ign, &oldquit);
	sigaddset(&sa_ign.sa_mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sa_ign.sa_mask, &oldblock);

	pid = fork();

	/* child */
	if(pid == 0) {
		close(err_fd[0]);
		fcntl(err_fd[1], F_SETFD, FD_CLOEXEC);

		/* restore signal handling for the child to inherit */
		sigaction(SIGINT, &oldint, NULL);
		sigaction(SIGQUIT, &oldquit, NULL);
		sigprocmask(SIG_SETMASK, &oldblock, NULL);

		execvp(file, argv);

		/* execvp failed, pass the error back to the parent */
		while(write(err_fd[1], &errno, sizeof(errno)) == -1 && errno == EINTR);
		_Exit(127);
	}

	/* parent */
	close(err_fd[1]);

	if(pid != -1)  {
		int wret;
		while((wret = waitpid(pid, &ret, 0)) == -1 && errno == EINTR);
		if(wret > 0) {
			while(read(err_fd[0], &err, sizeof(err)) == -1 && errno == EINTR);
		}
	} else {
		/* fork failed, make sure errno is preserved after cleanup */
		err = errno;
	}

	close(err_fd[0]);

	sigaction(SIGINT, &oldint, NULL);
	sigaction(SIGQUIT, &oldquit, NULL);
	sigprocmask(SIG_SETMASK, &oldblock, NULL);

	if(err) {
		errno = err;
		ret = -1;
	}

	return ret;
}

/** External fetch callback */
static int download_with_xfercommand(void *ctx, const char *url,
		const char *localpath, int force)
{
	int ret = 0, retval;
	int usepart = 0;
	int cwdfd = -1;
	struct stat st;
	char *destfile, *tempfile, *filename;
	const char **argv;
	size_t i;

	(void)ctx;

	if(!config->xfercommand_argv) {
		return -1;
	}

	filename = get_filename(url);
	if(!filename) {
		return -1;
	}
	destfile = get_destfile(localpath, filename);
	tempfile = get_tempfile(localpath, filename);

	if(force && stat(tempfile, &st) == 0) {
		unlink(tempfile);
	}
	if(force && stat(destfile, &st) == 0) {
		unlink(destfile);
	}

	if((argv = calloc(config->xfercommand_argc + 1, sizeof(char*))) == NULL) {
		size_t bytes = (config->xfercommand_argc + 1) * sizeof(char*);
		pm_printf(ALPM_LOG_ERROR,
				_n("malloc failure: could not allocate %zu byte\n",
				   "malloc failure: could not allocate %zu bytes\n",
					 bytes),
				bytes);
		goto cleanup;
	}

	for(i = 0; i <= config->xfercommand_argc; i++) {
		const char *val = config->xfercommand_argv[i];
		if(val && strcmp(val, "%o") == 0) {
			usepart = 1;
			val = tempfile;
		} else if(val && strcmp(val, "%u") == 0) {
			val = url;
		}
		argv[i] = val;
	}

	/* save the cwd so we can restore it later */
	do {
		cwdfd = open(".", O_RDONLY);
	} while(cwdfd == -1 && errno == EINTR);
	if(cwdfd < 0) {
		pm_printf(ALPM_LOG_ERROR, _("could not get current working directory\n"));
	}

	/* cwd to the download directory */
	if(chdir(localpath)) {
		pm_printf(ALPM_LOG_WARNING, _("could not chdir to download directory %s\n"), localpath);
		ret = -1;
		goto cleanup;
	}

	if(config->logmask & ALPM_LOG_DEBUG) {
		char *cmd = arg_to_string(config->xfercommand_argc, (char**)argv);
		if(cmd) {
			pm_printf(ALPM_LOG_DEBUG, "running command: %s\n", cmd);
			free(cmd);
		}
	}
	retval = systemvp(argv[0], (char**)argv);

	if(retval == -1) {
		pm_printf(ALPM_LOG_WARNING, _("running XferCommand: fork failed!\n"));
		ret = -1;
	} else if(retval != 0) {
		/* download failed */
		pm_printf(ALPM_LOG_DEBUG, "XferCommand command returned non-zero status "
				"code (%d)\n", retval);
		ret = -1;
	} else {
		/* download was successful */
		ret = 0;
		if(usepart) {
			if(rename(tempfile, destfile)) {
				pm_printf(ALPM_LOG_ERROR, _("could not rename %s to %s (%s)\n"),
						tempfile, destfile, strerror(errno));
				ret = -1;
			}
		}
	}

cleanup:
	/* restore the old cwd if we have it */
	if(cwdfd >= 0) {
		if(fchdir(cwdfd) != 0) {
			pm_printf(ALPM_LOG_ERROR, _("could not restore working directory (%s)\n"),
					strerror(errno));
		}
		close(cwdfd);
	}

	if(ret == -1) {
		/* hack to let an user the time to cancel a download */
		sleep(2);
	}
	free(destfile);
	free(tempfile);
	free(argv);

	return ret;
}


int config_add_architecture(char *arch)
{
	if(strcmp(arch, "auto") == 0) {
		struct utsname un;
		char *newarch;
		uname(&un);
		newarch = strdup(un.machine);
		free(arch);
		arch = newarch;
	}

	pm_printf(ALPM_LOG_DEBUG, "config: arch: %s\n", arch);
	config->architectures = alpm_list_add(config->architectures, arch);
	return 0;
}

/**
 * Parse a string into long number. The input string has to be non-empty
 * and represent a number that fits long type.
 * @param value the string to parse
 * @param result pointer to long where the final result will be stored.
 *   This result is modified if the input string parsed successfully.
 * @return 0 in case if value parsed successfully, 1 otherwise.
 */
static int parse_number(char *value, long *result) {
	char *endptr;
	long val;
	int invalid;

	errno = 0; /* To distinguish success/failure after call */
	val = strtol(value, &endptr, 10);
	invalid = (errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
		|| (*endptr != '\0')
		|| (endptr == value);

	if(!invalid) {
		*result = val;
	}

	return invalid;
}

/**
 * Parse a signature verification level line.
 * @param values the list of parsed option values
 * @param storage location to store the derived signature level; any existing
 * value here is used as a starting point
 * @param file path to the config file
 * @param linenum current line number in file
 * @return 0 on success, 1 on any parsing error
 */
static int process_siglevel(alpm_list_t *values, int *storage,
		int *storage_mask, const char *file, int linenum)
{
	int level = *storage, mask = *storage_mask;
	alpm_list_t *i;
	int ret = 0;

#define SLSET(sl) do { level |= (sl); mask |= (sl); } while(0)
#define SLUNSET(sl) do { level &= ~(sl); mask |= (sl); } while(0)

	/* Collapse the option names into a single bitmasked value */
	for(i = values; i; i = alpm_list_next(i)) {
		const char *original = i->data, *value;
		int package = 0, database = 0;

		if(strncmp(original, "Package", strlen("Package")) == 0) {
			/* only packages are affected, don't flip flags for databases */
			value = original + strlen("Package");
			package = 1;
		} else if(strncmp(original, "Database", strlen("Database")) == 0) {
			/* only databases are affected, don't flip flags for packages */
			value = original + strlen("Database");
			database = 1;
		} else {
			/* no prefix, so anything found will affect both packages and dbs */
			value = original;
			package = database = 1;
		}

		/* now parse out and store actual flag if it is valid */
		if(strcmp(value, "Never") == 0) {
			if(package) {
				SLUNSET(ALPM_SIG_PACKAGE);
			}
			if(database) {
				SLUNSET(ALPM_SIG_DATABASE);
			}
		} else if(strcmp(value, "Optional") == 0) {
			if(package) {
				SLSET(ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL);
			}
			if(database) {
				SLSET(ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
			}
		} else if(strcmp(value, "Required") == 0) {
			if(package) {
				SLSET(ALPM_SIG_PACKAGE);
				SLUNSET(ALPM_SIG_PACKAGE_OPTIONAL);
			}
			if(database) {
				SLSET(ALPM_SIG_DATABASE);
				SLUNSET(ALPM_SIG_DATABASE_OPTIONAL);
			}
		} else if(strcmp(value, "TrustedOnly") == 0) {
			if(package) {
				SLUNSET(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK);
			}
			if(database) {
				SLUNSET(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK);
			}
		} else if(strcmp(value, "TrustAll") == 0) {
			if(package) {
				SLSET(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK);
			}
			if(database) {
				SLSET(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK);
			}
		} else {
			pm_printf(ALPM_LOG_ERROR,
					_("config file %s, line %d: invalid value for '%s' : '%s'\n"),
					file, linenum, "SigLevel", original);
			ret = 1;
		}
		level &= ~ALPM_SIG_USE_DEFAULT;
	}

#undef SLSET
#undef SLUNSET

	/* ensure we have sig checking ability and are actually turning it on */
	if(!(alpm_capabilities() & ALPM_CAPABILITY_SIGNATURES) &&
			level & (ALPM_SIG_PACKAGE | ALPM_SIG_DATABASE)) {
		pm_printf(ALPM_LOG_ERROR,
				_("config file %s, line %d: '%s' option invalid, no signature support\n"),
				file, linenum, "SigLevel");
		ret = 1;
	}

	if(!ret) {
		*storage = level;
		*storage_mask = mask;
	}
	return ret;
}

/**
 * Merge the package entries of two signature verification levels.
 * @param base initial siglevel
 * @param over overriding siglevel
 * @return merged siglevel
 */
static int merge_siglevel(int base, int over, int mask)
{
	return mask ? (over & mask) | (base & ~mask) : over;
}

static int process_cleanmethods(alpm_list_t *values,
		const char *file, int linenum)
{
	alpm_list_t *i;
	for(i = values; i; i = alpm_list_next(i)) {
		const char *value = i->data;
		if(strcmp(value, "KeepInstalled") == 0) {
			config->cleanmethod |= PM_CLEAN_KEEPINST;
		} else if(strcmp(value, "KeepCurrent") == 0) {
			config->cleanmethod |= PM_CLEAN_KEEPCUR;
		} else {
			pm_printf(ALPM_LOG_ERROR,
					_("config file %s, line %d: invalid value for '%s' : '%s'\n"),
					file, linenum, "CleanMethod", value);
			return 1;
		}
	}
	return 0;
}

/** Add repeating options such as NoExtract, NoUpgrade, etc to libalpm
 * settings. Refactored out of the parseconfig code since all of them did
 * the exact same thing and duplicated code.
 * @param ptr a pointer to the start of the multiple options
 * @param option the string (friendly) name of the option, used for messages
 * @param list the list to add the option to
 */
static void setrepeatingoption(char *ptr, const char *option,
		alpm_list_t **list)
{
	char *val, *saveptr = NULL;

	val = strtok_r(ptr, " ", &saveptr);
	while(val) {
		*list = alpm_list_add(*list, strdup(val));
		pm_printf(ALPM_LOG_DEBUG, "config: %s: %s\n", option, val);
		val = strtok_r(NULL, " ", &saveptr);
	}
}

static int _parse_options(const char *key, char *value,
		const char *file, int linenum)
{
	if(value == NULL) {
		/* options without settings */
		if(strcmp(key, "UseSyslog") == 0) {
			config->usesyslog = 1;
			pm_printf(ALPM_LOG_DEBUG, "config: usesyslog\n");
		} else if(strcmp(key, "ILoveCandy") == 0) {
			config->chomp = 1;
			pm_printf(ALPM_LOG_DEBUG, "config: chomp\n");
		} else if(strcmp(key, "VerbosePkgLists") == 0) {
			config->verbosepkglists = 1;
			pm_printf(ALPM_LOG_DEBUG, "config: verbosepkglists\n");
		} else if(strcmp(key, "CheckSpace") == 0) {
			config->checkspace = 1;
		} else if(strcmp(key, "Color") == 0) {
			if(config->color == PM_COLOR_UNSET) {
				config->color = isatty(fileno(stdout)) ? PM_COLOR_ON : PM_COLOR_OFF;
				enable_colors(config->color);
			}
		} else if(strcmp(key, "NoProgressBar") == 0) {
			config->noprogressbar = 1;
		} else if(strcmp(key, "DisableDownloadTimeout") == 0) {
			config->disable_dl_timeout = 1;
		} else {
			pm_printf(ALPM_LOG_WARNING,
					_("config file %s, line %d: directive '%s' in section '%s' not recognized.\n"),
					file, linenum, key, "options");
		}
	} else {
		/* options with settings */
		if(strcmp(key, "NoUpgrade") == 0) {
			setrepeatingoption(value, "NoUpgrade", &(config->noupgrade));
		} else if(strcmp(key, "NoExtract") == 0) {
			setrepeatingoption(value, "NoExtract", &(config->noextract));
		} else if(strcmp(key, "IgnorePkg") == 0) {
			setrepeatingoption(value, "IgnorePkg", &(config->ignorepkg));
		} else if(strcmp(key, "IgnoreGroup") == 0) {
			setrepeatingoption(value, "IgnoreGroup", &(config->ignoregrp));
		} else if(strcmp(key, "HoldPkg") == 0) {
			setrepeatingoption(value, "HoldPkg", &(config->holdpkg));
		} else if(strcmp(key, "CacheDir") == 0) {
			setrepeatingoption(value, "CacheDir", &(config->cachedirs));
		} else if(strcmp(key, "HookDir") == 0) {
			setrepeatingoption(value, "HookDir", &(config->hookdirs));
		} else if(strcmp(key, "Architecture") == 0) {
			alpm_list_t *i, *arches = NULL;
			setrepeatingoption(value, "Architecture", &arches);
			for(i = arches; i; i = alpm_list_next(i)) {
				config_add_architecture(i->data);
			}
			alpm_list_free(arches);
		} else if(strcmp(key, "DBPath") == 0) {
			/* don't overwrite a path specified on the command line */
			if(!config->dbpath) {
				config->dbpath = strdup(value);
				pm_printf(ALPM_LOG_DEBUG, "config: dbpath: %s\n", value);
			}
		} else if(strcmp(key, "RootDir") == 0) {
			/* don't overwrite a path specified on the command line */
			if(!config->rootdir) {
				config->rootdir = strdup(value);
				pm_printf(ALPM_LOG_DEBUG, "config: rootdir: %s\n", value);
			}
		} else if(strcmp(key, "GPGDir") == 0) {
			if(!config->gpgdir) {
				config->gpgdir = strdup(value);
				pm_printf(ALPM_LOG_DEBUG, "config: gpgdir: %s\n", value);
			}
		} else if(strcmp(key, "LogFile") == 0) {
			if(!config->logfile) {
				config->logfile = strdup(value);
				pm_printf(ALPM_LOG_DEBUG, "config: logfile: %s\n", value);
			}
		} else if(strcmp(key, "XferCommand") == 0) {
			char **c;
			if((config->xfercommand_argv = wordsplit(value)) == NULL) {
				pm_printf(ALPM_LOG_ERROR,
						_("config file %s, line %d: invalid value for '%s' : '%s'\n"),
						file, linenum, "XferCommand", value);
				return 1;
			}
			config->xfercommand_argc = 0;
			for(c = config->xfercommand_argv; *c; c++) {
				config->xfercommand_argc++;
			}
			config->xfercommand = strdup(value);
			pm_printf(ALPM_LOG_DEBUG, "config: xfercommand: %s\n", value);
		} else if(strcmp(key, "CleanMethod") == 0) {
			alpm_list_t *methods = NULL;
			setrepeatingoption(value, "CleanMethod", &methods);
			if(process_cleanmethods(methods, file, linenum)) {
				FREELIST(methods);
				return 1;
			}
			FREELIST(methods);
		} else if(strcmp(key, "SigLevel") == 0) {
			alpm_list_t *values = NULL;
			setrepeatingoption(value, "SigLevel", &values);
			if(process_siglevel(values, &config->siglevel,
						&config->siglevel_mask, file, linenum)) {
				FREELIST(values);
				return 1;
			}
			FREELIST(values);
		} else if(strcmp(key, "LocalFileSigLevel") == 0) {
			alpm_list_t *values = NULL;
			setrepeatingoption(value, "LocalFileSigLevel", &values);
			if(process_siglevel(values, &config->localfilesiglevel,
						&config->localfilesiglevel_mask, file, linenum)) {
				FREELIST(values);
				return 1;
			}
			FREELIST(values);
		} else if(strcmp(key, "RemoteFileSigLevel") == 0) {
			alpm_list_t *values = NULL;
			setrepeatingoption(value, "RemoteFileSigLevel", &values);
			if(process_siglevel(values, &config->remotefilesiglevel,
						&config->remotefilesiglevel_mask, file, linenum)) {
				FREELIST(values);
				return 1;
			}
			FREELIST(values);
		} else if(strcmp(key, "ParallelDownloads") == 0) {
			long number;
			int err;

			err = parse_number(value, &number);
			if(err) {
				pm_printf(ALPM_LOG_ERROR,
						_("config file %s, line %d: invalid value for '%s' : '%s'\n"),
						file, linenum, "ParallelDownloads", value);
				return 1;
			}

			if(number < 1) {
				pm_printf(ALPM_LOG_ERROR,
						_("config file %s, line %d: value for '%s' has to be positive : '%s'\n"),
						file, linenum, "ParallelDownloads", value);
				return 1;
			}

			if(number > INT_MAX) {
				pm_printf(ALPM_LOG_ERROR,
						_("config file %s, line %d: value for '%s' is too large : '%s'\n"),
						file, linenum, "ParallelDownloads", value);
				return 1;
			}

			config->parallel_downloads = number;
		} else {
			pm_printf(ALPM_LOG_WARNING,
					_("config file %s, line %d: directive '%s' in section '%s' not recognized.\n"),
					file, linenum, key, "options");
		}

	}
	return 0;
}

static char *replace_server_vars(config_t *c, config_repo_t *r, const char *s)
{
	if(c->architectures == NULL && strstr(s, "$arch")) {
		pm_printf(ALPM_LOG_ERROR,
				_("mirror '%s' contains the '%s' variable, but no '%s' is defined.\n"),
				s, "$arch", "Architecture");
		return NULL;
	}

	/* use first specified architecture */
	if(c->architectures) {
		char *temp, *replaced;
		alpm_list_t *i = config->architectures;
		const char *arch = i->data;

		replaced = strreplace(s, "$arch", arch);

		temp = replaced;
		replaced = strreplace(temp, "$repo", r->name);
		free(temp);

		return replaced;
	} else {
		return strreplace(s, "$repo", r->name);
	}
}

static int _add_mirror(alpm_db_t *db, char *value)
{
	if(alpm_db_add_server(db, value) != 0) {
		/* pm_errno is set by alpm_db_setserver */
		pm_printf(ALPM_LOG_ERROR, _("could not add server URL to database '%s': %s (%s)\n"),
				alpm_db_get_name(db), value, alpm_strerror(alpm_errno(config->handle)));
		return 1;
	}

	return 0;
}

static int register_repo(config_repo_t *repo)
{
	alpm_list_t *i;
	alpm_db_t *db;

	db = alpm_register_syncdb(config->handle, repo->name, repo->siglevel);
	if(db == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("could not register '%s' database (%s)\n"),
				repo->name, alpm_strerror(alpm_errno(config->handle)));
		return 1;
	}

	pm_printf(ALPM_LOG_DEBUG, "setting usage of %d for %s repository\n",
			repo->usage, repo->name);
	alpm_db_set_usage(db, repo->usage);

	for(i = repo->servers; i; i = alpm_list_next(i)) {
		if(_add_mirror(db, i->data) != 0) {
			return 1;
		}
	}

	return 0;
}

/** Sets up libalpm global stuff in one go. Called after the command line
 * and initial config file parsing. Once this is complete, we can see if any
 * paths were defined. If a rootdir was defined and nothing else, we want all
 * of our paths to live under the rootdir that was specified. Safe to call
 * multiple times (will only do anything the first time).
 */
static int setup_libalpm(void)
{
	int ret = 0;
	alpm_errno_t err;
	alpm_handle_t *handle;
	alpm_list_t *i;

	pm_printf(ALPM_LOG_DEBUG, "setup_libalpm called\n");

	/* initialize library */
	handle = alpm_initialize(config->rootdir, config->dbpath, &err);
	if(!handle) {
		pm_printf(ALPM_LOG_ERROR, _("failed to initialize alpm library:\n(root: %s, dbpath: %s)\n%s\n"),
		        config->rootdir, config->dbpath, alpm_strerror(err));
		if(err == ALPM_ERR_DB_VERSION) {
			fprintf(stderr, _("try running pacman-db-upgrade\n"));
		}
		return -1;
	}
	config->handle = handle;

	alpm_option_set_logcb(handle, cb_log, NULL);
	alpm_option_set_dlcb(handle, cb_download, NULL);
	alpm_option_set_eventcb(handle, cb_event, NULL);
	alpm_option_set_questioncb(handle, cb_question, NULL);
	alpm_option_set_progresscb(handle, cb_progress, NULL);

	if(config->op == PM_OP_FILES) {
		alpm_option_set_dbext(handle, ".files");
	}

	ret = alpm_option_set_logfile(handle, config->logfile);
	if(ret != 0) {
		pm_printf(ALPM_LOG_ERROR, _("problem setting logfile '%s' (%s)\n"),
				config->logfile, alpm_strerror(alpm_errno(handle)));
		return ret;
	}

	/* Set GnuPG's home directory. This is not relative to rootdir, even if
	 * rootdir is defined. Reasoning: gpgdir contains configuration data. */
	ret = alpm_option_set_gpgdir(handle, config->gpgdir);
	if(ret != 0) {
		pm_printf(ALPM_LOG_ERROR, _("problem setting gpgdir '%s' (%s)\n"),
				config->gpgdir, alpm_strerror(alpm_errno(handle)));
		return ret;
	}

	/* Set user hook directory. This is not relative to rootdir, even if
	 * rootdir is defined. Reasoning: hookdir contains configuration data. */
	/* add hook directories 1-by-1 to avoid overwriting the system directory */
	for(i = config->hookdirs; i; i = alpm_list_next(i)) {
		if((ret = alpm_option_add_hookdir(handle, i->data)) != 0) {
			pm_printf(ALPM_LOG_ERROR, _("problem adding hookdir '%s' (%s)\n"),
					(char *) i->data, alpm_strerror(alpm_errno(handle)));
			return ret;
		}
	}

	alpm_option_set_cachedirs(handle, config->cachedirs);

	alpm_option_set_overwrite_files(handle, config->overwrite_files);

	alpm_option_set_default_siglevel(handle, config->siglevel);

	alpm_option_set_local_file_siglevel(handle, config->localfilesiglevel);
	alpm_option_set_remote_file_siglevel(handle, config->remotefilesiglevel);

	for(i = config->repos; i; i = alpm_list_next(i)) {
		register_repo(i->data);
	}

	if(config->xfercommand) {
		alpm_option_set_fetchcb(handle, download_with_xfercommand, NULL);
	} else if(!(alpm_capabilities() & ALPM_CAPABILITY_DOWNLOADER)) {
		pm_printf(ALPM_LOG_WARNING, _("no '%s' configured\n"), "XferCommand");
	}

	alpm_option_set_architectures(handle, config->architectures);
	alpm_option_set_checkspace(handle, config->checkspace);
	alpm_option_set_usesyslog(handle, config->usesyslog);

	alpm_option_set_ignorepkgs(handle, config->ignorepkg);
	alpm_option_set_ignoregroups(handle, config->ignoregrp);
	alpm_option_set_noupgrades(handle, config->noupgrade);
	alpm_option_set_noextracts(handle, config->noextract);

	alpm_option_set_disable_dl_timeout(handle, config->disable_dl_timeout);
	alpm_option_set_parallel_downloads(handle, config->parallel_downloads);

	for(i = config->assumeinstalled; i; i = i->next) {
		char *entry = i->data;
		alpm_depend_t *dep = alpm_dep_from_string(entry);
		if(!dep) {
			return 1;
		}
		pm_printf(ALPM_LOG_DEBUG, "parsed assume installed: %s %s\n", dep->name, dep->version);

		ret = alpm_option_add_assumeinstalled(handle, dep);
		alpm_dep_free(dep);
		if(ret) {
			pm_printf(ALPM_LOG_ERROR, _("Failed to pass %s entry to libalpm"), "assume-installed");
			return ret;
		}
	 }

	return 0;
}

/**
 * Allows parsing in advance of an entire config section before we start
 * calling library methods.
 */
struct section_t {
	const char *name;
	config_repo_t *repo;
	int depth;
};

static int process_usage(alpm_list_t *values, int *usage,
		const char *file, int linenum)
{
	alpm_list_t *i;
	int level = *usage;
	int ret = 0;

	for(i = values; i; i = i->next) {
		char *key = i->data;

		if(strcmp(key, "Sync") == 0) {
			level |= ALPM_DB_USAGE_SYNC;
		} else if(strcmp(key, "Search") == 0) {
			level |= ALPM_DB_USAGE_SEARCH;
		} else if(strcmp(key, "Install") == 0) {
			level |= ALPM_DB_USAGE_INSTALL;
		} else if(strcmp(key, "Upgrade") == 0) {
			level |= ALPM_DB_USAGE_UPGRADE;
		} else if(strcmp(key, "All") == 0) {
			level |= ALPM_DB_USAGE_ALL;
		} else {
			pm_printf(ALPM_LOG_ERROR,
					_("config file %s, line %d: '%s' option '%s' not recognized\n"),
					file, linenum, "Usage", key);
			ret = 1;
		}
	}

	*usage = level;

	return ret;
}


static int _parse_repo(const char *key, char *value, const char *file,
		int line, struct section_t *section)
{
	int ret = 0;
	config_repo_t *repo = section->repo;

#define CHECK_VALUE(val) do { \
	if(!val) { \
		pm_printf(ALPM_LOG_ERROR, _("config file %s, line %d: directive '%s' needs a value\n"), \
				file, line, key); \
		return 1; \
	} \
} while(0)

	if(strcmp(key, "Server") == 0) {
		CHECK_VALUE(value);
		repo->servers = alpm_list_add(repo->servers, strdup(value));
	} else if(strcmp(key, "SigLevel") == 0) {
		CHECK_VALUE(value);
		alpm_list_t *values = NULL;
		setrepeatingoption(value, "SigLevel", &values);
		if(values) {
			ret = process_siglevel(values, &repo->siglevel,
					&repo->siglevel_mask, file, line);
			FREELIST(values);
		}
	} else if(strcmp(key, "Usage") == 0) {
		CHECK_VALUE(value);
		alpm_list_t *values = NULL;
		setrepeatingoption(value, "Usage", &values);
		if(values) {
			if(process_usage(values, &repo->usage, file, line)) {
				FREELIST(values);
				return 1;
			}
			FREELIST(values);
		}
	} else {
		pm_printf(ALPM_LOG_WARNING,
				_("config file %s, line %d: directive '%s' in section '%s' not recognized.\n"),
				file, line, key, repo->name);
	}

#undef CHECK_VALUE

	return ret;
}

static int _parse_directive(const char *file, int linenum, const char *name,
		char *key, char *value, void *data);

static int process_include(const char *value, void *data,
		const char *file, int linenum)
{
	glob_t globbuf;
	int globret, ret = 0;
	size_t gindex;
	struct section_t *section = data;
	static const int config_max_recursion = 10;

	if(value == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("config file %s, line %d: directive '%s' needs a value\n"),
				file, linenum, "Include");
		return 1;
	}

	if(section->depth >= config_max_recursion) {
		pm_printf(ALPM_LOG_ERROR,
				_("config parsing exceeded max recursion depth of %d.\n"),
				config_max_recursion);
		return 1;
	}

	section->depth++;

	/* Ignore include failures... assume non-critical */
	globret = glob(value, GLOB_NOCHECK, NULL, &globbuf);
	switch(globret) {
		case GLOB_NOSPACE:
			pm_printf(ALPM_LOG_DEBUG,
					"config file %s, line %d: include globbing out of space\n",
					file, linenum);
			break;
		case GLOB_ABORTED:
			pm_printf(ALPM_LOG_DEBUG,
					"config file %s, line %d: include globbing read error for %s\n",
					file, linenum, value);
			break;
		case GLOB_NOMATCH:
			pm_printf(ALPM_LOG_DEBUG,
					"config file %s, line %d: no include found for %s\n",
					file, linenum, value);
			break;
		default:
			for(gindex = 0; gindex < globbuf.gl_pathc; gindex++) {
				pm_printf(ALPM_LOG_DEBUG, "config file %s, line %d: including %s\n",
						file, linenum, globbuf.gl_pathv[gindex]);
				ret = parse_ini(globbuf.gl_pathv[gindex], _parse_directive, data);
				if(ret) {
					goto cleanup;
				}
			}
			break;
	}

cleanup:
	section->depth--;
	globfree(&globbuf);
	return ret;
}

static int _parse_directive(const char *file, int linenum, const char *name,
		char *key, char *value, void *data)
{
	struct section_t *section = data;
	if(!name && !key && !value) {
		pm_printf(ALPM_LOG_ERROR, _("config file %s could not be read: %s\n"),
				file, strerror(errno));
		return 1;
	} else if(!key && !value) {
		section->name = name;
		pm_printf(ALPM_LOG_DEBUG, "config: new section '%s'\n", name);
		if(strcmp(name, "options") == 0) {
			section->repo = NULL;
		} else {
			section->repo = calloc(sizeof(config_repo_t), 1);
			section->repo->name = strdup(name);
			section->repo->siglevel = ALPM_SIG_USE_DEFAULT;
			section->repo->usage = 0;
			config->repos = alpm_list_add(config->repos, section->repo);
		}
		return 0;
	}

	if(strcmp(key, "Include") == 0) {
		return process_include(value, data, file, linenum);
	}

	if(section->name == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("config file %s, line %d: All directives must belong to a section.\n"),
				file, linenum);
		return 1;
	}

	if(!section->repo) {
		/* we are either in options ... */
		return _parse_options(key, value, file, linenum);
	} else {
		/* ... or in a repo section */
		return _parse_repo(key, value, file, linenum, section);
	}
}

int setdefaults(config_t *c)
{
	alpm_list_t *i;

#define SETDEFAULT(opt, val) if(!opt) { opt = val; if(!opt) { return -1; } }

	if(c->rootdir) {
		char path[PATH_MAX];
		if(!c->dbpath) {
			snprintf(path, PATH_MAX, "%s/%s", c->rootdir, &DBPATH[1]);
			SETDEFAULT(c->dbpath, strdup(path));
		}
		if(!c->logfile) {
			snprintf(path, PATH_MAX, "%s/%s", c->rootdir, &LOGFILE[1]);
			SETDEFAULT(c->logfile, strdup(path));
		}
	} else {
		SETDEFAULT(c->rootdir, strdup(ROOTDIR));
		SETDEFAULT(c->dbpath, strdup(DBPATH));
	}

	SETDEFAULT(c->logfile, strdup(LOGFILE));
	SETDEFAULT(c->gpgdir, strdup(GPGDIR));
	SETDEFAULT(c->cachedirs, alpm_list_add(NULL, strdup(CACHEDIR)));
	SETDEFAULT(c->hookdirs, alpm_list_add(NULL, strdup(HOOKDIR)));
	SETDEFAULT(c->cleanmethod, PM_CLEAN_KEEPINST);

	c->localfilesiglevel = merge_siglevel(c->siglevel,
			c->localfilesiglevel, c->localfilesiglevel_mask);
	c->remotefilesiglevel = merge_siglevel(c->siglevel,
			c->remotefilesiglevel, c->remotefilesiglevel_mask);

	for(i = c->repos; i; i = i->next) {
		config_repo_t *r = i->data;
		alpm_list_t *j;
		SETDEFAULT(r->usage, ALPM_DB_USAGE_ALL);
		r->siglevel = merge_siglevel(c->siglevel, r->siglevel, r->siglevel_mask);
		for(j = r->servers; j; j = j->next) {
			char *newurl = replace_server_vars(c, r, j->data);
			if(newurl == NULL) {
				return -1;
			} else {
				free(j->data);
				j->data = newurl;
			}
		}
	}

#undef SETDEFAULT

	return 0;
}

int parseconfigfile(const char *file)
{
	struct section_t section = {0};
	pm_printf(ALPM_LOG_DEBUG, "config: attempting to read file %s\n", file);
	return parse_ini(file, _parse_directive, &section);
}

/** Parse a configuration file.
 * @param file path to the config file
 * @return 0 on success, non-zero on error
 */
int parseconfig(const char *file)
{
	int ret;
	if((ret = parseconfigfile(file))) {
		return ret;
	}
	if((ret = setdefaults(config))) {
		return ret;
	}
	pm_printf(ALPM_LOG_DEBUG, "config: finished parsing %s\n", file);
	if((ret = setup_libalpm())) {
		return ret;
	}
	alpm_list_free_inner(config->repos, (alpm_list_fn_free) config_repo_free);
	alpm_list_free(config->repos);
	config->repos = NULL;
	return ret;
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Package.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////
#define CLBUF_SIZE 4096

/* The term "title" refers to the first field of each line in the package
 * information displayed by pacman. Titles are stored in the `titles` array and
 * referenced by the following indices.
 */
enum {
	T_ARCHITECTURE = 0,
	T_BACKUP_FILES,
	T_BUILD_DATE,
	T_COMPRESSED_SIZE,
	T_CONFLICTS_WITH,
	T_DEPENDS_ON,
	T_DESCRIPTION,
	T_DOWNLOAD_SIZE,
	T_GROUPS,
	T_INSTALL_DATE,
	T_INSTALL_REASON,
	T_INSTALL_SCRIPT,
	T_INSTALLED_SIZE,
	T_LICENSES,
	T_MD5_SUM,
	T_NAME,
	T_OPTIONAL_DEPS,
	T_OPTIONAL_FOR,
	T_PACKAGER,
	T_PROVIDES,
	T_REPLACES,
	T_REPOSITORY,
	T_REQUIRED_BY,
	T_SHA_256_SUM,
	T_SIGNATURES,
	T_URL,
	T_VALIDATED_BY,
	T_VERSION,
	/* the following is a sentinel and should remain in last position */
	_T_MAX
};

/* As of 2015/10/20, the longest title (all locales considered) was less than 30
 * characters long. We set the title maximum length to 50 to allow for some
 * potential growth.
 */
#define TITLE_MAXLEN 50

static char titles[_T_MAX][TITLE_MAXLEN * sizeof(wchar_t)];

/** Build the `titles` array of localized titles and pad them with spaces so
 * that they align with the longest title. Storage for strings is stack
 * allocated and naively truncated to TITLE_MAXLEN characters.
 */
static void make_aligned_titles(void)
{
	unsigned int i;
	size_t maxlen = 0;
	int maxcol = 0;
	static const wchar_t title_suffix[] = L" :";
	wchar_t wbuf[ARRAYSIZE(titles)][TITLE_MAXLEN + ARRAYSIZE(title_suffix)] = {{ 0 }};
	size_t wlen[ARRAYSIZE(wbuf)];
	int wcol[ARRAYSIZE(wbuf)];
	char *buf[ARRAYSIZE(wbuf)];
	buf[T_ARCHITECTURE] = _("Architecture");
	buf[T_BACKUP_FILES] = _("Backup Files");
	buf[T_BUILD_DATE] = _("Build Date");
	buf[T_COMPRESSED_SIZE] = _("Compressed Size");
	buf[T_CONFLICTS_WITH] = _("Conflicts With");
	buf[T_DEPENDS_ON] = _("Depends On");
	buf[T_DESCRIPTION] = _("Description");
	buf[T_DOWNLOAD_SIZE] = _("Download Size");
	buf[T_GROUPS] = _("Groups");
	buf[T_INSTALL_DATE] = _("Install Date");
	buf[T_INSTALL_REASON] = _("Install Reason");
	buf[T_INSTALL_SCRIPT] = _("Install Script");
	buf[T_INSTALLED_SIZE] = _("Installed Size");
	buf[T_LICENSES] = _("Licenses");
	buf[T_MD5_SUM] = _("MD5 Sum");
	buf[T_NAME] = _("Name");
	buf[T_OPTIONAL_DEPS] = _("Optional Deps");
	buf[T_OPTIONAL_FOR] = _("Optional For");
	buf[T_PACKAGER] = _("Packager");
	buf[T_PROVIDES] = _("Provides");
	buf[T_REPLACES] = _("Replaces");
	buf[T_REPOSITORY] = _("Repository");
	buf[T_REQUIRED_BY] = _("Required By");
	buf[T_SHA_256_SUM] = _("SHA-256 Sum");
	buf[T_SIGNATURES] = _("Signatures");
	buf[T_URL] = _("URL");
	buf[T_VALIDATED_BY] = _("Validated By");
	buf[T_VERSION] = _("Version");

	for(i = 0; i < ARRAYSIZE(wbuf); i++) {
		wlen[i] = mbstowcs(wbuf[i], buf[i], strlen(buf[i]) + 1);
		wcol[i] = wcswidth(wbuf[i], wlen[i]);
		if(wcol[i] > maxcol) {
			maxcol = wcol[i];
		}
		if(wlen[i] > maxlen) {
			maxlen = wlen[i];
		}
	}

	for(i = 0; i < ARRAYSIZE(wbuf); i++) {
		size_t padlen = maxcol - wcol[i];
		wmemset(wbuf[i] + wlen[i], L' ', padlen);
		wmemcpy(wbuf[i] + wlen[i] + padlen, title_suffix, ARRAYSIZE(title_suffix));
		wcstombs(titles[i], wbuf[i], sizeof(wbuf[i]));
	}
}

/** Turn a depends list into a text list.
 * @param deps a list with items of type alpm_depend_t
 */
static void deplist_display(const char *title,
		alpm_list_t *deps, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	for(i = deps; i; i = alpm_list_next(i)) {
		alpm_depend_t *dep = i->data;
		text = alpm_list_add(text, alpm_dep_compute_string(dep));
	}
	list_display(title, text, cols);
	FREELIST(text);
}

/** Turn a optdepends list into a text list.
 * @param optdeps a list with items of type alpm_depend_t
 */
static void optdeplist_display(alpm_pkg_t *pkg, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	alpm_db_t *localdb = alpm_get_localdb(config->handle);
	for(i = alpm_pkg_get_optdepends(pkg); i; i = alpm_list_next(i)) {
		alpm_depend_t *optdep = i->data;
		char *depstring = alpm_dep_compute_string(optdep);
		if(alpm_pkg_get_origin(pkg) == ALPM_PKG_FROM_LOCALDB) {
			if(alpm_find_satisfier(alpm_db_get_pkgcache(localdb), depstring)) {
				const char *installed = _(" [installed]");
				depstring = realloc(depstring, strlen(depstring) + strlen(installed) + 1);
				strcpy(depstring + strlen(depstring), installed);
			}
		}
		text = alpm_list_add(text, depstring);
	}
	list_display_linebreak(titles[T_OPTIONAL_DEPS], text, cols);
	FREELIST(text);
}

/**
 * Display the details of a package.
 * Extra information entails 'required by' info for sync packages and backup
 * files info for local packages.
 * @param pkg package to display information for
 * @param extra should we show extra information
 */
void dump_pkg_full(alpm_pkg_t *pkg, int extra)
{
	unsigned short cols;
	time_t bdate, idate;
	alpm_pkgfrom_t from;
	double size;
	char bdatestr[50] = "", idatestr[50] = "";
	const char *label, *reason;
	alpm_list_t *validation = NULL, *requiredby = NULL, *optionalfor = NULL;

	/* make aligned titles once only */
	static int need_alignment = 1;
	if(need_alignment) {
		need_alignment = 0;
		make_aligned_titles();
	}

	from = alpm_pkg_get_origin(pkg);

	/* set variables here, do all output below */
	bdate = (time_t)alpm_pkg_get_builddate(pkg);
	if(bdate) {
		strftime(bdatestr, 50, "%c", localtime(&bdate));
	}
	idate = (time_t)alpm_pkg_get_installdate(pkg);
	if(idate) {
		strftime(idatestr, 50, "%c", localtime(&idate));
	}

	switch(alpm_pkg_get_reason(pkg)) {
		case ALPM_PKG_REASON_EXPLICIT:
			reason = _("Explicitly installed");
			break;
		case ALPM_PKG_REASON_DEPEND:
			reason = _("Installed as a dependency for another package");
			break;
		default:
			reason = _("Unknown");
			break;
	}

	int v = alpm_pkg_get_validation(pkg);
	if(v) {
		if(v & ALPM_PKG_VALIDATION_NONE) {
			validation = alpm_list_add(validation, _("None"));
		} else {
			if(v & ALPM_PKG_VALIDATION_MD5SUM) {
				validation = alpm_list_add(validation, _("MD5 Sum"));
			}
			if(v & ALPM_PKG_VALIDATION_SHA256SUM) {
				validation = alpm_list_add(validation, _("SHA-256 Sum"));
			}
			if(v & ALPM_PKG_VALIDATION_SIGNATURE) {
				validation = alpm_list_add(validation, _("Signature"));
			}
		}
	} else {
		validation = alpm_list_add(validation, _("Unknown"));
	}

	if(extra || from == ALPM_PKG_FROM_LOCALDB) {
		/* compute this here so we don't get a pause in the middle of output */
		requiredby = alpm_pkg_compute_requiredby(pkg);
		optionalfor = alpm_pkg_compute_optionalfor(pkg);
	}

	cols = getcols();

	/* actual output */
	if(from == ALPM_PKG_FROM_SYNCDB) {
		string_display(titles[T_REPOSITORY],
				alpm_db_get_name(alpm_pkg_get_db(pkg)), cols);
	}
	string_display(titles[T_NAME], alpm_pkg_get_name(pkg), cols);
	string_display(titles[T_VERSION], alpm_pkg_get_version(pkg), cols);
	string_display(titles[T_DESCRIPTION], alpm_pkg_get_desc(pkg), cols);
	string_display(titles[T_ARCHITECTURE], alpm_pkg_get_arch(pkg), cols);
	string_display(titles[T_URL], alpm_pkg_get_url(pkg), cols);
	list_display(titles[T_LICENSES], alpm_pkg_get_licenses(pkg), cols);
	list_display(titles[T_GROUPS], alpm_pkg_get_groups(pkg), cols);
	deplist_display(titles[T_PROVIDES], alpm_pkg_get_provides(pkg), cols);
	deplist_display(titles[T_DEPENDS_ON], alpm_pkg_get_depends(pkg), cols);
	optdeplist_display(pkg, cols);

	if(extra || from == ALPM_PKG_FROM_LOCALDB) {
		list_display(titles[T_REQUIRED_BY], requiredby, cols);
		list_display(titles[T_OPTIONAL_FOR], optionalfor, cols);
	}
	deplist_display(titles[T_CONFLICTS_WITH], alpm_pkg_get_conflicts(pkg), cols);
	deplist_display(titles[T_REPLACES], alpm_pkg_get_replaces(pkg), cols);

	size = humanize_size(alpm_pkg_get_size(pkg), '\0', 2, &label);
	if(from == ALPM_PKG_FROM_SYNCDB) {
		printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_DOWNLOAD_SIZE],
			config->colstr.nocolor, size, label);
	} else if(from == ALPM_PKG_FROM_FILE) {
		printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_COMPRESSED_SIZE],
			config->colstr.nocolor, size, label);
	} else {
		/* autodetect size for "Installed Size" */
		label = "\0";
	}

	size = humanize_size(alpm_pkg_get_isize(pkg), label[0], 2, &label);
	printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_INSTALLED_SIZE],
			config->colstr.nocolor, size, label);

	string_display(titles[T_PACKAGER], alpm_pkg_get_packager(pkg), cols);
	string_display(titles[T_BUILD_DATE], bdatestr, cols);
	if(from == ALPM_PKG_FROM_LOCALDB) {
		string_display(titles[T_INSTALL_DATE], idatestr, cols);
		string_display(titles[T_INSTALL_REASON], reason, cols);
	}
	if(from == ALPM_PKG_FROM_FILE || from == ALPM_PKG_FROM_LOCALDB) {
		string_display(titles[T_INSTALL_SCRIPT],
				alpm_pkg_has_scriptlet(pkg) ? _("Yes") : _("No"), cols);
	}

	if(from == ALPM_PKG_FROM_SYNCDB && extra) {
		const char *base64_sig = alpm_pkg_get_base64_sig(pkg);
		alpm_list_t *keys = NULL;
		if(base64_sig) {
			unsigned char *decoded_sigdata = NULL;
			size_t data_len;
			alpm_decode_signature(base64_sig, &decoded_sigdata, &data_len);
			alpm_extract_keyid(config->handle, alpm_pkg_get_name(pkg),
					decoded_sigdata, data_len, &keys);
			free(decoded_sigdata);
		} else {
			keys = alpm_list_add(keys, _("None"));
		}

		string_display(titles[T_MD5_SUM], alpm_pkg_get_md5sum(pkg), cols);
		string_display(titles[T_SHA_256_SUM], alpm_pkg_get_sha256sum(pkg), cols);
		list_display(titles[T_SIGNATURES], keys, cols);

		if(base64_sig) {
			FREELIST(keys);
		}
	} else {
		list_display(titles[T_VALIDATED_BY], validation, cols);
	}

	if(from == ALPM_PKG_FROM_FILE) {
		alpm_siglist_t siglist;
		int err = alpm_pkg_check_pgp_signature(pkg, &siglist);
		if(err && alpm_errno(config->handle) == ALPM_ERR_SIG_MISSING) {
			string_display(titles[T_SIGNATURES], _("None"), cols);
		} else if(err) {
			string_display(titles[T_SIGNATURES],
					alpm_strerror(alpm_errno(config->handle)), cols);
		} else {
			signature_display(titles[T_SIGNATURES], &siglist, cols);
		}
		alpm_siglist_cleanup(&siglist);
	}

	/* Print additional package info if info flag passed more than once */
	if(from == ALPM_PKG_FROM_LOCALDB && extra) {
		dump_pkg_backups(pkg, cols);
	}

	/* final newline to separate packages */
	printf("\n");

	FREELIST(requiredby);
	FREELIST(optionalfor);
	alpm_list_free(validation);
}

static const char *get_backup_file_status(const char *root,
		const alpm_backup_t *backup)
{
	char path[PATH_MAX];
	const char *ret;

	snprintf(path, PATH_MAX, "%s%s", root, backup->name);

	/* if we find the file, calculate checksums, otherwise it is missing */
	if(access(path, R_OK) == 0) {
		char *md5sum = alpm_compute_md5sum(path);

		if(md5sum == NULL) {
			pm_printf(ALPM_LOG_ERROR,
					_("could not calculate checksums for %s\n"), path);
			return NULL;
		}

		/* if checksums don't match, file has been modified */
		if(strcmp(md5sum, backup->hash) != 0) {
			ret = "[modified]";
		} else {
			ret = "[unmodified]";
		}
		free(md5sum);
	} else {
		switch(errno) {
			case EACCES:
				ret = "[unreadable]";
				break;
			case ENOENT:
				ret = "[missing]";
				break;
			default:
				ret = "[unknown]";
		}
	}
	return ret;
}

/* Display list of backup files and their modification states
 */
void dump_pkg_backups(alpm_pkg_t *pkg, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	const char *root = alpm_option_get_root(config->handle);
	/* package has backup files, so print them */
	for(i = alpm_pkg_get_backup(pkg); i; i = alpm_list_next(i)) {
		const alpm_backup_t *backup = i->data;
		const char *value;
		char *line;
		size_t needed;
		if(!backup->hash) {
			continue;
		}
		value = get_backup_file_status(root, backup);
		needed = strlen(root) + strlen(backup->name) + 1 + strlen(value) + 1;
		line = malloc(needed);
		if(!line) {
			goto cleanup;
		}
		sprintf(line, "%s%s %s", root, backup->name, value);
		text = alpm_list_add(text, line);
	}

	list_display_linebreak(titles[T_BACKUP_FILES], text, cols);

cleanup:
	FREELIST(text);
}

/* List all files contained in a package
 */
void dump_pkg_files(alpm_pkg_t *pkg, int quiet)
{
	const char *pkgname, *root;
	alpm_filelist_t *pkgfiles;
	size_t i;

	pkgname = alpm_pkg_get_name(pkg);
	pkgfiles = alpm_pkg_get_files(pkg);
	root = alpm_option_get_root(config->handle);

	for(i = 0; i < pkgfiles->count; i++) {
		const alpm_file_t *file = pkgfiles->files + i;
		/* Regular: '<pkgname> <root><filepath>\n'
		 * Quiet  : '<root><filepath>\n'
		 */
		if(!quiet) {
			printf("%s%s%s ", config->colstr.title, pkgname, config->colstr.nocolor);
		}
		printf("%s%s\n", root, file->name);
	}

	fflush(stdout);
}

/* Display the changelog of a package
 */
void dump_pkg_changelog(alpm_pkg_t *pkg)
{
	void *fp = NULL;

	if((fp = alpm_pkg_changelog_open(pkg)) == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("no changelog available for '%s'.\n"),
				alpm_pkg_get_name(pkg));
		return;
	} else {
		fprintf(stdout, _("Changelog for %s:\n"), alpm_pkg_get_name(pkg));
		/* allocate a buffer to get the changelog back in chunks */
		char buf[CLBUF_SIZE];
		size_t ret = 0;
		while((ret = alpm_pkg_changelog_read(buf, CLBUF_SIZE, pkg, fp))) {
			fwrite(buf, 1, ret, stdout);
		}
		alpm_pkg_changelog_close(pkg, fp);
		putchar('\n');
	}
}

void print_installed(alpm_db_t *db_local, alpm_pkg_t *pkg)
{
	const char *pkgname = alpm_pkg_get_name(pkg);
	const char *pkgver = alpm_pkg_get_version(pkg);
	alpm_pkg_t *lpkg = alpm_db_get_pkg(db_local, pkgname);
	if(lpkg) {
		const char *lpkgver = alpm_pkg_get_version(lpkg);
		const colstr_t *colstr = &config->colstr;
		if(strcmp(lpkgver, pkgver) == 0) {
			printf(" %s[%s]%s", colstr->meta, _("installed"), colstr->nocolor);
		} else {
			printf(" %s[%s: %s]%s", colstr->meta, _("installed"),
					lpkgver, colstr->nocolor);
		}
	}
}

void print_groups(alpm_pkg_t *pkg)
{
	alpm_list_t *grp;
	if((grp = alpm_pkg_get_groups(pkg)) != NULL) {
		const colstr_t *colstr = &config->colstr;
		alpm_list_t *k;
		printf(" %s(", colstr->groups);
		for(k = grp; k; k = alpm_list_next(k)) {
			const char *group = k->data;
			fputs(group, stdout);
			if(alpm_list_next(k)) {
				/* only print a spacer if there are more groups */
				putchar(' ');
			}
		}
		printf(")%s", colstr->nocolor);
	}
}

/**
 * Display the details of a search.
 * @param db the database we're searching
 * @param targets the targets we're searching for
 * @param show_status show if the package is also in the local db
 * @return -1 on error, 0 if there were matches, 1 if there were not
 */
int dump_pkg_search(alpm_db_t *db, alpm_list_t *targets, int show_status)
{
	int freelist = 0;
	alpm_db_t *db_local;
	alpm_list_t *i, *searchlist = NULL;
	unsigned short cols;
	const colstr_t *colstr = &config->colstr;

	if(show_status) {
		db_local = alpm_get_localdb(config->handle);
	}

	/* if we have a targets list, search for packages matching it */
	if(targets) {
		if(alpm_db_search(db, targets, &searchlist) != 0) {
			return -1;
		}
		freelist = 1;
	} else {
		searchlist = alpm_db_get_pkgcache(db);
		freelist = 0;
	}
	if(searchlist == NULL) {
		return 1;
	}

	cols = getcols();
	for(i = searchlist; i; i = alpm_list_next(i)) {
		alpm_pkg_t *pkg = i->data;

		if(config->quiet) {
			fputs(alpm_pkg_get_name(pkg), stdout);
		} else {
			printf("%s%s/%s%s %s%s%s", colstr->repo, alpm_db_get_name(db),
					colstr->title, alpm_pkg_get_name(pkg),
					colstr->version, alpm_pkg_get_version(pkg), colstr->nocolor);

			print_groups(pkg);
			if(show_status) {
				print_installed(db_local, pkg);
			}

			/* we need a newline and initial indent first */
			fputs("\n    ", stdout);
			indentprint(alpm_pkg_get_desc(pkg), 4, cols);
		}
		fputc('\n', stdout);
	}

	/* we only want to free if the list was a search list */
	if(freelist) {
		alpm_list_free(searchlist);
	}

	return 0;
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Pacman.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////
/* list of targets specified on command line */
static alpm_list_t *pm_targets;

/* Used to sort the options in --help */
static int options_cmp(const void *p1, const void *p2)
{
	const char *s1 = p1;
	const char *s2 = p2;

	if(s1 == s2) return 0;
	if(!s1) return -1;
	if(!s2) return 1;
	/* First skip all spaces in both strings */
	while(isspace((unsigned char)*s1)) {
		s1++;
	}
	while(isspace((unsigned char)*s2)) {
		s2++;
	}
	/* If we compare a long option (--abcd) and a short one (-a),
	 * the short one always wins */
	if(*s1 == '-' && *s2 == '-') {
		s1++;
		s2++;
		if(*s1 == '-' && *s2 == '-') {
			/* two long -> strcmp */
			s1++;
			s2++;
		} else if(*s2 == '-') {
			/* s1 short, s2 long */
			return -1;
		} else if(*s1 == '-') {
			/* s1 long, s2 short */
			return 1;
		}
		/* two short -> strcmp */
	}

	return strcmp(s1, s2);
}

/** Display usage/syntax for the specified operation.
 * @param op     the operation code requested
 * @param myname basename(argv[0])
 */
static void usage(int op, const char * const myname)
{
#define addlist(s) (list = alpm_list_add(list, s))
	alpm_list_t *list = NULL, *i;
	/* prefetch some strings for usage below, which moves a lot of calls
	 * out of gettext. */
	char const *const str_opt  = _("options");
	char const *const str_file = _("file(s)");
	char const *const str_pkg  = _("package(s)");
	char const *const str_usg  = _("usage");
	char const *const str_opr  = _("operation");

	/* please limit your strings to 80 characters in width */
	if(op == PM_OP_MAIN) {
		printf("%s:  %s <%s> [...]\n", str_usg, myname, str_opr);
		printf(_("operations:\n"));
		printf("    %s {-h --help}\n", myname);
		printf("    %s {-V --version}\n", myname);
		printf("    %s {-D --database} <%s> <%s>\n", myname, str_opt, str_pkg);
		printf("    %s {-F --files}    [%s] [%s]\n", myname, str_opt, str_file);
		printf("    %s {-Q --query}    [%s] [%s]\n", myname, str_opt, str_pkg);
		printf("    %s {-R --remove}   [%s] <%s>\n", myname, str_opt, str_pkg);
		printf("    %s {-S --sync}     [%s] [%s]\n", myname, str_opt, str_pkg);
		printf("    %s {-T --deptest}  [%s] [%s]\n", myname, str_opt, str_pkg);
		printf("    %s {-U --upgrade}  [%s] <%s>\n", myname, str_opt, str_file);
		printf(_("\nuse '%s {-h --help}' with an operation for available options\n"),
				myname);
	} else {
		if(op == PM_OP_REMOVE) {
			printf("%s:  %s {-R --remove} [%s] <%s>\n", str_usg, myname, str_opt, str_pkg);
			printf("%s:\n", str_opt);
			addlist(_("  -c, --cascade        remove packages and all packages that depend on them\n"));
			addlist(_("  -n, --nosave         remove configuration files\n"));
			addlist(_("  -s, --recursive      remove unnecessary dependencies\n"
			          "                       (-ss includes explicitly installed dependencies)\n"));
			addlist(_("  -u, --unneeded       remove unneeded packages\n"));
		} else if(op == PM_OP_UPGRADE) {
			printf("%s:  %s {-U --upgrade} [%s] <%s>\n", str_usg, myname, str_opt, str_file);
			addlist(_("      --needed         do not reinstall up to date packages\n"));
			printf("%s:\n", str_opt);
		} else if(op == PM_OP_QUERY) {
			printf("%s:  %s {-Q --query} [%s] [%s]\n", str_usg, myname, str_opt, str_pkg);
			printf("%s:\n", str_opt);
			addlist(_("  -c, --changelog      view the changelog of a package\n"));
			addlist(_("  -d, --deps           list packages installed as dependencies [filter]\n"));
			addlist(_("  -e, --explicit       list packages explicitly installed [filter]\n"));
			addlist(_("  -g, --groups         view all members of a package group\n"));
			addlist(_("  -i, --info           view package information (-ii for backup files)\n"));
			addlist(_("  -k, --check          check that package files exist (-kk for file properties)\n"));
			addlist(_("  -l, --list           list the files owned by the queried package\n"));
			addlist(_("  -m, --foreign        list installed packages not found in sync db(s) [filter]\n"));
			addlist(_("  -n, --native         list installed packages only found in sync db(s) [filter]\n"));
			addlist(_("  -o, --owns <file>    query the package that owns <file>\n"));
			addlist(_("  -p, --file <package> query a package file instead of the database\n"));
			addlist(_("  -q, --quiet          show less information for query and search\n"));
			addlist(_("  -s, --search <regex> search locally-installed packages for matching strings\n"));
			addlist(_("  -t, --unrequired     list packages not (optionally) required by any\n"
			          "                       package (-tt to ignore optdepends) [filter]\n"));
			addlist(_("  -u, --upgrades       list outdated packages [filter]\n"));
		} else if(op == PM_OP_SYNC) {
			printf("%s:  %s {-S --sync} [%s] [%s]\n", str_usg, myname, str_opt, str_pkg);
			printf("%s:\n", str_opt);
			addlist(_("  -c, --clean          remove old packages from cache directory (-cc for all)\n"));
			addlist(_("  -g, --groups         view all members of a package group\n"
			          "                       (-gg to view all groups and members)\n"));
			addlist(_("  -i, --info           view package information (-ii for extended information)\n"));
			addlist(_("  -l, --list <repo>    view a list of packages in a repo\n"));
			addlist(_("  -q, --quiet          show less information for query and search\n"));
			addlist(_("  -s, --search <regex> search remote repositories for matching strings\n"));
			addlist(_("  -u, --sysupgrade     upgrade installed packages (-uu enables downgrades)\n"));
			addlist(_("  -y, --refresh        download fresh package databases from the server\n"
			          "                       (-yy to force a refresh even if up to date)\n"));
			addlist(_("      --needed         do not reinstall up to date packages\n"));
		} else if(op == PM_OP_DATABASE) {
			printf("%s:  %s {-D --database} <%s> <%s>\n", str_usg, myname, str_opt, str_pkg);
			printf("%s:\n", str_opt);
			addlist(_("      --asdeps         mark packages as non-explicitly installed\n"));
			addlist(_("      --asexplicit     mark packages as explicitly installed\n"));
			addlist(_("  -k, --check          test local database for validity (-kk for sync databases)\n"));
			addlist(_("  -q, --quiet          suppress output of success messages\n"));
		} else if(op == PM_OP_DEPTEST) {
			printf("%s:  %s {-T --deptest} [%s] [%s]\n", str_usg, myname, str_opt, str_pkg);
			printf("%s:\n", str_opt);
		} else if(op == PM_OP_FILES) {
			printf("%s:  %s {-F --files} [%s] [%s]\n", str_usg, myname, str_opt, str_file);
			printf("%s:\n", str_opt);
			addlist(_("  -l, --list           list the files owned by the queried package\n"));
			addlist(_("  -q, --quiet          show less information for query and search\n"));
			addlist(_("  -x, --regex          enable searching using regular expressions\n"));
			addlist(_("  -y, --refresh        download fresh package databases from the server\n"
			          "                       (-yy to force a refresh even if up to date)\n"));
			addlist(_("      --machinereadable\n"
			          "                       produce machine-readable output\n"));
		}
		switch(op) {
			case PM_OP_SYNC:
			case PM_OP_UPGRADE:
				addlist(_("  -w, --downloadonly   download packages but do not install/upgrade anything\n"));
				addlist(_("      --overwrite <glob>\n"
				          "                       overwrite conflicting files (can be used more than once)\n"));
				addlist(_("      --asdeps         install packages as non-explicitly installed\n"));
				addlist(_("      --asexplicit     install packages as explicitly installed\n"));
				addlist(_("      --ignore <pkg>   ignore a package upgrade (can be used more than once)\n"));
				addlist(_("      --ignoregroup <grp>\n"
				          "                       ignore a group upgrade (can be used more than once)\n"));
				__attribute__((fallthrough));
			case PM_OP_REMOVE:
				addlist(_("  -d, --nodeps         skip dependency version checks (-dd to skip all checks)\n"));
				addlist(_("      --assume-installed <package=version>\n"
				          "                       add a virtual package to satisfy dependencies\n"));
				addlist(_("      --dbonly         only modify database entries, not package files\n"));
				addlist(_("      --noprogressbar  do not show a progress bar when downloading files\n"));
				addlist(_("      --noscriptlet    do not execute the install scriptlet if one exists\n"));
				addlist(_("  -p, --print          print the targets instead of performing the operation\n"));
				addlist(_("      --print-format <string>\n"
				          "                       specify how the targets should be printed\n"));
				break;
		}

		addlist(_("  -b, --dbpath <path>  set an alternate database location\n"));
		addlist(_("  -r, --root <path>    set an alternate installation root\n"));
		addlist(_("  -v, --verbose        be verbose\n"));
		addlist(_("      --arch <arch>    set an alternate architecture\n"));
		addlist(_("      --sysroot        operate on a mounted guest system (root-only)\n"));
		addlist(_("      --cachedir <dir> set an alternate package cache location\n"));
		addlist(_("      --hookdir <dir>  set an alternate hook location\n"));
		addlist(_("      --color <when>   colorize the output\n"));
		addlist(_("      --config <path>  set an alternate configuration file\n"));
		addlist(_("      --debug          display debug messages\n"));
		addlist(_("      --gpgdir <path>  set an alternate home directory for GnuPG\n"));
		addlist(_("      --logfile <path> set an alternate log file\n"));
		addlist(_("      --noconfirm      do not ask for any confirmation\n"));
		addlist(_("      --confirm        always ask for confirmation\n"));
		addlist(_("      --disable-download-timeout\n"
		          "                       use relaxed timeouts for download\n"));
	}
	list = alpm_list_msort(list, alpm_list_count(list), options_cmp);
	for(i = list; i; i = alpm_list_next(i)) {
		fputs((const char *)i->data, stdout);
	}
	alpm_list_free(list);
#undef addlist
}

/** Output pacman version and copyright.
 */
static void version(void)
{
	printf("\n");
	printf(" .--.                  Pacman v%s - libalpm v%s\n", PACKAGE_VERSION, alpm_version());
	printf("/ _.-' .-.  .-.  .-.   Copyright (C) 2006-2022 Pacman Development Team\n");
	printf("\\  '-. '-'  '-'  '-'   Copyright (C) 2002-2006 Judd Vinet\n");
	printf(" '--'\n");
	printf(_("                       This program may be freely redistributed under\n"
	         "                       the terms of the GNU General Public License.\n"));
	printf("\n");
}

/** Sets up gettext localization. Safe to call multiple times.
 */
/* Inspired by the monotone function localize_monotone. */
#if defined(ENABLE_NLS)
static void localize(void)
{
	static int init = 0;
	if(!init) {
		setlocale(LC_ALL, "");
		bindtextdomain(PACKAGE, LOCALEDIR);
		textdomain(PACKAGE);
		init = 1;
	}
}
#endif

/** Set user agent environment variable.
 */
static void setuseragent(void)
{
	char agent[100];
	struct utsname un;
	int len;

	uname(&un);
	len = snprintf(agent, 100, "pacman/%s (%s %s) libalpm/%s",
			PACKAGE_VERSION, un.sysname, un.machine, alpm_version());
	if(len >= 100) {
		pm_printf(ALPM_LOG_WARNING, _("HTTP_USER_AGENT truncated\n"));
	}

	setenv("HTTP_USER_AGENT", agent, 0);
}

/** Free the resources.
 *
 * @param ret the return value
 */
static void cleanup(int ret)
{
	remove_soft_interrupt_handler();
	if(config) {
		/* free alpm library resources */
		if(config->handle && alpm_release(config->handle) == -1) {
			pm_printf(ALPM_LOG_ERROR, "error releasing alpm library\n");
		}

		config_free(config);
		config = NULL;
	}

	/* free memory */
	FREELIST(pm_targets);
	console_cursor_show();
	exit(ret);
}

static void invalid_opt(int used, const char *opt1, const char *opt2)
{
	if(used) {
		pm_printf(ALPM_LOG_ERROR,
				_("invalid option: '%s' and '%s' may not be used together\n"),
				opt1, opt2);
		cleanup(1);
	}
}

static int parsearg_util_addlist(alpm_list_t **list)
{
	char *i, *save = NULL;

	for(i = strtok_r(optarg, ",", &save); i; i = strtok_r(NULL, ",", &save)) {
		*list = alpm_list_add(*list, strdup(i));
	}

	return 0;
}

/** Helper function for parsing operation from command-line arguments.
 * @param opt Keycode returned by getopt_long
 * @param dryrun If nonzero, application state is NOT changed
 * @return 0 if opt was handled, 1 if it was not handled
 */
static int parsearg_op(int opt, int dryrun)
{
	switch(opt) {
		/* operations */
		case 'D':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_DATABASE); break;
		case 'F':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_FILES); break;
		case 'Q':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_QUERY); break;
		case 'R':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_REMOVE); break;
		case 'S':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_SYNC); break;
		case 'T':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_DEPTEST); break;
		case 'U':
			if(dryrun) break;
			config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_UPGRADE); break;
		case 'V':
			if(dryrun) break;
			config->version = 1; break;
		case 'h':
			if(dryrun) break;
			config->help = 1; break;
		default:
			return 1;
	}
	return 0;
}

/** Helper functions for parsing command-line arguments.
 * @param opt Keycode returned by getopt_long
 * @return 0 on success, 1 on failure
 */
static int parsearg_global(int opt)
{
	switch(opt) {
		case OP_ARCH:
			config_add_architecture(strdup(optarg));
			break;
		case OP_ASK:
			config->noask = 1;
			config->ask = (unsigned int)atoi(optarg);
			break;
		case OP_CACHEDIR:
			config->cachedirs = alpm_list_add(config->cachedirs, strdup(optarg));
			break;
		case OP_COLOR:
			if(strcmp("never", optarg) == 0) {
				config->color = PM_COLOR_OFF;
			} else if(strcmp("auto", optarg) == 0) {
				config->color = isatty(fileno(stdout)) ? PM_COLOR_ON : PM_COLOR_OFF;
			} else if(strcmp("always", optarg) == 0) {
				config->color = PM_COLOR_ON;
			} else {
				pm_printf(ALPM_LOG_ERROR, _("invalid argument '%s' for %s\n"),
						optarg, "--color");
				return 1;
			}
			enable_colors(config->color);
			break;
		case OP_CONFIG:
			free(config->configfile);
			config->configfile = strndup(optarg, PATH_MAX);
			break;
		case OP_DEBUG:
			/* debug levels are made more 'human readable' than using a raw logmask
			 * here, error and warning are set in config_new, though perhaps a
			 * --quiet option will remove these later */
			if(optarg) {
				unsigned short debug = (unsigned short)atoi(optarg);
				switch(debug) {
					case 2:
						config->logmask |= ALPM_LOG_FUNCTION;
						__attribute__((fallthrough));
					case 1:
						config->logmask |= ALPM_LOG_DEBUG;
						break;
					default:
						pm_printf(ALPM_LOG_ERROR, _("'%s' is not a valid debug level\n"),
								optarg);
						return 1;
				}
			} else {
				config->logmask |= ALPM_LOG_DEBUG;
			}
			/* progress bars get wonky with debug on, shut them off */
			config->noprogressbar = 1;
			break;
		case OP_GPGDIR:
			free(config->gpgdir);
			config->gpgdir = strdup(optarg);
			break;
		case OP_HOOKDIR:
			config->hookdirs = alpm_list_add(config->hookdirs, strdup(optarg));
			break;
		case OP_LOGFILE:
			free(config->logfile);
			config->logfile = strndup(optarg, PATH_MAX);
			break;
		case OP_NOCONFIRM:
			config->noconfirm = 1;
			break;
		case OP_CONFIRM:
			config->noconfirm = 0;
			break;
		case OP_DBPATH:
		case 'b':
			free(config->dbpath);
			config->dbpath = strdup(optarg);
			break;
		case OP_ROOT:
		case 'r':
			free(config->rootdir);
			config->rootdir = strdup(optarg);
			break;
		case OP_SYSROOT:
			free(config->sysroot);
			config->sysroot = strdup(optarg);
			break;
		case OP_DISABLEDLTIMEOUT:
			config->disable_dl_timeout = 1;
			break;
		case OP_VERBOSE:
		case 'v':
			(config->verbose)++;
			break;
		default:
			return 1;
	}
	return 0;
}

static int parsearg_database(int opt)
{
	switch(opt) {
		case OP_ASDEPS:
			config->flags |= ALPM_TRANS_FLAG_ALLDEPS;
			break;
		case OP_ASEXPLICIT:
			config->flags |= ALPM_TRANS_FLAG_ALLEXPLICIT;
			break;
		case OP_CHECK:
		case 'k':
			(config->op_q_check)++;
			break;
		case OP_QUIET:
		case 'q':
			config->quiet = 1;
		break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_database(void)
{
	invalid_opt(config->flags & ALPM_TRANS_FLAG_ALLDEPS
			&& config->flags & ALPM_TRANS_FLAG_ALLEXPLICIT,
			"--asdeps", "--asexplicit");

	if(config->op_q_check) {
		invalid_opt(config->flags & ALPM_TRANS_FLAG_ALLDEPS,
				"--asdeps", "--check");
		invalid_opt(config->flags & ALPM_TRANS_FLAG_ALLEXPLICIT,
				"--asexplicit", "--check");
	}
}

static int parsearg_query(int opt)
{
	switch(opt) {
		case OP_CHANGELOG:
		case 'c':
			config->op_q_changelog = 1;
			break;
		case OP_DEPS:
		case 'd':
			config->op_q_deps = 1;
			break;
		case OP_EXPLICIT:
		case 'e':
			config->op_q_explicit = 1;
			break;
		case OP_GROUPS:
		case 'g':
			(config->group)++;
			break;
		case OP_INFO:
		case 'i':
			(config->op_q_info)++;
			break;
		case OP_CHECK:
		case 'k':
			(config->op_q_check)++;
			break;
		case OP_LIST:
		case 'l':
			config->op_q_list = 1;
			break;
		case OP_FOREIGN:
		case 'm':
			config->op_q_locality |= PKG_LOCALITY_FOREIGN;
			break;
		case OP_NATIVE:
		case 'n':
			config->op_q_locality |= PKG_LOCALITY_NATIVE;
			break;
		case OP_OWNS:
		case 'o':
			config->op_q_owns = 1;
			break;
		case OP_FILE:
		case 'p':
			config->op_q_isfile = 1;
			break;
		case OP_QUIET:
		case 'q':
			config->quiet = 1;
			break;
		case OP_SEARCH:
		case 's':
			config->op_q_search = 1;
			break;
		case OP_UNREQUIRED:
		case 't':
			(config->op_q_unrequired)++;
			break;
		case OP_UPGRADES:
		case 'u':
			config->op_q_upgrade = 1;
			break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_query_display_opts(const char *opname) {
	invalid_opt(config->op_q_changelog, opname, "--changelog");
	invalid_opt(config->op_q_check, opname, "--check");
	invalid_opt(config->op_q_info, opname, "--info");
	invalid_opt(config->op_q_list, opname, "--list");
}

static void checkargs_query_filter_opts(const char *opname) {
	invalid_opt(config->op_q_deps, opname, "--deps");
	invalid_opt(config->op_q_explicit, opname, "--explicit");
	invalid_opt(config->op_q_upgrade, opname, "--upgrade");
	invalid_opt(config->op_q_unrequired, opname, "--unrequired");
	invalid_opt(config->op_q_locality & PKG_LOCALITY_NATIVE, opname, "--native");
	invalid_opt(config->op_q_locality & PKG_LOCALITY_FOREIGN, opname, "--foreign");
}

static void checkargs_query(void)
{
	if(config->op_q_isfile) {
		invalid_opt(config->group, "--file", "--groups");
		invalid_opt(config->op_q_search, "--file", "--search");
		invalid_opt(config->op_q_owns, "--file", "--owns");
	} else if(config->op_q_search) {
		invalid_opt(config->group, "--search", "--groups");
		invalid_opt(config->op_q_owns, "--search", "--owns");
		checkargs_query_display_opts("--search");
		checkargs_query_filter_opts("--search");
	} else if(config->op_q_owns) {
		invalid_opt(config->group, "--owns", "--groups");
		checkargs_query_display_opts("--owns");
		checkargs_query_filter_opts("--owns");
	} else if(config->group) {
		checkargs_query_display_opts("--groups");
	}

	invalid_opt(config->op_q_deps && config->op_q_explicit, "--deps", "--explicit");
	invalid_opt((config->op_q_locality & PKG_LOCALITY_NATIVE) &&
				 (config->op_q_locality & PKG_LOCALITY_FOREIGN),
			"--native", "--foreign");
}

/* options common to -S -R -U */
static int parsearg_trans(int opt)
{
	switch(opt) {
		case OP_NODEPS:
		case 'd':
			if(config->flags & ALPM_TRANS_FLAG_NODEPVERSION) {
				config->flags |= ALPM_TRANS_FLAG_NODEPS;
			} else {
				config->flags |= ALPM_TRANS_FLAG_NODEPVERSION;
			}
			break;
		case OP_DBONLY:
			config->flags |= ALPM_TRANS_FLAG_DBONLY;
			config->flags |= ALPM_TRANS_FLAG_NOSCRIPTLET;
			config->flags |= ALPM_TRANS_FLAG_NOHOOKS;
			break;
		case OP_NOPROGRESSBAR:
			config->noprogressbar = 1;
			break;
		case OP_NOSCRIPTLET:
			config->flags |= ALPM_TRANS_FLAG_NOSCRIPTLET;
			break;
		case OP_PRINT:
		case 'p':
			config->print = 1;
			break;
		case OP_PRINTFORMAT:
			config->print = 1;
			free(config->print_format);
			config->print_format = strdup(optarg);
			break;
		case OP_ASSUMEINSTALLED:
			parsearg_util_addlist(&(config->assumeinstalled));
			break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_trans(void)
{
	if(config->print) {
		invalid_opt(config->flags & ALPM_TRANS_FLAG_DBONLY,
				"--print", "--dbonly");
		invalid_opt(config->flags & ALPM_TRANS_FLAG_NOSCRIPTLET,
				"--print", "--noscriptlet");
	}
}

static int parsearg_remove(int opt)
{
	if(parsearg_trans(opt) == 0) {
		return 0;
	}
	switch(opt) {
		case OP_CASCADE:
		case 'c':
			config->flags |= ALPM_TRANS_FLAG_CASCADE;
			break;
		case OP_NOSAVE:
		case 'n':
			config->flags |= ALPM_TRANS_FLAG_NOSAVE;
			break;
		case OP_RECURSIVE:
		case 's':
			if(config->flags & ALPM_TRANS_FLAG_RECURSE) {
				config->flags |= ALPM_TRANS_FLAG_RECURSEALL;
			} else {
				config->flags |= ALPM_TRANS_FLAG_RECURSE;
			}
			break;
		case OP_UNNEEDED:
		case 'u':
			config->flags |= ALPM_TRANS_FLAG_UNNEEDED;
			break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_remove(void)
{
	checkargs_trans();
	if(config->flags & ALPM_TRANS_FLAG_NOSAVE) {
		invalid_opt(config->print, "--nosave", "--print");
		invalid_opt(config->flags & ALPM_TRANS_FLAG_DBONLY,
				"--nosave", "--dbonly");
	}
}

/* options common to -S -U */
static int parsearg_upgrade(int opt)
{
	if(parsearg_trans(opt) == 0) {
		return 0;
	}
	switch(opt) {
		case OP_OVERWRITE_FILES:
			parsearg_util_addlist(&(config->overwrite_files));
			break;
		case OP_ASDEPS:
			config->flags |= ALPM_TRANS_FLAG_ALLDEPS;
			break;
		case OP_ASEXPLICIT:
			config->flags |= ALPM_TRANS_FLAG_ALLEXPLICIT;
			break;
		case OP_NEEDED:
			config->flags |= ALPM_TRANS_FLAG_NEEDED;
			break;
		case OP_IGNORE:
			parsearg_util_addlist(&(config->ignorepkg));
			break;
		case OP_IGNOREGROUP:
			parsearg_util_addlist(&(config->ignoregrp));
			break;
		case OP_DOWNLOADONLY:
		case 'w':
			config->op_s_downloadonly = 1;
			config->flags |= ALPM_TRANS_FLAG_DOWNLOADONLY;
			config->flags |= ALPM_TRANS_FLAG_NOCONFLICTS;
			break;
		default: return 1;
	}
	return 0;
}

static void checkargs_upgrade(void)
{
	checkargs_trans();
	invalid_opt(config->flags & ALPM_TRANS_FLAG_ALLDEPS
			&& config->flags & ALPM_TRANS_FLAG_ALLEXPLICIT,
			"--asdeps", "--asexplicit");
}

static int parsearg_files(int opt)
{
	if(parsearg_trans(opt) == 0) {
		return 0;
	}
	switch(opt) {
		case OP_LIST:
		case 'l':
			config->op_q_list = 1;
			break;
		case OP_REFRESH:
		case 'y':
			(config->op_s_sync)++;
			break;
		case OP_REGEX:
		case 'x':
			config->op_f_regex = 1;
			break;
		case OP_MACHINEREADABLE:
			config->op_f_machinereadable = 1;
			break;
		case OP_QUIET:
		case 'q':
			config->quiet = 1;
			break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_files(void)
{
	if(config->op_q_search) {
		invalid_opt(config->op_q_list, "--regex", "--list");
	}
}

static int parsearg_sync(int opt)
{
	if(parsearg_upgrade(opt) == 0) {
		return 0;
	}
	switch(opt) {
		case OP_CLEAN:
		case 'c':
			(config->op_s_clean)++;
			break;
		case OP_GROUPS:
		case 'g':
			(config->group)++;
			break;
		case OP_INFO:
		case 'i':
			(config->op_s_info)++;
			break;
		case OP_LIST:
		case 'l':
			config->op_q_list = 1;
			break;
		case OP_QUIET:
		case 'q':
			config->quiet = 1;
			break;
		case OP_SEARCH:
		case 's':
			config->op_s_search = 1;
			break;
		case OP_SYSUPGRADE:
		case 'u':
			(config->op_s_upgrade)++;
			break;
		case OP_REFRESH:
		case 'y':
			(config->op_s_sync)++;
			break;
		default:
			return 1;
	}
	return 0;
}

static void checkargs_sync(void)
{
	checkargs_upgrade();
	if(config->op_s_clean) {
		invalid_opt(config->group, "--clean", "--groups");
		invalid_opt(config->op_s_info, "--clean", "--info");
		invalid_opt(config->op_q_list, "--clean", "--list");
		invalid_opt(config->op_s_sync, "--clean", "--refresh");
		invalid_opt(config->op_s_search, "--clean", "--search");
		invalid_opt(config->op_s_upgrade, "--clean", "--sysupgrade");
		invalid_opt(config->op_s_downloadonly, "--clean", "--downloadonly");
	} else if(config->op_s_info) {
		invalid_opt(config->group, "--info", "--groups");
		invalid_opt(config->op_q_list, "--info", "--list");
		invalid_opt(config->op_s_search, "--info", "--search");
		invalid_opt(config->op_s_upgrade, "--info", "--sysupgrade");
		invalid_opt(config->op_s_downloadonly, "--info", "--downloadonly");
	} else if(config->op_s_search) {
		invalid_opt(config->group, "--search", "--groups");
		invalid_opt(config->op_q_list, "--search", "--list");
		invalid_opt(config->op_s_upgrade, "--search", "--sysupgrade");
		invalid_opt(config->op_s_downloadonly, "--search", "--downloadonly");
	} else if(config->op_q_list) {
		invalid_opt(config->group, "--list", "--groups");
		invalid_opt(config->op_s_upgrade, "--list", "--sysupgrade");
		invalid_opt(config->op_s_downloadonly, "--list", "--downloadonly");
	} else if(config->group) {
		invalid_opt(config->op_s_upgrade, "--groups", "--sysupgrade");
		invalid_opt(config->op_s_downloadonly, "--groups", "--downloadonly");
	}
}

/** Parse command-line arguments for each operation.
 * @param argc argc
 * @param argv argv
 * @return 0 on success, 1 on error
 */
static int parseargs(int argc, char *argv[])
{
	int opt;
	int option_index = 0;
	int result;
	const char *optstring = "DFQRSTUVb:cdefghiklmnopqr:stuvwxy";
	static const struct option opts[] =
	{
		{"database",   no_argument,       0, 'D'},
		{"files",      no_argument,       0, 'F'},
		{"query",      no_argument,       0, 'Q'},
		{"remove",     no_argument,       0, 'R'},
		{"sync",       no_argument,       0, 'S'},
		{"deptest",    no_argument,       0, 'T'}, /* used by makepkg */
		{"upgrade",    no_argument,       0, 'U'},
		{"version",    no_argument,       0, 'V'},
		{"help",       no_argument,       0, 'h'},

		{"dbpath",     required_argument, 0, OP_DBPATH},
		{"cascade",    no_argument,       0, OP_CASCADE},
		{"changelog",  no_argument,       0, OP_CHANGELOG},
		{"clean",      no_argument,       0, OP_CLEAN},
		{"nodeps",     no_argument,       0, OP_NODEPS},
		{"deps",       no_argument,       0, OP_DEPS},
		{"explicit",   no_argument,       0, OP_EXPLICIT},
		{"groups",     no_argument,       0, OP_GROUPS},
		{"info",       no_argument,       0, OP_INFO},
		{"check",      no_argument,       0, OP_CHECK},
		{"list",       no_argument,       0, OP_LIST},
		{"foreign",    no_argument,       0, OP_FOREIGN},
		{"native",     no_argument,       0, OP_NATIVE},
		{"nosave",     no_argument,       0, OP_NOSAVE},
		{"owns",       no_argument,       0, OP_OWNS},
		{"file",       no_argument,       0, OP_FILE},
		{"print",      no_argument,       0, OP_PRINT},
		{"quiet",      no_argument,       0, OP_QUIET},
		{"root",       required_argument, 0, OP_ROOT},
		{"sysroot",    required_argument, 0, OP_SYSROOT},
		{"recursive",  no_argument,       0, OP_RECURSIVE},
		{"search",     no_argument,       0, OP_SEARCH},
		{"regex",      no_argument,       0, OP_REGEX},
		{"machinereadable",      no_argument,       0, OP_MACHINEREADABLE},
		{"unrequired", no_argument,       0, OP_UNREQUIRED},
		{"upgrades",   no_argument,       0, OP_UPGRADES},
		{"sysupgrade", no_argument,       0, OP_SYSUPGRADE},
		{"unneeded",   no_argument,       0, OP_UNNEEDED},
		{"verbose",    no_argument,       0, OP_VERBOSE},
		{"downloadonly", no_argument,     0, OP_DOWNLOADONLY},
		{"refresh",    no_argument,       0, OP_REFRESH},
		{"noconfirm",  no_argument,       0, OP_NOCONFIRM},
		{"confirm",    no_argument,       0, OP_CONFIRM},
		{"config",     required_argument, 0, OP_CONFIG},
		{"ignore",     required_argument, 0, OP_IGNORE},
		{"assume-installed",     required_argument, 0, OP_ASSUMEINSTALLED},
		{"debug",      optional_argument, 0, OP_DEBUG},
		{"force",      no_argument,       0, OP_FORCE},
		{"overwrite",  required_argument, 0, OP_OVERWRITE_FILES},
		{"noprogressbar", no_argument,    0, OP_NOPROGRESSBAR},
		{"noscriptlet", no_argument,      0, OP_NOSCRIPTLET},
		{"ask",        required_argument, 0, OP_ASK},
		{"cachedir",   required_argument, 0, OP_CACHEDIR},
		{"hookdir",    required_argument, 0, OP_HOOKDIR},
		{"asdeps",     no_argument,       0, OP_ASDEPS},
		{"logfile",    required_argument, 0, OP_LOGFILE},
		{"ignoregroup", required_argument, 0, OP_IGNOREGROUP},
		{"needed",     no_argument,       0, OP_NEEDED},
		{"asexplicit",     no_argument,   0, OP_ASEXPLICIT},
		{"arch",       required_argument, 0, OP_ARCH},
		{"print-format", required_argument, 0, OP_PRINTFORMAT},
		{"gpgdir",     required_argument, 0, OP_GPGDIR},
		{"dbonly",     no_argument,       0, OP_DBONLY},
		{"color",      required_argument, 0, OP_COLOR},
		{"disable-download-timeout", no_argument, 0, OP_DISABLEDLTIMEOUT},
		{0, 0, 0, 0}
	};

	/* parse operation */
	while((opt = getopt_long(argc, argv, optstring, opts, &option_index)) != -1) {
		if(opt == 0) {
			continue;
		} else if(opt == '?') {
			/* unknown option, getopt printed an error */
			return 1;
		}
		parsearg_op(opt, 0);
	}

	if(config->op == 0) {
		pm_printf(ALPM_LOG_ERROR, _("only one operation may be used at a time\n"));
		return 1;
	}
	if(config->help) {
		usage(config->op, mbasename(argv[0]));
		cleanup(0);
	}
	if(config->version) {
		version();
		cleanup(0);
	}

	/* parse all other options */
	optind = 1;
	while((opt = getopt_long(argc, argv, optstring, opts, &option_index)) != -1) {
		if(opt == 0) {
			continue;
		} else if(opt == '?') {
			/* this should have failed during first pass already */
			return 1;
		} else if(parsearg_op(opt, 1) == 0) {
			/* opt is an operation */
			continue;
		}

		switch(config->op) {
			case PM_OP_DATABASE:
				result = parsearg_database(opt);
				break;
			case PM_OP_QUERY:
				result = parsearg_query(opt);
				break;
			case PM_OP_REMOVE:
				result = parsearg_remove(opt);
				break;
			case PM_OP_SYNC:
				result = parsearg_sync(opt);
				break;
			case PM_OP_UPGRADE:
				result = parsearg_upgrade(opt);
				break;
			case PM_OP_FILES:
				result = parsearg_files(opt);
				break;
			case PM_OP_DEPTEST:
			default:
				result = 1;
				break;
		}
		if(result == 0) {
			continue;
		}

		/* fall back to global options */
		result = parsearg_global(opt);
		if(result != 0) {
			/* global option parsing failed, abort */
			if(opt < OP_LONG_FLAG_MIN) {
				pm_printf(ALPM_LOG_ERROR, _("invalid option '-%c'\n"), opt);
			} else {
				pm_printf(ALPM_LOG_ERROR, _("invalid option '--%s'\n"),
						opts[option_index].name);
			}
			return result;
		}
	}

	while(optind < argc) {
		/* add the target to our target array */
		pm_targets = alpm_list_add(pm_targets, strdup(argv[optind]));
		optind++;
	}

	switch(config->op) {
		case PM_OP_DATABASE:
			checkargs_database();
			break;
		case PM_OP_DEPTEST:
			/* no conflicting options */
			break;
		case PM_OP_SYNC:
			checkargs_sync();
			break;
		case PM_OP_QUERY:
			checkargs_query();
			break;
		case PM_OP_REMOVE:
			checkargs_remove();
			break;
		case PM_OP_UPGRADE:
			checkargs_upgrade();
			break;
		case PM_OP_FILES:
			checkargs_files();
			break;
		default:
			break;
	}

	return 0;
}

/** Print command line to logfile.
 * @param argc
 * @param argv
 */
static void cl_to_log(int argc, char *argv[])
{
	char *cl_text = arg_to_string(argc, argv);
	if(cl_text) {
		alpm_logaction(config->handle, PACMAN_CALLER_PREFIX,
				"Running '%s'\n", cl_text);
		free(cl_text);
	}
}

/** Main function.
 * @param argc
 * @param argv
 * @return A return code indicating success, failure, etc.
 */
int main(int argc, char *argv[])
{
	int ret = 0;
	uid_t myuid = getuid();

	console_cursor_hide();
	install_segv_handler();

	/* i18n init */
#if defined(ENABLE_NLS)
	localize();
#endif

	/* set user agent for downloading */
	setuseragent();

	/* init config data */
	if(!(config = config_new())) {
		/* config_new prints the appropriate error message */
		cleanup(1);
	}

	install_soft_interrupt_handler();

	if(!isatty(fileno(stdout))) {
		/* disable progressbar if the output is redirected */
		config->noprogressbar = 1;
	} else {
		/* install signal handler to update output width */
		install_winch_handler();
	}

	/* Priority of options:
	 * 1. command line
	 * 2. config file
	 * 3. compiled-in defaults
	 * However, we have to parse the command line first because a config file
	 * location can be specified here, so we need to make sure we prefer these
	 * options over the config file coming second.
	 */

	/* parse the command line */
	ret = parseargs(argc, argv);
	if(ret != 0) {
		cleanup(ret);
	}

	/* check if we have sufficient permission for the requested operation */
	if(myuid > 0 && needs_root()) {
		pm_printf(ALPM_LOG_ERROR, _("you cannot perform this operation unless you are root.\n"));
		cleanup(EXIT_FAILURE);
	}

	/* we support reading targets from stdin if a cmdline parameter is '-' */
	if(alpm_list_find_str(pm_targets, "-")) {
		if(!isatty(fileno(stdin))) {
			int target_found = 0;
			char *vdata, *line = NULL;
			size_t line_size = 0;
			ssize_t nread;

			/* remove the '-' from the list */
			pm_targets = alpm_list_remove_str(pm_targets, "-", &vdata);
			free(vdata);

			while((nread = getline(&line, &line_size, stdin)) != -1) {
				if(line[nread - 1] == '\n') {
					/* remove trailing newline */
					line[nread - 1] = '\0';
				}
				if(line[0] == '\0') {
					/* skip empty lines */
					continue;
				}
				if(!alpm_list_append_strdup(&pm_targets, line)) {
					break;
				}
				target_found = 1;
			}
			free(line);

			if(ferror(stdin)) {
				pm_printf(ALPM_LOG_ERROR,
						_("failed to read arguments from stdin: (%s)\n"), strerror(errno));
				cleanup(EXIT_FAILURE);
			}

			if(!freopen(ctermid(NULL), "r", stdin)) {
				pm_printf(ALPM_LOG_ERROR, _("failed to reopen stdin for reading: (%s)\n"),
						strerror(errno));
			}

			if(!target_found) {
				pm_printf(ALPM_LOG_ERROR, _("argument '-' specified with empty stdin\n"));
				cleanup(1);
			}
		} else {
			/* do not read stdin from terminal */
			pm_printf(ALPM_LOG_ERROR, _("argument '-' specified without input on stdin\n"));
			cleanup(1);
		}
	}

	if(config->sysroot && (chroot(config->sysroot) != 0 || chdir("/") != 0)) {
		pm_printf(ALPM_LOG_ERROR,
				_("chroot to '%s' failed: (%s)\n"), config->sysroot, strerror(errno));
		cleanup(EXIT_FAILURE);
	}

	pm_printf(ALPM_LOG_DEBUG, "pacman v%s - libalpm v%s\n", PACKAGE_VERSION, alpm_version());

	/* parse the config file */
	ret = parseconfig(config->configfile);
	if(ret != 0) {
		cleanup(ret);
	}

	/* noask is meant to be non-interactive */
	if(config->noask) {
		config->noconfirm = 1;
	}

	/* set up the print operations */
	if(config->print && !config->op_s_clean) {
		config->noconfirm = 1;
		config->flags |= ALPM_TRANS_FLAG_NOCONFLICTS;
		config->flags |= ALPM_TRANS_FLAG_NOLOCK;
		/* Display only errors */
		config->logmask &= ~ALPM_LOG_WARNING;
	}

	if(config->verbose > 0) {
		alpm_list_t *j;
		printf("Root      : %s\n", alpm_option_get_root(config->handle));
		printf("Conf File : %s\n", config->configfile);
		printf("DB Path   : %s\n", alpm_option_get_dbpath(config->handle));
		printf("Cache Dirs: ");
		for(j = alpm_option_get_cachedirs(config->handle); j; j = alpm_list_next(j)) {
			printf("%s  ", (const char *)j->data);
		}
		printf("\n");
		printf("Hook Dirs : ");
		for(j = alpm_option_get_hookdirs(config->handle); j; j = alpm_list_next(j)) {
			printf("%s  ", (const char *)j->data);
		}
		printf("\n");
		printf("Lock File : %s\n", alpm_option_get_lockfile(config->handle));
		printf("Log File  : %s\n", alpm_option_get_logfile(config->handle));
		printf("GPG Dir   : %s\n", alpm_option_get_gpgdir(config->handle));
		list_display("Targets   :", pm_targets, 0);
	}

	/* Log command line */
	if(needs_root()) {
		cl_to_log(argc, argv);
	}

	/* start the requested operation */
	switch(config->op) {
		case PM_OP_DATABASE:
			ret = pacman_database(pm_targets);
			break;
		case PM_OP_REMOVE:
			ret = pacman_remove(pm_targets);
			break;
		case PM_OP_UPGRADE:
			ret = pacman_upgrade(pm_targets);
			break;
		case PM_OP_QUERY:
			ret = pacman_query(pm_targets);
			break;
		case PM_OP_SYNC:
			ret = pacman_sync(pm_targets);
			break;
		case PM_OP_DEPTEST:
			ret = pacman_deptest(pm_targets);
			break;
		case PM_OP_FILES:
			ret = pacman_files(pm_targets);
			break;
		default:
			pm_printf(ALPM_LOG_ERROR, _("no operation specified (use -h for help)\n"));
			ret = EXIT_FAILURE;
	}

	cleanup(ret);
	/* not reached */
	return EXIT_SUCCESS;
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//Query.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////
#define LOCAL_PREFIX "local/"

/* check if filename exists in PATH */
static int search_path(char **filename, struct stat *bufptr)
{
	char *envpath, *envpathsplit, *path, *fullname;
	size_t flen;

	if((envpath = getenv("PATH")) == NULL) {
		return -1;
	}
	if((envpath = envpathsplit = strdup(envpath)) == NULL) {
		return -1;
	}

	flen = strlen(*filename);

	while((path = strsep(&envpathsplit, ":")) != NULL) {
		size_t plen = strlen(path);

		/* strip the trailing slash if one exists */
		while(path[plen - 1] == '/') {
			path[--plen] = '\0';
		}

		fullname = malloc(plen + flen + 2);
		if(!fullname) {
			free(envpath);
			return -1;
		}
		sprintf(fullname, "%s/%s", path, *filename);

		if(lstat(fullname, bufptr) == 0) {
			free(*filename);
			*filename = fullname;
			free(envpath);
			return 0;
		}
		free(fullname);
	}
	free(envpath);
	return -1;
}

static void print_query_fileowner(const char *filename, alpm_pkg_t *info)
{
	if(!config->quiet) {
		const colstr_t *colstr = &config->colstr;
		printf(_("%s is owned by %s%s %s%s%s\n"), filename, colstr->title,
				alpm_pkg_get_name(info), colstr->version, alpm_pkg_get_version(info),
				colstr->nocolor);
	} else {
		printf("%s\n", alpm_pkg_get_name(info));
	}
}

/** Resolve the canonicalized absolute path of a symlink.
 * @param path path to resolve
 * @param resolved_path destination for the resolved path, will be malloc'd if
 * NULL
 * @return the resolved path
 */
static char *lrealpath(const char *path, char *resolved_path)
{
	const char *bname = mbasename(path);
	char *rpath = NULL, *dname = NULL;
	int success = 0;

	if(strcmp(bname, ".") == 0 || strcmp(bname, "..") == 0) {
		/* the entire path needs to be resolved */
		return realpath(path, resolved_path);
	}

	if(!(dname = mdirname(path))) {
		goto cleanup;
	}
	if(!(rpath = realpath(dname, NULL))) {
		goto cleanup;
	}
	if(!resolved_path) {
		if(!(resolved_path = malloc(strlen(rpath) + strlen(bname) + 2))) {
			goto cleanup;
		}
	}

	strcpy(resolved_path, rpath);
	if(resolved_path[strlen(resolved_path) - 1] != '/') {
		strcat(resolved_path, "/");
	}
	strcat(resolved_path, bname);
	success = 1;

cleanup:
	free(dname);
	free(rpath);

	return (success ? resolved_path : NULL);
}

static int query_fileowner(alpm_list_t *targets)
{
	int ret = 0;
	const char *root = alpm_option_get_root(config->handle);
	size_t rootlen = strlen(root);
	alpm_list_t *t;
	alpm_db_t *db_local;
	alpm_list_t *packages;

	/* This code is here for safety only */
	if(targets == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("no file was specified for --owns\n"));
		return 1;
	}

	db_local = alpm_get_localdb(config->handle);
	packages = alpm_db_get_pkgcache(db_local);

	for(t = targets; t; t = alpm_list_next(t)) {
		char *filename = NULL;
		char rpath[PATH_MAX], *rel_path;
		struct stat buf;
		alpm_list_t *i;
		size_t len;
		unsigned int found = 0;
		int is_dir = 0, is_missing = 0;

		if((filename = strdup(t->data)) == NULL) {
			goto targcleanup;
		}

		if(strcmp(filename, "") == 0) {
			pm_printf(ALPM_LOG_ERROR, _("empty string passed to file owner query\n"));
			goto targcleanup;
		}

		/* trailing '/' causes lstat to dereference directory symlinks */
		len = strlen(filename) - 1;
		while(len > 0 && filename[len] == '/') {
			filename[len--] = '\0';
			/* If a non-dir file exists, S_ISDIR will correct this later. */
			is_dir = 1;
		}

		if(lstat(filename, &buf) == -1) {
			is_missing = 1;
			/* if it is not a path but a program name, then check in PATH */
			if ((strchr(filename, '/') == NULL) && (search_path(&filename, &buf) == 0)) {
				is_missing = 0;
			}
		}

		if(!lrealpath(filename, rpath)) {
			/* Can't canonicalize path, try to proceed anyway */
			strcpy(rpath, filename);
		}

		if(strncmp(rpath, root, rootlen) != 0) {
			/* file is outside root, we know nothing can own it */
			pm_printf(ALPM_LOG_ERROR, _("No package owns %s\n"), filename);
			goto targcleanup;
		}

		rel_path = rpath + rootlen;

		if((is_missing && is_dir) || (!is_missing && (is_dir = S_ISDIR(buf.st_mode)))) {
			size_t rlen = strlen(rpath);
			if(rlen + 2 >= PATH_MAX) {
					pm_printf(ALPM_LOG_ERROR, _("path too long: %s/\n"), rpath);
					goto targcleanup;
			}
			strcat(rpath + rlen, "/");
		}

		for(i = packages; i && (!found || is_dir); i = alpm_list_next(i)) {
			if(alpm_filelist_contains(alpm_pkg_get_files(i->data), rel_path)) {
				print_query_fileowner(rpath, i->data);
				found = 1;
			}
		}
		if(!found) {
			pm_printf(ALPM_LOG_ERROR, _("No package owns %s\n"), filename);
		}

targcleanup:
		if(!found) {
			ret++;
		}
		free(filename);
	}

	return ret;
}

/* search the local database for a matching package */
static int query_search(alpm_list_t *targets)
{
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	int ret = dump_pkg_search(db_local, targets, 0);
	if(ret == -1) {
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, "search failed: %s\n", alpm_strerror(err));
		return 1;
	}

	return ret;

}

static unsigned short pkg_get_locality(alpm_pkg_t *pkg)
{
	const char *pkgname = alpm_pkg_get_name(pkg);
	alpm_list_t *j;
	alpm_list_t *sync_dbs = alpm_get_syncdbs(config->handle);

	for(j = sync_dbs; j; j = alpm_list_next(j)) {
		if(alpm_db_get_pkg(j->data, pkgname)) {
			return PKG_LOCALITY_NATIVE;
		}
	}
	return PKG_LOCALITY_FOREIGN;
}

static int is_unrequired(alpm_pkg_t *pkg, unsigned short level)
{
	alpm_list_t *requiredby = alpm_pkg_compute_requiredby(pkg);
	if(requiredby == NULL) {
		if(level == 1) {
			requiredby = alpm_pkg_compute_optionalfor(pkg);
		}
		if(requiredby == NULL) {
			return 1;
		}
	}
	FREELIST(requiredby);
	return 0;
}

static int filter(alpm_pkg_t *pkg)
{
	/* check if this package was explicitly installed */
	if(config->op_q_explicit &&
			alpm_pkg_get_reason(pkg) != ALPM_PKG_REASON_EXPLICIT) {
		return 0;
	}
	/* check if this package was installed as a dependency */
	if(config->op_q_deps &&
			alpm_pkg_get_reason(pkg) != ALPM_PKG_REASON_DEPEND) {
		return 0;
	}
	/* check if this pkg is or isn't in a sync DB */
	if(config->op_q_locality && config->op_q_locality != pkg_get_locality(pkg)) {
		return 0;
	}
	/* check if this pkg is unrequired */
	if(config->op_q_unrequired && !is_unrequired(pkg, config->op_q_unrequired)) {
		return 0;
	}
	/* check if this pkg is outdated */
	if(config->op_q_upgrade && (alpm_sync_get_new_version(pkg,
					alpm_get_syncdbs(config->handle)) == NULL)) {
		return 0;
	}
	return 1;
}

static int display(alpm_pkg_t *pkg)
{
	int ret = 0;

	if(config->op_q_info) {
		if(config->op_q_isfile) {
			dump_pkg_full(pkg, 0);
		} else {
			dump_pkg_full(pkg, config->op_q_info > 1);
		}
	}
	if(config->op_q_list) {
		dump_pkg_files(pkg, config->quiet);
	}
	if(config->op_q_changelog) {
		dump_pkg_changelog(pkg);
	}
	if(config->op_q_check) {
		if(config->op_q_check == 1) {
			ret = check_pkg_fast(pkg);
		} else {
			ret = check_pkg_full(pkg);
		}
	}
	if(!config->op_q_info && !config->op_q_list
			&& !config->op_q_changelog && !config->op_q_check) {
		if(!config->quiet) {
			const colstr_t *colstr = &config->colstr;
			printf("%s%s %s%s%s", colstr->title, alpm_pkg_get_name(pkg),
					colstr->version, alpm_pkg_get_version(pkg), colstr->nocolor);

			if(config->op_q_upgrade) {
				int usage;
				alpm_pkg_t *newpkg = alpm_sync_get_new_version(pkg, alpm_get_syncdbs(config->handle));
				alpm_db_t *db = alpm_pkg_get_db(newpkg);
				alpm_db_get_usage(db, &usage);
				
				printf(" -> %s%s%s", colstr->version, alpm_pkg_get_version(newpkg), colstr->nocolor);

				if(alpm_pkg_should_ignore(config->handle, pkg) || !(usage & ALPM_DB_USAGE_UPGRADE)) {
					printf(" %s", _("[ignored]"));
				}
			}

			printf("\n");
		} else {
			printf("%s\n", alpm_pkg_get_name(pkg));
		}
	}
	return ret;
}

static int query_group(alpm_list_t *targets)
{
	alpm_list_t *i, *j;
	const char *grpname = NULL;
	int ret = 0;
	alpm_db_t *db_local = alpm_get_localdb(config->handle);

	if(targets == NULL) {
		for(j = alpm_db_get_groupcache(db_local); j; j = alpm_list_next(j)) {
			alpm_group_t *grp = j->data;
			const alpm_list_t *p;

			for(p = grp->packages; p; p = alpm_list_next(p)) {
				alpm_pkg_t *pkg = p->data;
				if(!filter(pkg)) {
					continue;
				}
				printf("%s %s\n", grp->name, alpm_pkg_get_name(pkg));
			}
		}
	} else {
		for(i = targets; i; i = alpm_list_next(i)) {
			alpm_group_t *grp;
			grpname = i->data;
			grp = alpm_db_get_group(db_local, grpname);
			if(grp) {
				const alpm_list_t *p;
				for(p = grp->packages; p; p = alpm_list_next(p)) {
					if(!filter(p->data)) {
						continue;
					}
					if(!config->quiet) {
						printf("%s %s\n", grpname,
								alpm_pkg_get_name(p->data));
					} else {
						printf("%s\n", alpm_pkg_get_name(p->data));
					}
				}
			} else {
				pm_printf(ALPM_LOG_ERROR, _("group '%s' was not found\n"), grpname);
				ret++;
			}
		}
	}
	return ret;
}

int pacman_query(alpm_list_t *targets)
{
	int ret = 0;
	int match = 0;
	alpm_list_t *i;
	alpm_pkg_t *pkg = NULL;
	alpm_db_t *db_local;

	/* First: operations that do not require targets */

	/* search for a package */
	if(config->op_q_search) {
		ret = query_search(targets);
		return ret;
	}

	/* looking for groups */
	if(config->group) {
		ret = query_group(targets);
		return ret;
	}

	if(config->op_q_locality || config->op_q_upgrade) {
		if(check_syncdbs(1, 1)) {
			return 1;
		}
	}

	db_local = alpm_get_localdb(config->handle);

	/* operations on all packages in the local DB
	 * valid: no-op (plain -Q), list, info, check
	 * invalid: isfile, owns */
	if(targets == NULL) {
		if(config->op_q_isfile || config->op_q_owns) {
			pm_printf(ALPM_LOG_ERROR, _("no targets specified (use -h for help)\n"));
			return 1;
		}

		for(i = alpm_db_get_pkgcache(db_local); i; i = alpm_list_next(i)) {
			pkg = i->data;
			if(filter(pkg)) {
				int value = display(pkg);
				if(value != 0) {
					ret = 1;
				}
				match = 1;
			}
		}
		if(!match) {
			ret = 1;
		}
		return ret;
	}

	/* Second: operations that require target(s) */

	/* determine the owner of a file */
	if(config->op_q_owns) {
		ret = query_fileowner(targets);
		return ret;
	}

	/* operations on named packages in the local DB
	 * valid: no-op (plain -Q), list, info, check */
	for(i = targets; i; i = alpm_list_next(i)) {
		const char *strname = i->data;

		/* strip leading part of "local/pkgname" */
		if(strncmp(strname, LOCAL_PREFIX, strlen(LOCAL_PREFIX)) == 0) {
			strname += strlen(LOCAL_PREFIX);
		}

		if(config->op_q_isfile) {
			alpm_pkg_load(config->handle, strname, 1, 0, &pkg);

			if(pkg == NULL) {
				pm_printf(ALPM_LOG_ERROR,
						_("could not load package '%s': %s\n"), strname,
						alpm_strerror(alpm_errno(config->handle)));
			}
		} else {
			pkg = alpm_db_get_pkg(db_local, strname);
			if(pkg == NULL) {
				pkg = alpm_find_satisfier(alpm_db_get_pkgcache(db_local), strname);
			}

			if(pkg == NULL) {
				pm_printf(ALPM_LOG_ERROR,
						_("package '%s' was not found\n"), strname);
				if(!config->op_q_isfile && access(strname, R_OK) == 0) {
					pm_printf(ALPM_LOG_WARNING,
							_("'%s' is a file, you might want to use %s.\n"),
							strname, "-p/--file");
				}
			}
		}

		if(pkg == NULL) {
			ret = 1;
			continue;
		}

		if(filter(pkg)) {
			int value = display(pkg);
			if(value != 0) {
				ret = 1;
			}
			match = 1;
		}

		if(config->op_q_isfile) {
			alpm_pkg_free(pkg);
			pkg = NULL;
		}
	}

	if(!match) {
		ret = 1;
	}

	return ret;
}

///////////////////////////////////////////////////
///////////////////////////////////////////////////
//sync.c
///////////////////////////////////////////////////
///////////////////////////////////////////////////
static int unlink_verbose(const char *pathname, int ignore_missing)
{
	int ret = unlink(pathname);
	if(ret) {
		if(ignore_missing && errno == ENOENT) {
			ret = 0;
		} else {
			pm_printf(ALPM_LOG_ERROR, _("could not remove %s: %s\n"),
					pathname, strerror(errno));
		}
	}
	return ret;
}

static int sync_cleandb(const char *dbpath)
{
	DIR *dir;
	struct dirent *ent;
	alpm_list_t *syncdbs;
	int ret = 0;

	dir = opendir(dbpath);
	if(dir == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("could not access database directory\n"));
		return 1;
	}

	syncdbs = alpm_get_syncdbs(config->handle);

	rewinddir(dir);
	/* step through the directory one file at a time */
	while((ent = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		struct stat buf;
		int found = 0;
		const char *dname = ent->d_name;
		char *dbname;
		size_t len;
		alpm_list_t *i;

		if(strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0) {
			continue;
		}

		/* build the full path */
		snprintf(path, PATH_MAX, "%s%s", dbpath, dname);

		/* remove all non-skipped directories and non-database files */
		if(stat(path, &buf) == -1) {
			pm_printf(ALPM_LOG_ERROR, _("could not remove %s: %s\n"),
					path, strerror(errno));
		}
		if(S_ISDIR(buf.st_mode)) {
			if(rmrf(path)) {
				pm_printf(ALPM_LOG_ERROR, _("could not remove %s: %s\n"),
						path, strerror(errno));
			}
			continue;
		}

		len = strlen(dname);
		if(len > 3 && strcmp(dname + len - 3, ".db") == 0) {
			dbname = strndup(dname, len - 3);
		} else if(len > 7 && strcmp(dname + len - 7, ".db.sig") == 0) {
			dbname = strndup(dname, len - 7);
		} else if(len > 6 && strcmp(dname + len - 6, ".files") == 0) {
			dbname = strndup(dname, len - 6);
		} else if(len > 10 && strcmp(dname + len - 10, ".files.sig") == 0) {
			dbname = strndup(dname, len - 10);
		} else {
			ret += unlink_verbose(path, 0);
			continue;
		}

		for(i = syncdbs; i && !found; i = alpm_list_next(i)) {
			alpm_db_t *db = i->data;
			found = !strcmp(dbname, alpm_db_get_name(db));
		}

		/* We have a file that doesn't match any syncdb. */
		if(!found) {
			ret += unlink_verbose(path, 0);
		}

		free(dbname);
	}
	closedir(dir);
	return ret;
}

static int sync_cleandb_all(void)
{
	const char *dbpath;
	char *syncdbpath;
	int ret = 0;

	dbpath = alpm_option_get_dbpath(config->handle);
	printf(_("Database directory: %s\n"), dbpath);
	if(!yesno(_("Do you want to remove unused repositories?"))) {
		return 0;
	}
	printf(_("removing unused sync repositories...\n"));

	if(asprintf(&syncdbpath, "%s%s", dbpath, "sync/") < 0) {
		ret += 1;
		return ret;
	}
	ret += sync_cleandb(syncdbpath);
	free(syncdbpath);

	return ret;
}

static int sync_cleancache(int level)
{
	alpm_list_t *i;
	alpm_list_t *sync_dbs = alpm_get_syncdbs(config->handle);
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	alpm_list_t *cachedirs = alpm_option_get_cachedirs(config->handle);
	int ret = 0;

	if(!config->cleanmethod) {
		/* default to KeepInstalled if user did not specify */
		config->cleanmethod = PM_CLEAN_KEEPINST;
	}

	if(level == 1) {
		printf(_("Packages to keep:\n"));
		if(config->cleanmethod & PM_CLEAN_KEEPINST) {
			printf(_("  All locally installed packages\n"));
		}
		if(config->cleanmethod & PM_CLEAN_KEEPCUR) {
			printf(_("  All current sync database packages\n"));
		}
	}
	printf("\n");

	for(i = cachedirs; i; i = alpm_list_next(i)) {
		const char *cachedir = i->data;
		DIR *dir;
		struct dirent *ent;

		printf(_("Cache directory: %s\n"), (const char *)i->data);

		if(level == 1) {
			if(!yesno(_("Do you want to remove all other packages from cache?"))) {
				printf("\n");
				continue;
			}
			printf(_("removing old packages from cache...\n"));
		} else {
			if(!noyes(_("Do you want to remove ALL files from cache?"))) {
				printf("\n");
				continue;
			}
			printf(_("removing all files from cache...\n"));
		}

		dir = opendir(cachedir);
		if(dir == NULL) {
			pm_printf(ALPM_LOG_ERROR,
					_("could not access cache directory %s\n"), cachedir);
			ret++;
			continue;
		}

		rewinddir(dir);
		/* step through the directory one file at a time */
		while((ent = readdir(dir)) != NULL) {
			char path[PATH_MAX];
			int delete = 1;
			alpm_pkg_t *localpkg = NULL, *pkg = NULL;
			const char *local_name, *local_version;

			if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
				continue;
			}

			if(level <= 1) {
				static const char *const glob_skips[] = {
					/* skip signature files - they are removed with their package file */
					"*.sig",
					/* skip package databases within the cache directory */
					"*.db*", "*.files*",
					/* skip source packages within the cache directory */
					"*.src.tar.*"
				};
				size_t j;

				for(j = 0; j < ARRAYSIZE(glob_skips); j++) {
					if(fnmatch(glob_skips[j], ent->d_name, 0) == 0) {
						delete = 0;
						break;
					}
				}
				if(delete == 0) {
					continue;
				}
			}

			/* build the full filepath */
			snprintf(path, PATH_MAX, "%s%s", cachedir, ent->d_name);

			/* short circuit for removing all files from cache */
			if(level > 1) {
				ret += unlink_verbose(path, 0);
				continue;
			}

			/* attempt to load the file as a package. if we cannot load the file,
			 * simply skip it and move on. we don't need a full load of the package,
			 * just the metadata. */
			if(alpm_pkg_load(config->handle, path, 0, 0, &localpkg) != 0) {
				pm_printf(ALPM_LOG_DEBUG, "skipping %s, could not load as package\n",
						path);
				continue;
			}
			local_name = alpm_pkg_get_name(localpkg);
			local_version = alpm_pkg_get_version(localpkg);

			if(config->cleanmethod & PM_CLEAN_KEEPINST) {
				/* check if this package is in the local DB */
				pkg = alpm_db_get_pkg(db_local, local_name);
				if(pkg != NULL && alpm_pkg_vercmp(local_version,
							alpm_pkg_get_version(pkg)) == 0) {
					/* package was found in local DB and version matches, keep it */
					pm_printf(ALPM_LOG_DEBUG, "package %s-%s found in local db\n",
							local_name, local_version);
					delete = 0;
				}
			}
			if(config->cleanmethod & PM_CLEAN_KEEPCUR) {
				alpm_list_t *j;
				/* check if this package is in a sync DB */
				for(j = sync_dbs; j && delete; j = alpm_list_next(j)) {
					alpm_db_t *db = j->data;
					pkg = alpm_db_get_pkg(db, local_name);
					if(pkg != NULL && alpm_pkg_vercmp(local_version,
								alpm_pkg_get_version(pkg)) == 0) {
						/* package was found in a sync DB and version matches, keep it */
						pm_printf(ALPM_LOG_DEBUG, "package %s-%s found in sync db\n",
								local_name, local_version);
						delete = 0;
					}
				}
			}
			/* free the local file package */
			alpm_pkg_free(localpkg);

			if(delete) {
				size_t pathlen = strlen(path);
				ret += unlink_verbose(path, 0);
				/* unlink a signature file if present too */
				if(PATH_MAX - 5 >= pathlen) {
					strcpy(path + pathlen, ".sig");
					ret += unlink_verbose(path, 1);
				}
			}
		}
		closedir(dir);
		printf("\n");
	}

	return ret;
}

/* search the sync dbs for a matching package */
static int sync_search(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i;
	int found = 0;

	for(i = syncs; i; i = alpm_list_next(i)) {
		alpm_db_t *db = i->data;
		int ret = dump_pkg_search(db, targets, 1);

		if(ret == -1) {
			alpm_errno_t err = alpm_errno(config->handle);
			pm_printf(ALPM_LOG_ERROR, "search failed: %s\n", alpm_strerror(err));
			return 1;
		}

		found += !ret;
	}

	return (found == 0);
}

static int sync_group(int level, alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i, *j, *k, *s = NULL;
	int ret = 0;

	if(targets) {
		size_t found;
		for(i = targets; i; i = alpm_list_next(i)) {
			found = 0;
			const char *grpname = i->data;
			for(j = syncs; j; j = alpm_list_next(j)) {
				alpm_db_t *db = j->data;
				alpm_group_t *grp = alpm_db_get_group(db, grpname);

				if(grp) {
					found++;
					/* get names of packages in group */
					for(k = grp->packages; k; k = alpm_list_next(k)) {
						if(!config->quiet) {
							printf("%s %s\n", grpname,
									alpm_pkg_get_name(k->data));
						} else {
							printf("%s\n", alpm_pkg_get_name(k->data));
						}
					}
				}
			}
			if(!found) {
				ret = 1;
			}
		}
	} else {
		ret = 1;
		for(i = syncs; i; i = alpm_list_next(i)) {
			alpm_db_t *db = i->data;

			for(j = alpm_db_get_groupcache(db); j; j = alpm_list_next(j)) {
				alpm_group_t *grp = j->data;
				ret = 0;

				if(level > 1) {
					for(k = grp->packages; k; k = alpm_list_next(k)) {
						printf("%s %s\n", grp->name,
								alpm_pkg_get_name(k->data));
					}
				} else {
					/* print grp names only, no package names */
					if(!alpm_list_find_str (s, grp->name)) {
						s = alpm_list_add (s, grp->name);
						printf("%s\n", grp->name);
					}
				}
			}
		}
		alpm_list_free(s);
	}

	return ret;
}

static int sync_info(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i, *j, *k;
	int ret = 0;

	if(targets) {
		for(i = targets; i; i = alpm_list_next(i)) {
			const char *target = i->data;
			char *name = strdup(target);
			char *repo, *pkgstr;
			int foundpkg = 0, founddb = 0;

			pkgstr = strchr(name, '/');
			if(pkgstr) {
				repo = name;
				*pkgstr = '\0';
				++pkgstr;
			} else {
				repo = NULL;
				pkgstr = name;
			}

			for(j = syncs; j; j = alpm_list_next(j)) {
				alpm_db_t *db = j->data;
				if(repo && strcmp(repo, alpm_db_get_name(db)) != 0) {
					continue;
				}
				founddb = 1;

				for(k = alpm_db_get_pkgcache(db); k; k = alpm_list_next(k)) {
					alpm_pkg_t *pkg = k->data;

					if(strcmp(alpm_pkg_get_name(pkg), pkgstr) == 0) {
						dump_pkg_full(pkg, config->op_s_info > 1);
						foundpkg = 1;
						break;
					}
				}
			}

			if(!founddb) {
				pm_printf(ALPM_LOG_ERROR,
						_("repository '%s' does not exist\n"), repo);
				ret++;
			}
			if(!foundpkg) {
				pm_printf(ALPM_LOG_ERROR,
						_("package '%s' was not found\n"), target);
				ret++;
			}
			free(name);
		}
	} else {
		for(i = syncs; i; i = alpm_list_next(i)) {
			alpm_db_t *db = i->data;

			for(j = alpm_db_get_pkgcache(db); j; j = alpm_list_next(j)) {
				alpm_pkg_t *pkg = j->data;
				dump_pkg_full(pkg, config->op_s_info > 1);
			}
		}
	}

	return ret;
}

static int sync_list(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i, *j, *ls = NULL;
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	int ret = 0;

	if(targets) {
		for(i = targets; i; i = alpm_list_next(i)) {
			const char *repo = i->data;
			alpm_db_t *db = NULL;

			for(j = syncs; j; j = alpm_list_next(j)) {
				alpm_db_t *d = j->data;

				if(strcmp(repo, alpm_db_get_name(d)) == 0) {
					db = d;
					break;
				}
			}

			if(db == NULL) {
				pm_printf(ALPM_LOG_ERROR,
					_("repository \"%s\" was not found.\n"), repo);
				ret = 1;
			}

			ls = alpm_list_add(ls, db);
		}
	} else {
		ls = syncs;
	}

	for(i = ls; i; i = alpm_list_next(i)) {
		alpm_db_t *db = i->data;

		for(j = alpm_db_get_pkgcache(db); j; j = alpm_list_next(j)) {
			alpm_pkg_t *pkg = j->data;

			if(!config->quiet) {
				const colstr_t *colstr = &config->colstr;
				printf("%s%s %s%s %s%s%s", colstr->repo, alpm_db_get_name(db),
						colstr->title, alpm_pkg_get_name(pkg),
						colstr->version, alpm_pkg_get_version(pkg), colstr->nocolor);
				print_installed(db_local, pkg);
				printf("\n");
			} else {
				printf("%s\n", alpm_pkg_get_name(pkg));
			}
		}
	}

	if(targets) {
		alpm_list_free(ls);
	}

	return ret;
}

static alpm_db_t *get_db(const char *dbname)
{
	alpm_list_t *i;
	for(i = alpm_get_syncdbs(config->handle); i; i = i->next) {
		alpm_db_t *db = i->data;
		if(strcmp(alpm_db_get_name(db), dbname) == 0) {
			return db;
		}
	}
	return NULL;
}

static int process_pkg(alpm_pkg_t *pkg)
{
	int ret = alpm_add_pkg(config->handle, pkg);

	if(ret == -1) {
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, "'%s': %s\n", alpm_pkg_get_name(pkg), alpm_strerror(err));
		return 1;
	}
	config->explicit_adds = alpm_list_add(config->explicit_adds, pkg);
	return 0;
}

static int group_exists(alpm_list_t *dbs, const char *name)
{
	alpm_list_t *i;
	for(i = dbs; i; i = i->next) {
		alpm_db_t *db = i->data;

		if(alpm_db_get_group(db, name)) {
			return 1;
		}
	}

	return 0;
}

static int process_group(alpm_list_t *dbs, const char *group, int error)
{
	int ret = 0;
	alpm_list_t *i;
	alpm_list_t *pkgs = alpm_find_group_pkgs(dbs, group);
	int count = alpm_list_count(pkgs);

	if(!count) {
		if(group_exists(dbs, group)) {
			return 0;
		}

		pm_printf(ALPM_LOG_ERROR, _("target not found: %s\n"), group);
		return 1;
	}

	if(error) {
		/* we already know another target errored. there is no reason to prompt the
		 * user here; we already validated the group name so just move on since we
		 * won't actually be installing anything anyway. */
		goto cleanup;
	}

	if(config->print == 0) {
		char *array = malloc(count);
		int n = 0;
		const colstr_t *colstr = &config->colstr;
		colon_printf(_n("There is %d member in group %s%s%s:\n",
				"There are %d members in group %s%s%s:\n", count),
				count, colstr->groups, group, colstr->title);
		select_display(pkgs);
		if(!array) {
			ret = 1;
			goto cleanup;
		}
		if(multiselect_question(array, count)) {
			ret = 1;
			free(array);
			goto cleanup;
		}
		for(i = pkgs, n = 0; i; i = alpm_list_next(i)) {
			alpm_pkg_t *pkg = i->data;

			if(array[n++] == 0) {
				continue;
			}

			if(process_pkg(pkg) == 1) {
				ret = 1;
				free(array);
				goto cleanup;
			}
		}
		free(array);
	} else {
		for(i = pkgs; i; i = alpm_list_next(i)) {
			alpm_pkg_t *pkg = i->data;

			if(process_pkg(pkg) == 1) {
				ret = 1;
				goto cleanup;
			}
		}
	}

cleanup:
	alpm_list_free(pkgs);
	return ret;
}

static int process_targname(alpm_list_t *dblist, const char *targname,
		int error)
{
	alpm_pkg_t *pkg = alpm_find_dbs_satisfier(config->handle, dblist, targname);

	/* skip ignored packages when user says no */
	if(alpm_errno(config->handle) == ALPM_ERR_PKG_IGNORED) {
			pm_printf(ALPM_LOG_WARNING, _("skipping target: %s\n"), targname);
			return 0;
	}

	if(pkg) {
		return process_pkg(pkg);
	}
	/* fallback on group */
	return process_group(dblist, targname, error);
}

static int process_target(const char *target, int error)
{
	/* process targets */
	char *targstring = strdup(target);
	char *targname = strchr(targstring, '/');
	int ret = 0;
	alpm_list_t *dblist;

	if(targname && targname != targstring) {
		alpm_db_t *db;
		const char *dbname;
		int usage;

		*targname = '\0';
		targname++;
		dbname = targstring;
		db = get_db(dbname);
		if(!db) {
			pm_printf(ALPM_LOG_ERROR, _("database not found: %s\n"),
					dbname);
			ret = 1;
			goto cleanup;
		}

		/* explicitly mark this repo as valid for installs since
		 * a repo name was given with the target */
		alpm_db_get_usage(db, &usage);
		alpm_db_set_usage(db, usage|ALPM_DB_USAGE_INSTALL);

		dblist = alpm_list_add(NULL, db);
		ret = process_targname(dblist, targname, error);
		alpm_list_free(dblist);

		/* restore old usage so we don't possibly disturb later
		 * targets */
		alpm_db_set_usage(db, usage);
	} else {
		targname = targstring;
		dblist = alpm_get_syncdbs(config->handle);
		ret = process_targname(dblist, targname, error);
	}

cleanup:
	free(targstring);
	if(ret && access(target, R_OK) == 0) {
		pm_printf(ALPM_LOG_WARNING,
				_("'%s' is a file, did you mean %s instead of %s?\n"),
				target, "-U/--upgrade", "-S/--sync");
	}
	return ret;
}

static int sync_trans(alpm_list_t *targets)
{
	int retval = 0;
	alpm_list_t *i;

	/* Step 1: create a new transaction... */
	if(trans_init(config->flags, 1) == -1) {
		return 1;
	}

	/* process targets */
	for(i = targets; i; i = alpm_list_next(i)) {
		const char *targ = i->data;
		if(process_target(targ, retval) == 1) {
			retval = 1;
		}
	}

	if(retval) {
		trans_release();
		return retval;
	}

	if(config->op_s_upgrade) {
		if(!config->print) {
			colon_printf(_("Starting full system upgrade...\n"));
			alpm_logaction(config->handle, PACMAN_CALLER_PREFIX,
					"starting full system upgrade\n");
		}
		if(alpm_sync_sysupgrade(config->handle, config->op_s_upgrade >= 2) == -1) {
			pm_printf(ALPM_LOG_ERROR, "%s\n", alpm_strerror(alpm_errno(config->handle)));
			trans_release();
			return 1;
		}
	}

	return sync_prepare_execute();
}

static void print_broken_dep(alpm_depmissing_t *miss)
{
	char *depstring = alpm_dep_compute_string(miss->depend);
	alpm_list_t *trans_add = alpm_trans_get_add(config->handle);
	alpm_pkg_t *pkg;
	if(miss->causingpkg == NULL) {
		/* package being installed/upgraded has unresolved dependency */
		colon_printf(_("unable to satisfy dependency '%s' required by %s\n"),
				depstring, miss->target);
	} else if((pkg = alpm_pkg_find(trans_add, miss->causingpkg))) {
		/* upgrading a package breaks a local dependency */
		colon_printf(_("installing %s (%s) breaks dependency '%s' required by %s\n"),
				miss->causingpkg, alpm_pkg_get_version(pkg), depstring, miss->target);
	} else {
		/* removing a package breaks a local dependency */
		colon_printf(_("removing %s breaks dependency '%s' required by %s\n"),
				miss->causingpkg, depstring, miss->target);
	}
	free(depstring);
}

int sync_prepare_execute(void)
{
	alpm_list_t *i, *packages, *data = NULL;
	int retval = 0;

	/* Step 2: "compute" the transaction based on targets and flags */
	if(alpm_trans_prepare(config->handle, &data) == -1) {
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, _("failed to prepare transaction (%s)\n"),
		        alpm_strerror(err));
		switch(err) {
			case ALPM_ERR_PKG_INVALID_ARCH:
				for(i = data; i; i = alpm_list_next(i)) {
					char *pkg = i->data;
					colon_printf(_("package %s does not have a valid architecture\n"), pkg);
					free(pkg);
				}
				break;
			case ALPM_ERR_UNSATISFIED_DEPS:
				for(i = data; i; i = alpm_list_next(i)) {
					print_broken_dep(i->data);
					alpm_depmissing_free(i->data);
				}
				break;
			case ALPM_ERR_CONFLICTING_DEPS:
				for(i = data; i; i = alpm_list_next(i)) {
					alpm_conflict_t *conflict = i->data;
					/* only print reason if it contains new information */
					if(conflict->reason->mod == ALPM_DEP_MOD_ANY) {
						colon_printf(_("%s and %s are in conflict\n"),
								conflict->package1, conflict->package2);
					} else {
						char *reason = alpm_dep_compute_string(conflict->reason);
						colon_printf(_("%s and %s are in conflict (%s)\n"),
								conflict->package1, conflict->package2, reason);
						free(reason);
					}
					alpm_conflict_free(conflict);
				}
				break;
			default:
				break;
		}
		retval = 1;
		goto cleanup;
	}

	packages = alpm_trans_get_add(config->handle);
	if(packages == NULL) {
		/* nothing to do: just exit without complaining */
		if(!config->print) {
			printf(_(" there is nothing to do\n"));
		}
		goto cleanup;
	}

	/* Step 3: actually perform the operation */
	if(config->print) {
		print_packages(packages);
		goto cleanup;
	}

	display_targets();
	printf("\n");

	int confirm;
	if(config->op_s_downloadonly) {
		confirm = yesno(_("Proceed with download?"));
	} else {
		confirm = yesno(_("Proceed with installation?"));
	}
	if(!confirm) {
		retval = 1;
		goto cleanup;
	}

	multibar_move_completed_up(true);
	if(alpm_trans_commit(config->handle, &data) == -1) {
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, _("failed to commit transaction (%s)\n"),
		        alpm_strerror(err));
		switch(err) {
			case ALPM_ERR_FILE_CONFLICTS:
				for(i = data; i; i = alpm_list_next(i)) {
					alpm_fileconflict_t *conflict = i->data;
					switch(conflict->type) {
						case ALPM_FILECONFLICT_TARGET:
							fprintf(stderr, _("%s exists in both '%s' and '%s'\n"),
									conflict->file, conflict->target, conflict->ctarget);
							break;
						case ALPM_FILECONFLICT_FILESYSTEM:
							if(conflict->ctarget[0]) {
								fprintf(stderr, _("%s: %s exists in filesystem (owned by %s)\n"),
										conflict->target, conflict->file, conflict->ctarget);
							} else {
								fprintf(stderr, _("%s: %s exists in filesystem\n"),
										conflict->target, conflict->file);
							}
							break;
					}
					alpm_fileconflict_free(conflict);
				}
				break;
			case ALPM_ERR_PKG_INVALID:
			case ALPM_ERR_PKG_INVALID_CHECKSUM:
			case ALPM_ERR_PKG_INVALID_SIG:
				for(i = data; i; i = alpm_list_next(i)) {
					char *filename = i->data;
					fprintf(stderr, _("%s is invalid or corrupted\n"), filename);
					free(filename);
				}
				break;
			default:
				break;
		}
		/* TODO: stderr? */
		printf(_("Errors occurred, no packages were upgraded.\n"));
		retval = 1;
		goto cleanup;
	}

	/* Step 4: release transaction resources */
cleanup:
	alpm_list_free(data);
	if(trans_release() == -1) {
		retval = 1;
	}

	return retval;
}

int pacman_sync(alpm_list_t *targets)
{
	alpm_list_t *sync_dbs = NULL;

	/* clean the cache */
	if(config->op_s_clean) {
		int ret = 0;

		if(trans_init(0, 0) == -1) {
			return 1;
		}

		ret += sync_cleancache(config->op_s_clean);
		ret += sync_cleandb_all();

		if(trans_release() == -1) {
			ret++;
		}

		return ret;
	}

	if(check_syncdbs(1, 0)) {
		return 1;
	}

	sync_dbs = alpm_get_syncdbs(config->handle);

	if(config->op_s_sync) {
		/* grab a fresh package list */
		colon_printf(_("Synchronizing package databases...\n"));
		alpm_logaction(config->handle, PACMAN_CALLER_PREFIX,
				"synchronizing package lists\n");
		if(!sync_syncdbs(config->op_s_sync, sync_dbs)) {
			return 1;
		}
	}

	if(check_syncdbs(1, 1)) {
		return 1;
	}

	/* search for a package */
	if(config->op_s_search) {
		return sync_search(sync_dbs, targets);
	}

	/* look for groups */
	if(config->group) {
		return sync_group(config->group, sync_dbs, targets);
	}

	/* get package info */
	if(config->op_s_info) {
		return sync_info(sync_dbs, targets);
	}

	/* get a listing of files in sync DBs */
	if(config->op_q_list) {
		return sync_list(sync_dbs, targets);
	}

	if(targets == NULL) {
		if(config->op_s_upgrade) {
			/* proceed */
		} else if(config->op_s_sync) {
			return 0;
		} else {
			/* don't proceed here unless we have an operation that doesn't require a
			 * target list */
			pm_printf(ALPM_LOG_ERROR, _("no targets specified (use -h for help)\n"));
			return 1;
		}
	}

	return sync_trans(targets);
}

//testpkg.c

__attribute__((format(printf, 3, 0)))
static void output_cb(void *ctx, alpm_loglevel_t level, const char *fmt, va_list args)
{
	(void)ctx;
	if(fmt[0] == '\0') {
		return;
	}
	switch(level) {
		case ALPM_LOG_ERROR: printf(_("error: ")); break;
		case ALPM_LOG_WARNING: printf(_("warning: ")); break;
		default: return; /* skip other messages */
	}
	vprintf(fmt, args);
}

int main(int argc, char *argv[])
{
	int retval = 1; /* default = false */
	alpm_handle_t *handle;
	alpm_errno_t err;
	alpm_pkg_t *pkg = NULL;
	const int siglevel = ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL;

#if defined(ENABLE_NLS)
	bindtextdomain(PACKAGE, LOCALEDIR);
#endif

	if(argc != 2) {
		fprintf(stderr, "testpkg (pacman) v" PACKAGE_VERSION "\n\n");
		fprintf(stderr,	_("Test a pacman package for validity.\n\n"));
		fprintf(stderr,	_("Usage: testpkg <package file>\n"));
		return 1;
	}

	handle = alpm_initialize(ROOTDIR, DBPATH, &err);
	if(!handle) {
		fprintf(stderr, _("cannot initialize alpm: %s\n"), alpm_strerror(err));
		return 1;
	}

	/* let us get log messages from libalpm */
	alpm_option_set_logcb(handle, output_cb, NULL);

	/* set gpgdir to default */
	alpm_option_set_gpgdir(handle, GPGDIR);

	if(alpm_pkg_load(handle, argv[1], 1, siglevel, &pkg) == -1
			|| pkg == NULL) {
		err = alpm_errno(handle);
		switch(err) {
			case ALPM_ERR_PKG_NOT_FOUND:
				printf(_("Cannot find the given file.\n"));
				break;
			case ALPM_ERR_PKG_OPEN:
				printf(_("Cannot open the given file.\n"));
				break;
			case ALPM_ERR_LIBARCHIVE:
			case ALPM_ERR_PKG_INVALID:
				printf(_("Package is invalid.\n"));
				break;
			default:
				printf(_("libalpm error: %s\n"), alpm_strerror(err));
				break;
		}
		retval = 1;
	} else {
		alpm_pkg_free(pkg);
		printf(_("Package is valid.\n"));
		retval = 0;
	}

	if(alpm_release(handle) == -1) {
		fprintf(stderr, _("error releasing alpm\n"));
	}

	return retval;
}

/** Create a string representing bytes in hexadecimal.
 * @param bytes the bytes to represent in hexadecimal
 * @param size number of bytes to consider
 * @return a NULL terminated string with the hexadecimal representation of
 * bytes or NULL on error. This string must be freed.
 */
char *hex_representation(const unsigned char *bytes, size_t size)
{
	static const char *hex_digits = "0123456789abcdef";
	char *str = malloc(2 * size + 1);
	size_t i;

	if(!str) {
		return NULL;
	}

	for(i = 0; i < size; i++) {
		str[2 * i] = hex_digits[bytes[i] >> 4];
		str[2 * i + 1] = hex_digits[bytes[i] & 0x0f];
	}

	str[2 * size] = '\0';

	return str;
}

/** Parse the basename of a program from a path.
* @param path path to parse basename from
*
* @return everything following the final '/'
*/
const char *mbasename(const char *path)
{
	const char *last = strrchr(path, '/');
	if(last) {
		return last + 1;
	}
	return path;
}

/** Parse the dirname of a program from a path.
* The path returned should be freed.
* @param path path to parse dirname from
*
* @return everything preceding the final '/'
*/
char *mdirname(const char *path)
{
	char *ret, *last;

	/* null or empty path */
	if(path == NULL || *path == '\0') {
		return strdup(".");
	}

	if((ret = strdup(path)) == NULL) {
		return NULL;
	}

	last = strrchr(ret, '/');

	if(last != NULL) {
		/* we found a '/', so terminate our string */
		if(last == ret) {
			/* return "/" for root */
			last++;
		}
		*last = '\0';
		return ret;
	}

	/* no slash found */
	free(ret);
	return strdup(".");
}

/** lstat wrapper that treats /path/dirsymlink/ the same as /path/dirsymlink.
 * Linux lstat follows POSIX semantics and still performs a dereference on
 * the first, and for uses of lstat in libalpm this is not what we want.
 * @param path path to file to lstat
 * @param buf structure to fill with stat information
 * @return the return code from lstat
 */
int llstat(char *path, struct stat *buf)
{
	int ret;
	char *c = NULL;
	size_t len = strlen(path);

	while(len > 1 && path[len - 1] == '/') {
		--len;
		c = path + len;
	}

	if(c) {
		*c = '\0';
		ret = lstat(path, buf);
		*c = '/';
	} else {
		ret = lstat(path, buf);
	}

	return ret;
}

/** Wrapper around fgets() which properly handles EINTR
 * @param s string to read into
 * @param size maximum length to read
 * @param stream stream to read from
 * @return value returned by fgets()
 */
char *safe_fgets(char *s, int size, FILE *stream)
{
	char *ret;
	int errno_save = errno, ferror_save = ferror(stream);
	while((ret = fgets(s, size, stream)) == NULL && !feof(stream)) {
		if(errno == EINTR) {
			/* clear any errors we set and try again */
			errno = errno_save;
			if(!ferror_save) {
				clearerr(stream);
			}
		} else {
			break;
		}
	}
	return ret;
}

/* Trim whitespace and newlines from a string
 */
size_t strtrim(char *str)
{
	char *end, *pch = str;

	if(str == NULL || *str == '\0') {
		/* string is empty, so we're done. */
		return 0;
	}

	while(isspace((unsigned char)*pch)) {
		pch++;
	}
	if(pch != str) {
		size_t len = strlen(pch);
		/* check if there wasn't anything but whitespace in the string. */
		if(len == 0) {
			*str = '\0';
			return 0;
		}
		memmove(str, pch, len + 1);
		pch = str;
	}

	end = (str + strlen(str) - 1);
	while(isspace((unsigned char)*end)) {
		end--;
	}
	*++end = '\0';

	return end - pch;
}

#ifndef HAVE_STRNLEN
/* A quick and dirty implementation derived from glibc */
/** Determines the length of a fixed-size string.
 * @param s string to be measured
 * @param max maximum number of characters to search for the string end
 * @return length of s or max, whichever is smaller
 */
static size_t strnlen(const char *s, size_t max)
{
	register const char *p;
	for(p = s; *p && max--; ++p);
	return (p - s);
}
#endif

#ifndef HAVE_STRNDUP
/** Copies a string.
 * Returned string needs to be freed
 * @param s string to be copied
 * @param n maximum number of characters to copy
 * @return pointer to the new string on success, NULL on error
 */
char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *new = (char *) malloc(len + 1);

	if(new == NULL) {
		return NULL;
	}

	new[len] = '\0';
	return (char *)memcpy(new, s, len);
}
#endif

void wordsplit_free(char **ws)
{
	if(ws) {
		char **c;
		for(c = ws; *c; c++) {
			free(*c);
		}
		free(ws);
	}
}

char **wordsplit(const char *str)
{
	const char *c = str, *end;
	char **out = NULL, **outsave;
	size_t count = 0;

	if(str == NULL) {
		errno = EINVAL;
		return NULL;
	}

	for(c = str; isspace(*c); c++);
	while(*c) {
		size_t wordlen = 0;

		/* extend our array */
		outsave = out;
		if((out = realloc(out, (count + 1) * sizeof(char*))) == NULL) {
			out = outsave;
			goto error;
		}

		/* calculate word length and check for unbalanced quotes */
		for(end = c; *end && !isspace(*end); end++) {
			if(*end == '\'' || *end == '"') {
				char quote = *end;
				while(*(++end) && *end != quote) {
					if(*end == '\\' && *(end + 1) == quote) {
						end++;
					}
					wordlen++;
				}
				if(*end != quote) {
					errno = EINVAL;
					goto error;
				}
			} else {
				if(*end == '\\' && (end[1] == '\'' || end[1] == '"')) {
					end++; /* skip the '\\' */
				}
				wordlen++;
			}
		}

		if(wordlen == (size_t) (end - c)) {
			/* no internal quotes or escapes, copy it the easy way */
			if((out[count++] = strndup(c, wordlen)) == NULL) {
				goto error;
			}
		} else {
			/* manually copy to remove quotes and escapes */
			char *dest = out[count++] = malloc(wordlen + 1);
			if(dest == NULL) { goto error; }
			while(c < end) {
				if(*c == '\'' || *c == '"') {
					char quote = *c;
					/* we know there must be a matching end quote,
					 * no need to check for '\0' */
					for(c++; *c != quote; c++) {
						if(*c == '\\' && *(c + 1) == quote) {
							c++;
						}
						*(dest++) = *c;
					}
					c++;
				} else {
					if(*c == '\\' && (c[1] == '\'' || c[1] == '"')) {
						c++; /* skip the '\\' */
					}
					*(dest++) = *(c++);
				}
			}
			*dest = '\0';
		}

		if(*end == '\0') {
			break;
		} else {
			for(c = end + 1; isspace(*c); c++);
		}
	}

	outsave = out;
	if((out = realloc(out, (count + 1) * sizeof(char*))) == NULL) {
		out = outsave;
		goto error;
	}

	out[count++] = NULL;

	return out;

error:
	/* can't use wordsplit_free here because NULL has not been appended */
	while(count) {
		free(out[--count]);
	}
	free(out);
	return NULL;
}

//util.c
static int cached_columns = -1;

struct table_cell_t {
	char *label;
	size_t len;
	int mode;
};

enum {
	CELL_NORMAL = 0,
	CELL_TITLE = (1 << 0),
	CELL_RIGHT_ALIGN = (1 << 1),
	CELL_FREE = (1 << 3)
};

int trans_init(int flags, int check_valid)
{
	int ret;

	check_syncdbs(0, check_valid);

	ret = alpm_trans_init(config->handle, flags);
	if(ret == -1) {
		trans_init_error();
		return -1;
	}
	return 0;
}

void trans_init_error(void)
{
	alpm_errno_t err = alpm_errno(config->handle);
	pm_printf(ALPM_LOG_ERROR, _("failed to init transaction (%s)\n"),
			alpm_strerror(err));
	if(err == ALPM_ERR_HANDLE_LOCK) {
		const char *lockfile = alpm_option_get_lockfile(config->handle);
		pm_printf(ALPM_LOG_ERROR, _("could not lock database: %s\n"),
					strerror(errno));
		if(access(lockfile, F_OK) == 0) {
			fprintf(stderr, _("  if you're sure a package manager is not already\n"
						"  running, you can remove %s\n"), lockfile);
		}
	}
}

int trans_release(void)
{
	if(alpm_trans_release(config->handle) == -1) {
		pm_printf(ALPM_LOG_ERROR, _("failed to release transaction (%s)\n"),
				alpm_strerror(alpm_errno(config->handle)));
		return -1;
	}
	return 0;
}

int needs_root(void)
{
	if(config->sysroot) {
		return 1;
	}
	switch(config->op) {
		case PM_OP_DATABASE:
			return !config->op_q_check;
		case PM_OP_UPGRADE:
		case PM_OP_REMOVE:
			return !config->print;
		case PM_OP_SYNC:
			return (config->op_s_clean || config->op_s_sync ||
					(!config->group && !config->op_s_info && !config->op_q_list &&
					 !config->op_s_search && !config->print));
		case PM_OP_FILES:
			return config->op_s_sync;
		default:
			return 0;
	}
}

int check_syncdbs(size_t need_repos, int check_valid)
{
	int ret = 0;
	alpm_list_t *i;
	alpm_list_t *sync_dbs = alpm_get_syncdbs(config->handle);

	if(need_repos && sync_dbs == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("no usable package repositories configured.\n"));
		return 1;
	}

	if(check_valid) {
		/* ensure all known dbs are valid */
		for(i = sync_dbs; i; i = alpm_list_next(i)) {
			alpm_db_t *db = i->data;
			if(alpm_db_get_valid(db)) {
				pm_printf(ALPM_LOG_ERROR, _("database '%s' is not valid (%s)\n"),
						alpm_db_get_name(db), alpm_strerror(alpm_errno(config->handle)));
				ret = 1;
			}
		}
	}
	return ret;
}

int sync_syncdbs(int level, alpm_list_t *syncs)
{
	int ret;
	int force = (level < 2 ? 0 : 1);

	multibar_move_completed_up(false);
	ret = alpm_db_update(config->handle, syncs, force);
	if(ret < 0) {
		pm_printf(ALPM_LOG_ERROR, _("failed to synchronize all databases (%s)\n"),
			alpm_strerror(alpm_errno(config->handle)));
	}

	return (ret >= 0);
}

/* discard unhandled input on the terminal's input buffer */
static int flush_term_input(int fd)
{
#ifdef HAVE_TCFLUSH
	if(isatty(fd)) {
		return tcflush(fd, TCIFLUSH);
	}
#endif

	/* fail silently */
	return 0;
}

void columns_cache_reset(void)
{
	cached_columns = -1;
}

static int getcols_fd(int fd)
{
	int width = -1;

	if(!isatty(fd)) {
		return 0;
	}

#if defined(TIOCGSIZE)
	struct ttysize win;
	if(ioctl(fd, TIOCGSIZE, &win) == 0) {
		width = win.ts_cols;
	}
#elif defined(TIOCGWINSZ)
	struct winsize win;
	if(ioctl(fd, TIOCGWINSZ, &win) == 0) {
		width = win.ws_col;
	}
#endif

	if(width <= 0) {
		return -EIO;
	}

	return width;
}

unsigned short getcols(void)
{
	const char *e;
	int c = -1;

	if(cached_columns >= 0) {
		return cached_columns;
	}

	e = getenv("COLUMNS");
	if(e && *e) {
		char *p = NULL;
		c = strtol(e, &p, 10);
		if(*p != '\0') {
			c= -1;
		}
	}

	if(c < 0) {
		c = getcols_fd(STDOUT_FILENO);
	}

	if(c < 0) {
		c = 80;
	}

	cached_columns = c;
	return c;
}

/* does the same thing as 'rm -rf' */
int rmrf(const char *path)
{
	int errflag = 0;
	struct dirent *dp;
	DIR *dirp;

	if(!unlink(path)) {
		return 0;
	} else {
		switch(errno) {
		case ENOENT:
			return 0;
		case EPERM:
		case EISDIR:
			break;
		default:
			/* not a directory */
			return 1;
		}

		dirp = opendir(path);
		if(!dirp) {
			return 1;
		}
		for(dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
			if(strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0) {
				char name[PATH_MAX];
				snprintf(name, PATH_MAX, "%s/%s", path, dp->d_name);
				errflag += rmrf(name);
			}
		}
		closedir(dirp);
		if(rmdir(path)) {
			errflag++;
		}
		return errflag;
	}
}

/* output a string, but wrap words properly with a specified indentation
 */
void indentprint(const char *str, unsigned short indent, unsigned short cols)
{
	wchar_t *wcstr;
	const wchar_t *p;
	size_t len, cidx;

	if(!str) {
		return;
	}

	/* if we're not a tty, or our tty is not wide enough that wrapping even makes
	 * sense, print without indenting */
	if(cols == 0 || indent > cols) {
		fputs(str, stdout);
		return;
	}

	len = strlen(str) + 1;
	wcstr = calloc(len, sizeof(wchar_t));
	len = mbstowcs(wcstr, str, len);
	p = wcstr;
	cidx = indent;

	if(!p || !len) {
		free(wcstr);
		return;
	}

	while(*p) {
		if(*p == L' ') {
			const wchar_t *q, *next;
			p++;
			if(p == NULL || *p == L' ') continue;
			next = wcschr(p, L' ');
			if(next == NULL) {
				next = p + wcslen(p);
			}
			/* len captures # cols */
			len = 0;
			q = p;
			while(q < next) {
				len += wcwidth(*q++);
			}
			if((len + 1) > (cols - cidx)) {
				/* wrap to a newline and reindent */
				printf("\n%-*s", (int)indent, "");
				cidx = indent;
			} else {
				printf(" ");
				cidx++;
			}
			continue;
		}
		printf("%lc", (wint_t)*p);
		cidx += wcwidth(*p);
		p++;
	}
	free(wcstr);
}

/* Replace all occurrences of 'needle' with 'replace' in 'str', returning
 * a new string (must be free'd) */
char *strreplace(const char *str, const char *needle, const char *replace)
{
	const char *p = NULL, *q = NULL;
	char *newstr = NULL, *newp = NULL;
	alpm_list_t *i = NULL, *list = NULL;
	size_t needlesz = strlen(needle), replacesz = strlen(replace);
	size_t newsz;

	if(!str) {
		return NULL;
	}

	p = str;
	q = strstr(p, needle);
	while(q) {
		list = alpm_list_add(list, (char *)q);
		p = q + needlesz;
		q = strstr(p, needle);
	}

	/* no occurrences of needle found */
	if(!list) {
		return strdup(str);
	}
	/* size of new string = size of old string + "number of occurrences of needle"
	 * x "size difference between replace and needle" */
	newsz = strlen(str) + 1 +
		alpm_list_count(list) * (replacesz - needlesz);
	newstr = calloc(newsz, sizeof(char));
	if(!newstr) {
		return NULL;
	}

	p = str;
	newp = newstr;
	for(i = list; i; i = alpm_list_next(i)) {
		q = i->data;
		if(q > p) {
			/* add chars between this occurrence and last occurrence, if any */
			memcpy(newp, p, (size_t)(q - p));
			newp += q - p;
		}
		memcpy(newp, replace, replacesz);
		newp += replacesz;
		p = q + needlesz;
	}
	alpm_list_free(list);

	if(*p) {
		/* add the rest of 'p' */
		strcpy(newp, p);
	}

	return newstr;
}

static size_t string_length(const char *s)
{
	int len;
	wchar_t *wcstr;

	if(!s || s[0] == '\0') {
		return 0;
	}
	if(strstr(s, "\033")) {
		char* replaced = malloc(sizeof(char) * strlen(s));
		int iter = 0;
		for(; *s; s++) {
			if(*s == '\033') {
				while(*s != 'm') {
					s++;
				}
			} else {
				replaced[iter] = *s;
				iter++;
			}
		}
		replaced[iter] = '\0';
		len = iter;
		wcstr = calloc(len, sizeof(wchar_t));
		len = mbstowcs(wcstr, replaced, len);
		len = wcswidth(wcstr, len);
		free(wcstr);
		free(replaced);
	} else {
		/* len goes from # bytes -> # chars -> # cols */
		len = strlen(s) + 1;
		wcstr = calloc(len, sizeof(wchar_t));
		len = mbstowcs(wcstr, s, len);
		len = wcswidth(wcstr, len);
		free(wcstr);
	}

	return len;
}

static void add_table_cell(alpm_list_t **row, char *label, int mode)
{
	struct table_cell_t *cell = malloc(sizeof(struct table_cell_t));

	cell->label = label;
	cell->mode = mode;
	cell->len = string_length(label);

	*row = alpm_list_add(*row, cell);
}

static void table_free_cell(void *ptr)
{
	struct table_cell_t *cell = ptr;

	if(cell) {
		if(cell->mode & CELL_FREE) {
			free(cell->label);
		}
		free(cell);
	}
}

static void table_free(alpm_list_t *headers, alpm_list_t *rows)
{
	alpm_list_t *i;

	alpm_list_free_inner(headers, table_free_cell);

	for(i = rows; i; i = alpm_list_next(i)) {
		alpm_list_free_inner(i->data, table_free_cell);
		alpm_list_free(i->data);
	}

	alpm_list_free(headers);
	alpm_list_free(rows);
}

static void add_transaction_sizes_row(alpm_list_t **rows, char *label, off_t size)
{
	alpm_list_t *row = NULL;
	char *str;
	const char *units;
	double s = humanize_size(size, 'M', 2, &units);
	pm_asprintf(&str, "%.2f %s", s, units);

	add_table_cell(&row, label, CELL_TITLE);
	add_table_cell(&row, str, CELL_RIGHT_ALIGN | CELL_FREE);

	*rows = alpm_list_add(*rows, row);
}

void string_display(const char *title, const char *string, unsigned short cols)
{
	if(title) {
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}
	if(string == NULL || string[0] == '\0') {
		printf(_("None"));
	} else {
		/* compute the length of title + a space */
		size_t len = string_length(title) + 1;
		indentprint(string, (unsigned short)len, cols);
	}
	printf("\n");
}

static void table_print_line(const alpm_list_t *line, short col_padding,
		size_t colcount, const size_t *widths, const int *has_data)
{
	size_t i;
	int need_padding = 0;
	const alpm_list_t *curcell;

	for(i = 0, curcell = line; curcell && i < colcount;
			i++, curcell = alpm_list_next(curcell)) {
		const struct table_cell_t *cell = curcell->data;
		const char *str = (cell->label ? cell->label : "");
		int cell_width;

		if(!has_data[i]) {
			continue;
		}

		/* calculate cell width, adjusting for multi-byte character strings */
		cell_width = (int)widths[i] - string_length(str) + strlen(str);
		cell_width = cell->mode & CELL_RIGHT_ALIGN ? cell_width : -cell_width;

		if(need_padding) {
			printf("%*s", col_padding, "");
		}

		if(cell->mode & CELL_TITLE) {
			printf("%s%*s%s", config->colstr.title, cell_width, str, config->colstr.nocolor);
		} else {
			printf("%*s", cell_width, str);
		}
		need_padding = 1;
	}

	printf("\n");
}


/**
 * Find the max string width of each column. Also determines whether values
 * exist in the column and sets the value in has_data accordingly.
 * @param header a list of header strings
 * @param rows a list of lists of rows as strings
 * @param padding the amount of padding between columns
 * @param totalcols the total number of columns in the header and each row
 * @param widths a pointer to store width data
 * @param has_data a pointer to store whether column has data
 *
 * @return the total width of the table; 0 on failure
 */
static size_t table_calc_widths(const alpm_list_t *header,
		const alpm_list_t *rows, short padding, size_t totalcols,
		size_t **widths, int **has_data)
{
	const alpm_list_t *i;
	size_t curcol, totalwidth = 0, usefulcols = 0;
	size_t *colwidths;
	int *coldata;

	if(totalcols <= 0) {
		return 0;
	}

	colwidths = malloc(totalcols * sizeof(size_t));
	coldata = calloc(totalcols, sizeof(int));
	if(!colwidths || !coldata) {
		free(colwidths);
		free(coldata);
		return 0;
	}
	/* header determines column count and initial values of longest_strs */
	for(i = header, curcol = 0; i; i = alpm_list_next(i), curcol++) {
		const struct table_cell_t *row = i->data;
		colwidths[curcol] = row->len;
		/* note: header does not determine whether column has data */
	}

	/* now find the longest string in each column */
	for(i = rows; i; i = alpm_list_next(i)) {
		/* grab first column of each row and iterate through columns */
		const alpm_list_t *j = i->data;
		for(curcol = 0; j; j = alpm_list_next(j), curcol++) {
			const struct table_cell_t *cell = j->data;
			size_t str_len = cell ? cell->len : 0;

			if(str_len > colwidths[curcol]) {
				colwidths[curcol] = str_len;
			}
			if(str_len > 0) {
				coldata[curcol] = 1;
			}
		}
	}

	for(i = header, curcol = 0; i; i = alpm_list_next(i), curcol++) {
		/* only include columns that have data */
		if(coldata[curcol]) {
			usefulcols++;
			totalwidth += colwidths[curcol];
		}
	}

	/* add padding between columns */
	if(usefulcols > 0) {
		totalwidth += padding * (usefulcols - 1);
	}

	*widths = colwidths;
	*has_data = coldata;
	return totalwidth;
}

/** Displays the list in table format
 *
 * @param header the column headers. column count is determined by the nr
 *               of headers
 * @param rows the rows to display as a list of lists of strings. the outer
 *             list represents the rows, the inner list the cells (= columns)
 * @param cols the number of columns available in the terminal
 * @return -1 if not enough terminal cols available, else 0
 */
static int table_display(const alpm_list_t *header,
		const alpm_list_t *rows, unsigned short cols)
{
	const unsigned short padding = 2;
	const alpm_list_t *i, *first;
	size_t *widths = NULL, totalcols, totalwidth;
	int *has_data = NULL;
	int ret = 0;

	if(rows == NULL) {
		return ret;
	}

	/* we want the first row. if no headers are provided, use the first
	 * entry of the rows array. */
	first = header ? header : rows->data;

	totalcols = alpm_list_count(first);
	totalwidth = table_calc_widths(first, rows, padding, totalcols,
			&widths, &has_data);
	/* return -1 if terminal is not wide enough */
	if(cols && totalwidth > cols) {
		pm_printf(ALPM_LOG_WARNING,
				_("insufficient columns available for table display\n"));
		ret = -1;
		goto cleanup;
	}
	if(!totalwidth || !widths || !has_data) {
		ret = -1;
		goto cleanup;
	}

	if(header) {
		table_print_line(header, padding, totalcols, widths, has_data);
		printf("\n");
	}

	for(i = rows; i; i = alpm_list_next(i)) {
		table_print_line(i->data, padding, totalcols, widths, has_data);
	}

cleanup:
	free(widths);
	free(has_data);
	return ret;
}

void list_display(const char *title, const alpm_list_t *list,
		unsigned short maxcols)
{
	const alpm_list_t *i;
	size_t len = 0;

	if(title) {
		len = string_length(title) + 1;
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}

	if(!list) {
		printf("%s\n", _("None"));
	} else {
		size_t cols = len;
		const char *str = list->data;
		fputs(str, stdout);
		cols += string_length(str);
		for(i = alpm_list_next(list); i; i = alpm_list_next(i)) {
			str = i->data;
			size_t s = string_length(str);
			/* wrap only if we have enough usable column space */
			if(maxcols > len && cols + s + 2 >= maxcols) {
				size_t j;
				cols = len;
				printf("\n");
				for(j = 1; j <= len; j++) {
					printf(" ");
				}
			} else if(cols != len) {
				/* 2 spaces are added if this is not the first element on a line. */
				printf("  ");
				cols += 2;
			}
			fputs(str, stdout);
			cols += s;
		}
		putchar('\n');
	}
}

void list_display_linebreak(const char *title, const alpm_list_t *list,
		unsigned short maxcols)
{
	unsigned short len = 0;

	if(title) {
		len = (unsigned short)string_length(title) + 1;
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}

	if(!list) {
		printf("%s\n", _("None"));
	} else {
		const alpm_list_t *i;
		/* Print the first element */
		indentprint((const char *)list->data, len, maxcols);
		printf("\n");
		/* Print the rest */
		for(i = alpm_list_next(list); i; i = alpm_list_next(i)) {
			size_t j;
			for(j = 1; j <= len; j++) {
				printf(" ");
			}
			indentprint((const char *)i->data, len, maxcols);
			printf("\n");
		}
	}
}

void signature_display(const char *title, alpm_siglist_t *siglist,
		unsigned short maxcols)
{
	unsigned short len = 0;

	if(title) {
		len = (unsigned short)string_length(title) + 1;
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}
	if(siglist->count == 0) {
		printf(_("None"));
	} else {
		size_t i;
		for(i = 0; i < siglist->count; i++) {
			char *sigline;
			const char *status, *validity, *name;
			int ret;
			alpm_sigresult_t *result = siglist->results + i;
			/* Don't re-indent the first result */
			if(i != 0) {
				size_t j;
				for(j = 1; j <= len; j++) {
					printf(" ");
				}
			}
			switch(result->status) {
				case ALPM_SIGSTATUS_VALID:
					status = _("Valid");
					break;
				case ALPM_SIGSTATUS_KEY_EXPIRED:
					status = _("Key expired");
					break;
				case ALPM_SIGSTATUS_SIG_EXPIRED:
					status = _("Expired");
					break;
				case ALPM_SIGSTATUS_INVALID:
					status = _("Invalid");
					break;
				case ALPM_SIGSTATUS_KEY_UNKNOWN:
					status = _("Key unknown");
					break;
				case ALPM_SIGSTATUS_KEY_DISABLED:
					status = _("Key disabled");
					break;
				default:
					status = _("Signature error");
					break;
			}
			switch(result->validity) {
				case ALPM_SIGVALIDITY_FULL:
					validity = _("full trust");
					break;
				case ALPM_SIGVALIDITY_MARGINAL:
					validity = _("marginal trust");
					break;
				case ALPM_SIGVALIDITY_NEVER:
					validity = _("never trust");
					break;
				case ALPM_SIGVALIDITY_UNKNOWN:
				default:
					validity = _("unknown trust");
					break;
			}
			name = result->key.uid ? result->key.uid : result->key.fingerprint;
			ret = pm_asprintf(&sigline, _("%s, %s from \"%s\""),
					status, validity, name);
			if(ret == -1) {
				continue;
			}
			indentprint(sigline, len, maxcols);
			printf("\n");
			free(sigline);
		}
	}
}

/* creates a header row for use with table_display */
static alpm_list_t *create_verbose_header(size_t count)
{
	alpm_list_t *ret = NULL;

	char *header;
	pm_asprintf(&header, "%s (%zu)", _("Package"), count);

	add_table_cell(&ret, header, CELL_TITLE | CELL_FREE);
	add_table_cell(&ret, _("Old Version"), CELL_TITLE);
	add_table_cell(&ret, _("New Version"), CELL_TITLE);
	add_table_cell(&ret, _("Net Change"), CELL_TITLE);
	add_table_cell(&ret, _("Download Size"), CELL_TITLE);

	return ret;
}

/* returns package info as list of strings */
static alpm_list_t *create_verbose_row(pm_target_t *target)
{
	char *str;
	off_t size = 0;
	double human_size;
	const char *label;
	alpm_list_t *ret = NULL;

	/* a row consists of the package name, */
	if(target->install) {
		const alpm_db_t *db = alpm_pkg_get_db(target->install);
		if(db) {
			pm_asprintf(&str, "%s/%s", alpm_db_get_name(db), alpm_pkg_get_name(target->install));
		} else {
			pm_asprintf(&str, "%s", alpm_pkg_get_name(target->install));
		}
	} else {
		pm_asprintf(&str, "%s", alpm_pkg_get_name(target->remove));
	}
	add_table_cell(&ret, str, CELL_NORMAL | CELL_FREE);

	/* old and new versions */
	pm_asprintf(&str, "%s",
			target->remove != NULL ? alpm_pkg_get_version(target->remove) : "");
	add_table_cell(&ret, str, CELL_NORMAL | CELL_FREE);

	pm_asprintf(&str, "%s",
			target->install != NULL ? alpm_pkg_get_version(target->install) : "");
	add_table_cell(&ret, str, CELL_NORMAL | CELL_FREE);

	/* and size */
	size -= target->remove ? alpm_pkg_get_isize(target->remove) : 0;
	size += target->install ? alpm_pkg_get_isize(target->install) : 0;
	human_size = humanize_size(size, 'M', 2, &label);
	pm_asprintf(&str, "%.2f %s", human_size, label);
	add_table_cell(&ret, str, CELL_RIGHT_ALIGN | CELL_FREE);

	size = target->install ? alpm_pkg_download_size(target->install) : 0;
	if(size != 0) {
		human_size = humanize_size(size, 'M', 2, &label);
		pm_asprintf(&str, "%.2f %s", human_size, label);
	} else {
		str = NULL;
	}
	add_table_cell(&ret, str, CELL_RIGHT_ALIGN | CELL_FREE);

	return ret;
}

/* prepare a list of pkgs to display */
static void _display_targets(alpm_list_t *targets, int verbose)
{
	char *str;
	off_t isize = 0, rsize = 0, dlsize = 0;
	unsigned short cols;
	alpm_list_t *i, *names = NULL, *header = NULL, *rows = NULL;

	if(!targets) {
		return;
	}

	/* gather package info */
	for(i = targets; i; i = alpm_list_next(i)) {
		pm_target_t *target = i->data;

		if(target->install) {
			dlsize += alpm_pkg_download_size(target->install);
			isize += alpm_pkg_get_isize(target->install);
		}
		if(target->remove) {
			/* add up size of all removed packages */
			rsize += alpm_pkg_get_isize(target->remove);
		}
	}

	/* form data for both verbose and non-verbose display */
	for(i = targets; i; i = alpm_list_next(i)) {
		pm_target_t *target = i->data;

		if(verbose) {
			rows = alpm_list_add(rows, create_verbose_row(target));
		}

		if(target->install) {
			pm_asprintf(&str, "%s%s-%s%s", alpm_pkg_get_name(target->install), config->colstr.faint,
					alpm_pkg_get_version(target->install), config->colstr.nocolor);
		} else if(isize == 0) {
			pm_asprintf(&str, "%s%s-%s%s", alpm_pkg_get_name(target->remove), config->colstr.faint,
					alpm_pkg_get_version(target->remove), config->colstr.nocolor);
		} else {
			pm_asprintf(&str, "%s%s-%s %s[%s]%s", alpm_pkg_get_name(target->remove), config->colstr.faint,
					alpm_pkg_get_version(target->remove), config->colstr.nocolor, _("removal"), config->colstr.nocolor);
		}
		names = alpm_list_add(names, str);
	}

	/* print to screen */
	pm_asprintf(&str, "%s (%zu)", _("Packages"), alpm_list_count(targets));
	printf("\n");

	cols = getcols();
	if(verbose) {
		header = create_verbose_header(alpm_list_count(targets));
		if(table_display(header, rows, cols) != 0) {
			/* fallback to list display if table wouldn't fit */
			list_display(str, names, cols);
		}
	} else {
		list_display(str, names, cols);
	}
	printf("\n");

	table_free(header, rows);
	FREELIST(names);
	free(str);
	rows = NULL;

	if(dlsize > 0 || config->op_s_downloadonly) {
		add_transaction_sizes_row(&rows, _("Total Download Size:"), dlsize);
	}
	if(!config->op_s_downloadonly) {
		if(isize > 0) {
			add_transaction_sizes_row(&rows, _("Total Installed Size:"), isize);
		}
		if(rsize > 0 && isize == 0) {
			add_transaction_sizes_row(&rows, _("Total Removed Size:"), rsize);
		}
		/* only show this net value if different from raw installed size */
		if(isize > 0 && rsize > 0) {
			add_transaction_sizes_row(&rows, _("Net Upgrade Size:"), isize - rsize);
		}
	}
	table_display(NULL, rows, cols);
	table_free(NULL, rows);
}

static int target_cmp(const void *p1, const void *p2)
{
	const pm_target_t *targ1 = p1;
	const pm_target_t *targ2 = p2;
	/* explicit are always sorted after implicit (e.g. deps, pulled targets) */
	if(targ1->is_explicit != targ2->is_explicit) {
		return targ1->is_explicit > targ2->is_explicit;
	}
	const char *name1 = targ1->install ?
		alpm_pkg_get_name(targ1->install) : alpm_pkg_get_name(targ1->remove);
	const char *name2 = targ2->install ?
		alpm_pkg_get_name(targ2->install) : alpm_pkg_get_name(targ2->remove);
	return strcmp(name1, name2);
}

static int pkg_cmp(const void *p1, const void *p2)
{
	/* explicit cast due to (un)necessary removal of const */
	alpm_pkg_t *pkg1 = (alpm_pkg_t *)p1;
	alpm_pkg_t *pkg2 = (alpm_pkg_t *)p2;
	return strcmp(alpm_pkg_get_name(pkg1), alpm_pkg_get_name(pkg2));
}

void display_targets(void)
{
	alpm_list_t *i, *targets = NULL;
	alpm_db_t *db_local = alpm_get_localdb(config->handle);

	for(i = alpm_trans_get_add(config->handle); i; i = alpm_list_next(i)) {
		alpm_pkg_t *pkg = i->data;
		pm_target_t *targ = calloc(1, sizeof(pm_target_t));
		if(!targ) return;
		targ->install = pkg;
		targ->remove = alpm_db_get_pkg(db_local, alpm_pkg_get_name(pkg));
		if(alpm_list_find(config->explicit_adds, pkg, pkg_cmp)) {
			targ->is_explicit = 1;
		}
		targets = alpm_list_add(targets, targ);
	}
	for(i = alpm_trans_get_remove(config->handle); i; i = alpm_list_next(i)) {
		alpm_pkg_t *pkg = i->data;
		pm_target_t *targ = calloc(1, sizeof(pm_target_t));
		if(!targ) return;
		targ->remove = pkg;
		if(alpm_list_find(config->explicit_removes, pkg, pkg_cmp)) {
			targ->is_explicit = 1;
		}
		targets = alpm_list_add(targets, targ);
	}

	targets = alpm_list_msort(targets, alpm_list_count(targets), target_cmp);
	_display_targets(targets, config->verbosepkglists);
	FREELIST(targets);
}

static off_t pkg_get_size(alpm_pkg_t *pkg)
{
	switch(config->op) {
		case PM_OP_SYNC:
			return alpm_pkg_download_size(pkg);
		case PM_OP_UPGRADE:
			return alpm_pkg_get_size(pkg);
		default:
			return alpm_pkg_get_isize(pkg);
	}
}

static char *pkg_get_location(alpm_pkg_t *pkg)
{
	alpm_list_t *servers;
	char *string = NULL;
	switch(alpm_pkg_get_origin(pkg)) {
		case ALPM_PKG_FROM_SYNCDB:
			if(alpm_pkg_download_size(pkg) == 0) {
				/* file is already in the package cache */
				alpm_list_t *i;
				const char *pkgfile = alpm_pkg_get_filename(pkg);
				char path[PATH_MAX];
				struct stat buf;

				for(i = alpm_option_get_cachedirs(config->handle); i; i = i->next) {
					snprintf(path, PATH_MAX, "%s%s", (char *)i->data, pkgfile);
					if(stat(path, &buf) == 0 && S_ISREG(buf.st_mode)) {
						pm_asprintf(&string, "file://%s", path);
						return string;
					}
				}
			}

			servers = alpm_db_get_servers(alpm_pkg_get_db(pkg));
			if(servers) {
				pm_asprintf(&string, "%s/%s", (char *)(servers->data),
						alpm_pkg_get_filename(pkg));
				return string;
			}

			/* fallthrough - for theoretical serverless repos */
			__attribute__((fallthrough));

		case ALPM_PKG_FROM_FILE:
			return strdup(alpm_pkg_get_filename(pkg));

		case ALPM_PKG_FROM_LOCALDB:
		default:
			pm_asprintf(&string, "%s-%s", alpm_pkg_get_name(pkg), alpm_pkg_get_version(pkg));
			return string;
	}
}

/* a pow() implementation that is specialized for an integer base and small,
 * positive-only integer exponents. */
static double simple_pow(int base, int exp)
{
	double result = 1.0;
	for(; exp > 0; exp--) {
		result *= base;
	}
	return result;
}

/** Converts sizes in bytes into human readable units.
 *
 * @param bytes the size in bytes
 * @param target_unit '\0' or a short label. If equal to one of the short unit
 * labels ('B', 'K', ...) bytes is converted to target_unit; if '\0', the first
 * unit which will bring the value to below a threshold of 2048 will be chosen.
 * @param precision number of decimal places, ensures -0.00 gets rounded to
 * 0.00; -1 if no rounding desired
 * @param label will be set to the appropriate unit label
 *
 * @return the size in the appropriate unit
 */
double humanize_size(off_t bytes, const char target_unit, int precision,
		const char **label)
{
	static const char *labels[] = {"B", "KiB", "MiB", "GiB",
		"TiB", "PiB", "EiB", "ZiB", "YiB"};
	static const int unitcount = ARRAYSIZE(labels);

	double val = (double)bytes;
	int index;

	for(index = 0; index < unitcount - 1; index++) {
		if(target_unit != '\0' && labels[index][0] == target_unit) {
			break;
		} else if(target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
			break;
		}
		val /= 1024.0;
	}

	if(label) {
		*label = labels[index];
	}

	/* do not display negative zeroes */
	if(precision >= 0 && val < 0.0 &&
			val > (-0.5 / simple_pow(10, precision))) {
		val = 0.0;
	}

	return val;
}

void print_packages(const alpm_list_t *packages)
{
	const alpm_list_t *i;
	if(!config->print_format) {
		config->print_format = strdup("%l");
	}
	for(i = packages; i; i = alpm_list_next(i)) {
		alpm_pkg_t *pkg = i->data;
		char *string = strdup(config->print_format);
		char *temp = string;
		/* %n : pkgname */
		if(strstr(temp, "%n")) {
			string = strreplace(temp, "%n", alpm_pkg_get_name(pkg));
			free(temp);
			temp = string;
		}
		/* %v : pkgver */
		if(strstr(temp, "%v")) {
			string = strreplace(temp, "%v", alpm_pkg_get_version(pkg));
			free(temp);
			temp = string;
		}
		/* %l : location */
		if(strstr(temp, "%l")) {
			char *pkgloc = pkg_get_location(pkg);
			string = strreplace(temp, "%l", pkgloc);
			free(pkgloc);
			free(temp);
			temp = string;
		}
		/* %r : repo */
		if(strstr(temp, "%r")) {
			const char *repo = "local";
			alpm_db_t *db = alpm_pkg_get_db(pkg);
			if(db) {
				repo = alpm_db_get_name(db);
			}
			string = strreplace(temp, "%r", repo);
			free(temp);
			temp = string;
		}
		/* %s : size */
		if(strstr(temp, "%s")) {
			char *size;
			pm_asprintf(&size, "%jd", (intmax_t)pkg_get_size(pkg));
			string = strreplace(temp, "%s", size);
			free(size);
			free(temp);
		}
		printf("%s\n", string);
		free(string);
	}
}

/**
 * Helper function for comparing depends using the alpm "compare func"
 * signature. The function descends through the structure in the following
 * comparison order: name, modifier (e.g., '>', '='), version, description.
 * @param d1 the first depend structure
 * @param d2 the second depend structure
 * @return -1, 0, or 1 if first is <, ==, or > second
 */
static int depend_cmp(const void *d1, const void *d2)
{
	const alpm_depend_t *dep1 = d1;
	const alpm_depend_t *dep2 = d2;
	int ret;

	ret = strcmp(dep1->name, dep2->name);
	if(ret == 0) {
		ret = dep1->mod - dep2->mod;
	}
	if(ret == 0) {
		if(dep1->version && dep2->version) {
			ret = strcmp(dep1->version, dep2->version);
		} else if(!dep1->version && dep2->version) {
			ret = -1;
		} else if(dep1->version && !dep2->version) {
			ret = 1;
		}
	}
	if(ret == 0) {
		if(dep1->desc && dep2->desc) {
			ret = strcmp(dep1->desc, dep2->desc);
		} else if(!dep1->desc && dep2->desc) {
			ret = -1;
		} else if(dep1->desc && !dep2->desc) {
			ret = 1;
		}
	}

	return ret;
}

static char *make_optstring(alpm_depend_t *optdep)
{
	alpm_db_t *localdb = alpm_get_localdb(config->handle);
	char *optstring = alpm_dep_compute_string(optdep);
	char *status = NULL;
	if(alpm_find_satisfier(alpm_db_get_pkgcache(localdb), optstring)) {
		status = _(" [installed]");
	} else if(alpm_find_satisfier(alpm_trans_get_add(config->handle), optstring)) {
		status = _(" [pending]");
	}
	if(status) {
		optstring = realloc(optstring, strlen(optstring) + strlen(status) + 1);
		strcpy(optstring + strlen(optstring), status);
	}
	return optstring;
}

void display_new_optdepends(alpm_pkg_t *oldpkg, alpm_pkg_t *newpkg)
{
	alpm_list_t *i, *old, *new, *optdeps, *optstrings = NULL;

	old = alpm_pkg_get_optdepends(oldpkg);
	new = alpm_pkg_get_optdepends(newpkg);
	optdeps = alpm_list_diff(new, old, depend_cmp);

	/* turn optdepends list into a text list */
	for(i = optdeps; i; i = alpm_list_next(i)) {
		alpm_depend_t *optdep = i->data;
		optstrings = alpm_list_add(optstrings, make_optstring(optdep));
	}

	if(optstrings) {
		printf(_("New optional dependencies for %s\n"), alpm_pkg_get_name(newpkg));
		unsigned short cols = getcols();
		list_display_linebreak("   ", optstrings, cols);
	}

	alpm_list_free(optdeps);
	FREELIST(optstrings);
}

void display_optdepends(alpm_pkg_t *pkg)
{
	alpm_list_t *i, *optdeps, *optstrings = NULL;

	optdeps = alpm_pkg_get_optdepends(pkg);

	/* turn optdepends list into a text list */
	for(i = optdeps; i; i = alpm_list_next(i)) {
		alpm_depend_t *optdep = i->data;
		optstrings = alpm_list_add(optstrings, make_optstring(optdep));
	}

	if(optstrings) {
		printf(_("Optional dependencies for %s\n"), alpm_pkg_get_name(pkg));
		unsigned short cols = getcols();
		list_display_linebreak("   ", optstrings, cols);
	}

	FREELIST(optstrings);
}

static void display_repo_list(const char *dbname, alpm_list_t *list,
		unsigned short cols)
{
	const char *prefix = "  ";
	const colstr_t *colstr = &config->colstr;

	colon_printf(_("Repository %s%s\n"), colstr->repo, dbname);
	list_display(prefix, list, cols);
}

void select_display(const alpm_list_t *pkglist)
{
	const alpm_list_t *i;
	int nth = 1;
	alpm_list_t *list = NULL;
	char *string = NULL;
	const char *dbname = NULL;
	unsigned short cols = getcols();

	for(i = pkglist; i; i = i->next) {
		alpm_pkg_t *pkg = i->data;
		alpm_db_t *db = alpm_pkg_get_db(pkg);

		if(!dbname) {
			dbname = alpm_db_get_name(db);
		}
		if(strcmp(alpm_db_get_name(db), dbname) != 0) {
			display_repo_list(dbname, list, cols);
			FREELIST(list);
			dbname = alpm_db_get_name(db);
		}
		string = NULL;
		pm_asprintf(&string, "%d) %s", nth, alpm_pkg_get_name(pkg));
		list = alpm_list_add(list, string);
		nth++;
	}
	display_repo_list(dbname, list, cols);
	FREELIST(list);
}

static int parseindex(char *s, int *val, int min, int max)
{
	char *endptr = NULL;
	int n = strtol(s, &endptr, 10);
	if(*endptr == '\0') {
		if(n < min || n > max) {
			pm_printf(ALPM_LOG_ERROR,
					_("invalid value: %d is not between %d and %d\n"),
					n, min, max);
			return -1;
		}
		*val = n;
		return 0;
	} else {
		pm_printf(ALPM_LOG_ERROR, _("invalid number: %s\n"), s);
		return -1;
	}
}

static int multiselect_parse(char *array, int count, char *response)
{
	char *str, *saveptr;

	for(str = response; ; str = NULL) {
		int include = 1;
		int start, end;
		size_t len;
		char *ends = NULL;
		char *starts = strtok_r(str, " ,", &saveptr);

		if(starts == NULL) {
			break;
		}
		len = strtrim(starts);
		if(len == 0) {
			continue;
		}

		if(*starts == '^') {
			starts++;
			len--;
			include = 0;
		} else if(str) {
			/* if first token is including, we deselect all targets */
			memset(array, 0, count);
		}

		if(len > 1) {
			/* check for range */
			char *p;
			if((p = strchr(starts + 1, '-'))) {
				*p = 0;
				ends = p + 1;
			}
		}

		if(parseindex(starts, &start, 1, count) != 0) {
			return -1;
		}

		if(!ends) {
			array[start - 1] = include;
		} else {
			if(parseindex(ends, &end, start, count) != 0) {
				return -1;
			}
			do {
				array[start - 1] = include;
			} while(start++ < end);
		}
	}

	return 0;
}

void console_cursor_hide(void) {
	if(isatty(fileno(stdout))) {
		printf(CURSOR_HIDE_ANSICODE);
	}
}

void console_cursor_show(void) {
	if(isatty(fileno(stdout))) {
		printf(CURSOR_SHOW_ANSICODE);
	}
}

char *safe_fgets_stdin(char *s, int size)
{
	char *result;
	console_cursor_show();
	result = safe_fgets(s, size, stdin);
	console_cursor_hide();
	return result;
}

int multiselect_question(char *array, int count)
{
	char *response, *lastchar;
	FILE *stream;
	size_t response_len = 64;

	if(config->noconfirm) {
		stream = stdout;
	} else {
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	response = malloc(response_len);
	if(!response) {
		return -1;
	}
	lastchar = response + response_len - 1;
	/* sentinel byte to later see if we filled up the entire string */
	*lastchar = 1;

	while(1) {
		memset(array, 1, count);

		fprintf(stream, "\n");
		fprintf(stream, _("Enter a selection (default=all)"));
		fprintf(stream, ": ");
		fflush(stream);

		if(config->noconfirm) {
			fprintf(stream, "\n");
			break;
		}

		flush_term_input(fileno(stdin));

		if(safe_fgets_stdin(response, response_len)) {
			const size_t response_incr = 64;
			size_t len;
			/* handle buffer not being large enough to read full line case */
			while(*lastchar == '\0' && lastchar[-1] != '\n') {
				char *new_response;
				response_len += response_incr;
				new_response = realloc(response, response_len);
				if(!new_response) {
					free(response);
					return -1;
				}
				response = new_response;
				lastchar = response + response_len - 1;
				/* sentinel byte */
				*lastchar = 1;
				if(safe_fgets_stdin(response + response_len - response_incr - 1,
							response_incr + 1) == 0) {
					free(response);
					return -1;
				}
			}

			len = strtrim(response);
			if(len > 0) {
				if(multiselect_parse(array, count, response) == -1) {
					/* only loop if user gave an invalid answer */
					continue;
				}
			}
			break;
		} else {
			free(response);
			return -1;
		}
	}

	free(response);
	return 0;
}

int select_question(int count)
{
	char response[32];
	FILE *stream;
	int preset = 1;

	if(config->noconfirm) {
		stream = stdout;
	} else {
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	while(1) {
		fprintf(stream, "\n");
		fprintf(stream, _("Enter a number (default=%d)"), preset);
		fprintf(stream, ": ");
		fflush(stream);

		if(config->noconfirm) {
			fprintf(stream, "\n");
			break;
		}

		flush_term_input(fileno(stdin));

		if(safe_fgets_stdin(response, sizeof(response))) {
			size_t len = strtrim(response);
			if(len > 0) {
				int n;
				if(parseindex(response, &n, 1, count) != 0) {
					continue;
				}
				return (n - 1);
			}
		}
		break;
	}

	return (preset - 1);
}

#define CMP(x, y) ((x) < (y) ? -1 : ((x) > (y) ? 1 : 0))

static int mbscasecmp(const char *s1, const char *s2)
{
	size_t len1 = strlen(s1), len2 = strlen(s2);
	wchar_t c1, c2;
	const char *p1 = s1, *p2 = s2;
	mbstate_t ps1, ps2;
	memset(&ps1, 0, sizeof(mbstate_t));
	memset(&ps2, 0, sizeof(mbstate_t));
	while(*p1 && *p2) {
		size_t b1 = mbrtowc(&c1, p1, len1, &ps1);
		size_t b2 = mbrtowc(&c2, p2, len2, &ps2);
		if(b1 == (size_t) -2 || b1 == (size_t) -1
				|| b2 == (size_t) -2 || b2 == (size_t) -1) {
			/* invalid multi-byte string, fall back to strcasecmp */
			return strcasecmp(p1, p2);
		}
		if(b1 == 0 || b2 == 0) {
			return CMP(c1, c2);
		}
		c1 = towlower(c1);
		c2 = towlower(c2);
		if(c1 != c2) {
			return CMP(c1, c2);
		}
		p1 += b1;
		p2 += b2;
		len1 -= b1;
		len2 -= b2;
	}
	return CMP(*p1, *p2);
}

/* presents a prompt and gets a Y/N answer */
__attribute__((format(printf, 2, 0)))
static int question(short preset, const char *format, va_list args)
{
	char response[32];
	FILE *stream;
	int fd_in = fileno(stdin);

	if(config->noconfirm) {
		stream = stdout;
	} else {
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	/* ensure all text makes it to the screen before we prompt the user */
	fflush(stdout);
	fflush(stderr);

	fputs(config->colstr.colon, stream);
	vfprintf(stream, format, args);

	if(preset) {
		fprintf(stream, " %s ", _("[Y/n]"));
	} else {
		fprintf(stream, " %s ", _("[y/N]"));
	}

	fputs(config->colstr.nocolor, stream);
	fflush(stream);

	if(config->noconfirm) {
		fprintf(stream, "\n");
		return preset;
	}

	flush_term_input(fd_in);

	if(safe_fgets_stdin(response, sizeof(response))) {
		size_t len = strtrim(response);
		if(len == 0) {
			return preset;
		}

		/* if stdin is piped, response does not get printed out, and as a result
		 * a \n is missing, resulting in broken output */
		if(!isatty(fd_in)) {
			fprintf(stream, "%s\n", response);
		}

		if(mbscasecmp(response, _("Y")) == 0 || mbscasecmp(response, _("YES")) == 0) {
			return 1;
		} else if(mbscasecmp(response, _("N")) == 0 || mbscasecmp(response, _("NO")) == 0) {
			return 0;
		}
	}
	return 0;
}

int yesno(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = question(1, format, args);
	va_end(args);

	return ret;
}

int noyes(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = question(0, format, args);
	va_end(args);

	return ret;
}

int colon_printf(const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	fputs(config->colstr.colon, stdout);
	ret = vprintf(fmt, args);
	fputs(config->colstr.nocolor, stdout);
	va_end(args);

	fflush(stdout);
	return ret;
}

int pm_printf(alpm_loglevel_t level, const char *format, ...)
{
	int ret;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	ret = pm_vfprintf(stderr, level, format, args);
	va_end(args);

	return ret;
}

int pm_asprintf(char **string, const char *format, ...)
{
	int ret = 0;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	if(vasprintf(string, format, args) == -1) {
		pm_printf(ALPM_LOG_ERROR, _("failed to allocate string\n"));
		ret = -1;
	}
	va_end(args);

	return ret;
}

int pm_sprintf(char **string, alpm_loglevel_t level, const char *format, ...)
{
	int ret = 0;
	va_list args;

	/* print the message using va_arg list */
	va_start(args, format);
	ret = pm_vasprintf(string, level, format, args);
	va_end(args);

	return ret;
}

int pm_vasprintf(char **string, alpm_loglevel_t level, const char *format, va_list args)
{
	int ret = 0;
	char *msg = NULL;

	/* if current logmask does not overlap with level, do not print msg */
	if(!(config->logmask & level)) {
		return ret;
	}

	/* print the message using va_arg list */
	ret = vasprintf(&msg, format, args);

	/* print a prefix to the message */
	switch(level) {
		case ALPM_LOG_ERROR:
			pm_asprintf(string, "%s%s%s%s", config->colstr.err, _("error: "),
								config->colstr.nocolor, msg);
			break;
		case ALPM_LOG_WARNING:
			pm_asprintf(string, "%s%s%s%s", config->colstr.warn, _("warning: "),
								config->colstr.nocolor, msg);
			break;
		case ALPM_LOG_DEBUG:
			pm_asprintf(string, "debug: %s", msg);
			break;
		case ALPM_LOG_FUNCTION:
			pm_asprintf(string, "function: %s", msg);
			break;
		default:
			pm_asprintf(string, "%s", msg);
			break;
	}
	free(msg);

	return ret;
}

int pm_vfprintf(FILE *stream, alpm_loglevel_t level, const char *format, va_list args)
{
	int ret = 0;

	/* if current logmask does not overlap with level, do not print msg */
	if(!(config->logmask & level)) {
		return ret;
	}

#if defined(PACMAN_DEBUG)
	/* If debug is on, we'll timestamp the output */
	if(config->logmask & ALPM_LOG_DEBUG) {
		time_t t;
		struct tm *tmp;
		char timestr[10] = {0};

		t = time(NULL);
		tmp = localtime(&t);
		strftime(timestr, 9, "%H:%M:%S", tmp);
		timestr[8] = '\0';

		fprintf(stream, "[%s] ", timestr);
	}
#endif

	/* print a prefix to the message */
	switch(level) {
		case ALPM_LOG_ERROR:
			fprintf(stream, "%s%s%s", config->colstr.err, _("error: "),
					config->colstr.nocolor);
			break;
		case ALPM_LOG_WARNING:
			fprintf(stream, "%s%s%s", config->colstr.warn, _("warning: "),
					config->colstr.nocolor);
			break;
		case ALPM_LOG_DEBUG:
			fprintf(stream, "debug: ");
			break;
		case ALPM_LOG_FUNCTION:
			fprintf(stream, "function: ");
			break;
		default:
			break;
	}

	/* print the message using va_arg list */
	ret = vfprintf(stream, format, args);
	return ret;
}

char *arg_to_string(int argc, char *argv[])
{
	char *cl_text, *p;
	size_t size = 0;
	int i;
	for(i = 0; i < argc; i++) {
		size += strlen(argv[i]) + 1;
	}
	if(!size) {
		return NULL;
	}
	if(!(cl_text = malloc(size))) {
		return NULL;
	}
	for(p = cl_text, i = 0; i + 1 < argc; i++) {
		strcpy(p, argv[i]);
		p += strlen(argv[i]);
		*p++ = ' ';
	}
	strcpy(p, argv[i]);
	return cl_text;
}

/* Moves console cursor `lines` up */
void console_cursor_move_up(unsigned int lines)
{
	printf("\x1B[%dF", lines);
}

/* Moves console cursor `lines` down */
void console_cursor_move_down(unsigned int lines)
{
	printf("\x1B[%dE", lines);
}

/* Erases line from the current cursor position till the end of the line */
void console_erase_line(void)
{
	printf("\x1B[K");
}