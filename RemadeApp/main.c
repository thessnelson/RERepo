// This will be a completely restructured version of this file bc we need it.
#if defined(GIT_VERSION)
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif
// Libraries
// From pacman.c
#include <stdlib.h> /* atoi */
#include <stdio.h>
#include <ctype.h> /* isspace */
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h> /* uname */
#include <locale.h>		 /* setlocale */
#include <errno.h>
// From callback.c
#include <sys/time.h> /* gettimeofday */
#include <time.h>
#include <wchar.h>
// Nothing new from check.c
// From conf.c
#include <locale.h> /* setlocale */
#include <fcntl.h>	/* open */
#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
// Nothing new from package.c
// From query.c
#include <stdint.h>
// From sync.c
#include <dirent.h>
#include <fnmatch.h>
// From testpkg.c
#include <stdarg.h>
// Nothing new from util-common.c
// From util.c
#include <sys/ioctl.h>
#include <termios.h>
#include <wctype.h>
//from reversing.c
#include <libintl.h> //for gettext
/* alpm */ //QUOTES FOR LOCAL HEADERS
#include "alpm.h"
#include "alpm_list.h"
// SYNC_SYNCTREE IS FROM THE ORIGINAL FILE BC I CANT FIND IT

/*
Major bug that needs fixed.
'op' is not a structure or union.
*/

// GLOBAL VARIABLES FROM EACH FILE
// From Callback.c
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

struct pacman_progress_bar
{
	const char *filename;
	off_t xfered; /* Current amount of transferred data */
	off_t total_size;
	size_t downloaded;
	size_t howmany;
	uint64_t init_time; /* Time when this download started doing any progress */
	uint64_t sync_time; /* Last time we updated the bar info */
	off_t sync_xfered;	/* Amount of transferred data at the `sync_time` timestamp. It can be
						   smaller than `xfered` if we did not update bar UI for a while. */
	double rate;
	unsigned int eta; /* ETA in seconds */
	bool completed;	  /* transfer is completed */
};

// From config.c
config_t *config = NULL;

#define NOCOLOR "\033[0m"

#define BOLD "\033[0;1m"

#define BLACK "\033[0;30m"
#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define BLUE "\033[0;34m"
#define MAGENTA "\033[0;35m"
#define CYAN "\033[0;36m"
#define WHITE "\033[0;37m"

#define BOLDBLACK "\033[1;30m"
#define BOLDRED "\033[1;31m"
#define BOLDGREEN "\033[1;32m"
#define BOLDYELLOW "\033[1;33m"
#define BOLDBLUE "\033[1;34m"
#define BOLDMAGENTA "\033[1;35m"
#define BOLDCYAN "\033[1;36m"
#define BOLDWHITE "\033[1;37m"
#define GREY46 "\033[38;5;243m"

// From package.c
#define CLBUF_SIZE 4096

/* The term "title" refers to the first field of each line in the package
 * information displayed by pacman. Titles are stored in the `titles` array and
 * referenced by the following indices.
 */
enum
{
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

// From pacman
/* list of targets specified on command line */
static alpm_list_t *pm_targets;

// From query.c
#define LOCAL_PREFIX "local/"

// From util.c
static int cached_columns = -1;

// From conf.c
config_t *config_new(void)
{
	config_t *newconfig = calloc(1, sizeof(config_t));
	if (!newconfig)
	{
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
	if (alpm_capabilities() & ALPM_CAPABILITY_SIGNATURES)
	{
		newconfig->siglevel = ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL |
							  ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;
		newconfig->localfilesiglevel = ALPM_SIG_USE_DEFAULT;
		newconfig->remotefilesiglevel = ALPM_SIG_USE_DEFAULT;
	}

	/* by default use 1 download stream */
	newconfig->parallel_downloads = 1;
	newconfig->colstr.colon = ":: ";
	newconfig->colstr.title = "";
	newconfig->colstr.repo = "";
	newconfig->colstr.version = "";
	newconfig->colstr.groups = "";
	newconfig->colstr.meta = "";
	newconfig->colstr.warn = "";
	newconfig->colstr.err = "";
	newconfig->colstr.faint = "";
	newconfig->colstr.nocolor = "";

	return newconfig;
}

// UTILITY FILES (FROM UTIL.C)
// Might be better off with the reversed.cpp version.
int needs_root(void)
{
	if (config->sysroot)
	{ // config undeclared
		return 1;
	}
	switch (config->op)
	{
	case PM_OP_DATABASE: // PM_OP_DATABASE undeclared
		return !config->op_q_check;
	case PM_OP_REMOVE: // PM_OP_REMOVE undeclared
		return !config->print;
	case PM_OP_SYNC: // PM_OP_SYNC undeclared
		return (config->op_s_clean || config->op_s_sync ||
				(!config->group && !config->op_s_info && !config->op_q_list &&
				 !config->op_s_search && !config->print));
	case PM_OP_FILES:
		return config->op_s_sync;
	default:
		return 0;
	}
}

static int getcols_fd(int fd)
{
	int width = -1;

	if (!isatty(fd))
	{
		return 0;
	}

#if defined(TIOCGSIZE)
	struct ttysize win;
	if (ioctl(fd, TIOCGSIZE, &win) == 0)
	{
		width = win.ts_cols;
	}
#elif defined(TIOCGWINSZ)
	struct winsize win;
	if (ioctl(fd, TIOCGWINSZ, &win) == 0)
	{
		width = win.ws_col;
	}
#endif

	if (width <= 0)
	{
		return -EIO;
	}

	return width;
}

unsigned short getcols(void)
{
	const char *e;
	int c = -1;

	if (cached_columns >= 0)
	{ // cached problems undeclared
		return cached_columns;
	}

	e = getenv("COLUMNS");
	if (e && *e)
	{
		char *p = NULL;
		c = strtol(e, &p, 10);
		if (*p != '\0')
		{
			c = -1;
		}
	}

	if (c < 0)
	{
		c = getcols_fd(STDOUT_FILENO); // implicit declaration
	}

	if (c < 0)
	{
		c = 80;
	}

	cached_columns = c;
	return c;
}

static size_t string_length(const char *s)
{
	int len;
	wchar_t *wcstr;

	if (!s || s[0] == '\0')
	{
		return 0;
	}
	if (strstr(s, "\033"))
	{
		char *replaced = malloc(sizeof(char) * strlen(s));
		int iter = 0;
		for (; *s; s++)
		{
			if (*s == '\033')
			{
				while (*s != 'm')
				{
					s++;
				}
			}
			else
			{
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
	}
	else
	{
		/* len goes from # bytes -> # chars -> # cols */
		len = strlen(s) + 1;
		wcstr = calloc(len, sizeof(wchar_t));
		len = mbstowcs(wcstr, s, len);
		len = wcswidth(wcstr, len);
		free(wcstr);
	}

	return len;
}

/* output a string, but wrap words properly with a specified indentation
 */
void indentprint(const char *str, unsigned short indent, unsigned short cols)
{
	wchar_t *wcstr;
	const wchar_t *p;
	size_t len, cidx;

	if (!str)
	{
		return;
	}

	/* if we're not a tty, or our tty is not wide enough that wrapping even makes
	 * sense, print without indenting */
	if (cols == 0 || indent > cols)
	{
		fputs(str, stdout);
		return;
	}

	len = strlen(str) + 1;
	wcstr = calloc(len, sizeof(wchar_t));
	len = mbstowcs(wcstr, str, len);
	p = wcstr;
	cidx = indent;

	if (!p || !len)
	{
		free(wcstr);
		return;
	}

	while (*p)
	{
		if (*p == L' ')
		{
			const wchar_t *q, *next;
			p++;
			if (p == NULL || *p == L' ')
				continue;
			next = wcschr(p, L' ');
			if (next == NULL)
			{
				next = p + wcslen(p);
			}
			/* len captures # cols */
			len = 0;
			q = p;
			while (q < next)
			{
				len += wcwidth(*q++);
			}
			if ((len + 1) > (cols - cidx))
			{
				/* wrap to a newline and reindent */
				printf("\n%-*s", (int)indent, "");
				cidx = indent;
			}
			else
			{
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

void string_display(const char *title, const char *string, unsigned short cols)
{
	if (title)
	{
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}
	if (string == NULL || string[0] == '\0')
	{
		printf(_("None"));
	}
	else
	{
		/* compute the length of title + a space */
		size_t len = string_length(title) + 1;
		indentprint(string, (unsigned short)len, cols);
	}
	printf("\n");
}

void list_display(const char *title, const alpm_list_t *list,
				  unsigned short maxcols)
{
	const alpm_list_t *i;
	size_t len = 0;

	if (title)
	{
		len = string_length(title) + 1;
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}

	if (!list)
	{
		printf("%s\n", _("None"));
	}
	else
	{
		size_t cols = len;
		const char *str = list->data;
		fputs(str, stdout);
		cols += string_length(str);
		for (i = alpm_list_next(list); i; i = alpm_list_next(i))
		{
			str = i->data;
			size_t s = string_length(str);
			/* wrap only if we have enough usable column space */
			if (maxcols > len && cols + s + 2 >= maxcols)
			{
				size_t j;
				cols = len;
				printf("\n");
				for (j = 1; j <= len; j++)
				{
					printf(" ");
				}
			}
			else if (cols != len)
			{
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

	if (title)
	{
		len = (unsigned short)string_length(title) + 1;
		printf("%s%s%s ", config->colstr.title, title, config->colstr.nocolor);
	}

	if (!list)
	{
		printf("%s\n", _("None"));
	}
	else
	{
		const alpm_list_t *i;
		/* Print the first element */
		indentprint((const char *)list->data, len, maxcols);
		printf("\n");
		/* Print the rest */
		for (i = alpm_list_next(list); i; i = alpm_list_next(i))
		{
			size_t j;
			for (j = 1; j <= len; j++)
			{
				printf(" ");
			}
			indentprint((const char *)i->data, len, maxcols);
			printf("\n");
		}
	}
}

void display_optdepends(alpm_pkg_t *pkg)
{
	alpm_list_t *i, *optdeps, *optstrings = NULL;

	optdeps = alpm_pkg_get_optdepends(pkg);

	/* turn optdepends list into a text list */
	for (i = optdeps; i; i = alpm_list_next(i))
	{
		alpm_depend_t *optdep = i->data;
		optstrings = alpm_list_add(optstrings, make_optstring(optdep));
	}

	if (optstrings)
	{
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

	for (i = pkglist; i; i = i->next)
	{
		alpm_pkg_t *pkg = i->data;
		alpm_db_t *db = alpm_pkg_get_db(pkg);

		if (!dbname)
		{
			dbname = alpm_db_get_name(db);
		}
		if (strcmp(alpm_db_get_name(db), dbname) != 0)
		{
			display_repo_list(dbname, list, cols);
			FREELIST(list);
			dbname = alpm_db_get_name(db);
		}
		string = NULL;
		vsprintf(&string, "%d) %s", nth, alpm_pkg_get_name(pkg)); //implicit declaration of pm_asprintf, Did you mean 'vsprintf'?
		list = alpm_list_add(list, string);
		nth++;
	}
	display_repo_list(dbname, list, cols);
	FREELIST(list);
}

/* discard unhandled input on the terminal's input buffer */
static int flush_term_input(int fd)
{
#ifdef HAVE_TCFLUSH
	if (isatty(fd))
	{
		return tcflush(fd, TCIFLUSH);
	}
#endif
	/* fail silently */
	return 0;
}

static int parseindex(char *s, int *val, int min, int max)
{
	char *endptr = NULL;
	int n = strtol(s, &endptr, 10);
	if (*endptr == '\0')
	{
		if (n < min || n > max)
		{
			pm_printf(ALPM_LOG_ERROR,
					  _("invalid value: %d is not between %d and %d\n"),
					  n, min, max);
			return -1;
		}
		*val = n;
		return 0;
	}
	else
	{
		pm_printf(ALPM_LOG_ERROR, _("invalid number: %s\n"), s);
		return -1;
	}
}

int select_question(int count)
{
	char response[32];
	FILE *stream;
	int preset = 1;

	if (config->noconfirm)
	{
		stream = stdout;
	}
	else
	{
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	while (1)
	{
		fprintf(stream, "\n");
		fprintf(stream, _("Enter a number (default=%d)"), preset);
		fprintf(stream, ": ");
		fflush(stream);

		if (config->noconfirm)
		{
			fprintf(stream, "\n");
			break;
		}

		flush_term_input(fileno(stdin));

		if (safe_fgets_stdin(response, sizeof(response)))
		{
			size_t len = strtrim(response);
			if (len > 0)
			{
				int n;
				if (parseindex(response, &n, 1, count) != 0)
				{
					continue;
				}
				return (n - 1);
			}
		}
		break;
	}

	return (preset - 1);
}

// question from reversed.cpp
/* presents a prompt and gets a Y/N answer */
__attribute__((format(printf, 2, 0))) static int question(short preset, const char *format, va_list args)
{
	char response[32];
	FILE *stream;
	int fd_in = fileno(stdin);

	if (config->noconfirm)
	{
		stream = stdout;
	}
	else
	{
		/* Use stderr so questions are always displayed when redirecting output */
		stream = stderr;
	}

	/* ensure all text makes it to the screen before we prompt the user */
	fflush(stdout);
	fflush(stderr);

	fputs(config->colstr.colon, stream);
	vfprintf(stream, format, args);

	if (preset)
	{
		fprintf(stream, " %s ", _("[Y/n]"));
	}
	else
	{
		fprintf(stream, " %s ", _("[y/N]"));
	}

	fputs(config->colstr.nocolor, stream);
	fflush(stream);

	if (config->noconfirm)
	{
		fprintf(stream, "\n");
		return preset;
	}

	flush_term_input(fd_in);

	if (safe_fgets_stdin(response, sizeof(response)))
	{
		size_t len = strtrim(response);
		if (len == 0)
		{
			return preset;
		}

		/* if stdin is piped, response does not get printed out, and as a result
		 * a \n is missing, resulting in broken output */
		if (!isatty(fd_in))
		{
			fprintf(stream, "%s\n", response);
		}

		if (mbscasecmp(response, _("Y")) == 0 || mbscasecmp(response, _("YES")) == 0)
		{
			return 1;
		}
		else if (mbscasecmp(response, _("N")) == 0 || mbscasecmp(response, _("NO")) == 0)
		{
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

int trans_init(int flags, int check_valid)
{
	int ret;

	check_syncdbs(0, check_valid);

	ret = alpm_trans_init(config->handle, flags);
	if (ret == -1)
	{
		trans_init_error();
		return -1;
	}
	return 0;
}

/* does the same thing as 'rm -rf' */
int rmrf(const char *path)
{
	int errflag = 0;
	struct dirent *dp;
	DIR *dirp;

	if (!unlink(path))
	{
		return 0;
	}
	else
	{
		switch (errno)
		{
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
		if (!dirp)
		{
			return 1;
		}
		for (dp = readdir(dirp); dp != NULL; dp = readdir(dirp))
		{
			if (strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0)
			{
				char name[PATH_MAX];
				snprintf(name, PATH_MAX, "%s/%s", path, dp->d_name);
				errflag += rmrf(name);
			}
		}
		closedir(dirp);
		if (rmdir(path))
		{
			errflag++;
		}
		return errflag;
	}
}

// MORE UTILITY FILES (FROM UTIL-COMMON)
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
	if (path == NULL || *path == '\0')
	{
		return strdup(".");
	}

	if ((ret = strdup(path)) == NULL)
	{
		return NULL;
	}

	last = strrchr(ret, '/');

	if (last != NULL)
	{
		/* we found a '/', so terminate our string */
		if (last == ret)
		{
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
/* Trim whitespace and newlines from a string
 */
size_t strtrim(char *str)
{
	char *end, *pch = str;

	if (str == NULL || *str == '\0')
	{
		/* string is empty, so we're done. */
		return 0;
	}

	while (isspace((unsigned char)*pch))
	{
		pch++;
	}
	if (pch != str)
	{
		size_t len = strlen(pch);
		/* check if there wasn't anything but whitespace in the string. */
		if (len == 0)
		{
			*str = '\0';
			return 0;
		}
		memmove(str, pch, len + 1);
		pch = str;
	}

	end = (str + strlen(str) - 1);
	while (isspace((unsigned char)*end))
	{
		end--;
	}
	*++end = '\0';

	return end - pch;
}

// QUERY FILES (FROM QUERY.C)
static int filter(alpm_pkg_t *pkg)
{
	/* check if this package was explicitly installed */
	if (config->op_q_explicit &&
		alpm_pkg_get_reason(pkg) != ALPM_PKG_REASON_EXPLICIT)
	{
		return 0;
	}
	/* check if this package was installed as a dependency */
	if (config->op_q_deps &&
		alpm_pkg_get_reason(pkg) != ALPM_PKG_REASON_DEPEND)
	{
		return 0;
	}
	/* check if this pkg is or isn't in a sync DB */
	if (config->op_q_locality && config->op_q_locality != pkg_get_locality(pkg))
	{
		return 0;
	}
	/* check if this pkg is unrequired */
	if (config->op_q_unrequired && !is_unrequired(pkg, config->op_q_unrequired))
	{
		return 0;
	}
	/* check if this pkg is outdated */
	if (config->op_q_upgrade && (alpm_sync_get_new_version(pkg,
														   alpm_get_syncdbs(config->handle)) == NULL))
	{
		return 0;
	}
	return 1;
}

static int display(alpm_pkg_t *pkg)
{
	int ret = 0;

	if (config->op_q_info)
	{
		if (config->op_q_isfile)
		{
			dump_pkg_full(pkg, 0);
		}
		else
		{
			dump_pkg_full(pkg, config->op_q_info > 1);
		}
	}
	if (config->op_q_list)
	{
		dump_pkg_files(pkg, config->quiet);
	}
	if (config->op_q_changelog)
	{
		dump_pkg_changelog(pkg);
	}
	if (config->op_q_check)
	{
		if (config->op_q_check == 1)
		{
			ret = check_pkg_fast(pkg);
		}
		else
		{
			ret = check_pkg_full(pkg);
		}
	}
	if (!config->op_q_info && !config->op_q_list && !config->op_q_changelog && !config->op_q_check)
	{
		if (!config->quiet)
		{
			const colstr_t *colstr = &config->colstr;
			printf("%s%s %s%s%s", colstr->title, alpm_pkg_get_name(pkg),
				   colstr->version, alpm_pkg_get_version(pkg), colstr->nocolor);

			if (config->op_q_upgrade)
			{
				int usage;
				alpm_pkg_t *newpkg = alpm_sync_get_new_version(pkg, alpm_get_syncdbs(config->handle));
				alpm_db_t *db = alpm_pkg_get_db(newpkg);
				alpm_db_get_usage(db, &usage);

				printf(" -> %s%s%s", colstr->version, alpm_pkg_get_version(newpkg), colstr->nocolor);

				if (alpm_pkg_should_ignore(config->handle, pkg) || !(usage & ALPM_DB_USAGE_UPGRADE))
				{
					printf(" %s", _("[ignored]"));
				}
			}

			printf("\n");
		}
		else
		{
			printf("%s\n", alpm_pkg_get_name(pkg));
		}
	}
	return ret;
}

// resolve_path from reversed.cpp
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

	if (strcmp(bname, ".") == 0 || strcmp(bname, "..") == 0)
	{
		/* the entire path needs to be resolved */
		return realpath(path, resolved_path);
	}

	if (!(dname = mdirname(path)))
	{
		goto cleanup;
	}
	if (!(rpath = realpath(dname, NULL)))
	{
		goto cleanup;
	}
	if (!resolved_path)
	{
		if (!(resolved_path = malloc(strlen(rpath) + strlen(bname) + 2)))
		{
			goto cleanup;
		}
	}

	strcpy(resolved_path, rpath);
	if (resolved_path[strlen(resolved_path) - 1] != '/')
	{
		strcat(resolved_path, "/");
	}
	strcat(resolved_path, bname);
	success = 1;

cleanup:
	free(dname);
	free(rpath);

	return (success ? resolved_path : NULL);
}

static void print_query_fileowner(const char *filename, alpm_pkg_t *info)
{
	if (!config->quiet)
	{
		const colstr_t *colstr = &config->colstr;
		printf(_("%s is owned by %s%s %s%s%s\n"), filename, colstr->title,
			   alpm_pkg_get_name(info), colstr->version, alpm_pkg_get_version(info),
			   colstr->nocolor);
	}
	else
	{
		printf("%s\n", alpm_pkg_get_name(info));
	}
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
	if (targets == NULL)
	{
		pm_printf(ALPM_LOG_ERROR, _("no file was specified for --owns\n"));
		return 1;
	}

	db_local = alpm_get_localdb(config->handle);
	packages = alpm_db_get_pkgcache(db_local);

	for (t = targets; t; t = alpm_list_next(t))
	{
		char *filename = NULL;
		char rpath[PATH_MAX], *rel_path;
		struct stat buf;
		alpm_list_t *i;
		size_t len;
		unsigned int found = 0;
		int is_dir = 0, is_missing = 0;

		if ((filename = strdup(t->data)) == NULL)
		{
			goto targcleanup;
		}

		if (strcmp(filename, "") == 0)
		{
			pm_printf(ALPM_LOG_ERROR, _("empty string passed to file owner query\n"));
			goto targcleanup;
		}

		/* trailing '/' causes lstat to dereference directory symlinks */
		len = strlen(filename) - 1;
		while (len > 0 && filename[len] == '/')
		{
			filename[len--] = '\0';
			/* If a non-dir file exists, S_ISDIR will correct this later. */
			is_dir = 1;
		}

		if (lstat(filename, &buf) == -1)
		{
			is_missing = 1;
			/* if it is not a path but a program name, then check in PATH */
			if ((strchr(filename, '/') == NULL) && (search_path(&filename, &buf) == 0))
			{
				is_missing = 0;
			}
		}

		if (!lrealpath(filename, rpath))
		{
			/* Can't canonicalize path, try to proceed anyway */
			strcpy(rpath, filename);
		}

		if (strncmp(rpath, root, rootlen) != 0)
		{
			/* file is outside root, we know nothing can own it */
			pm_printf(ALPM_LOG_ERROR, _("No package owns %s\n"), filename);
			goto targcleanup;
		}

		rel_path = rpath + rootlen;

		if ((is_missing && is_dir) || (!is_missing && (is_dir = S_ISDIR(buf.st_mode))))
		{
			size_t rlen = strlen(rpath);
			if (rlen + 2 >= PATH_MAX)
			{
				pm_printf(ALPM_LOG_ERROR, _("path too long: %s/\n"), rpath);
				goto targcleanup;
			}
			strcat(rpath + rlen, "/");
		}

		for (i = packages; i && (!found || is_dir); i = alpm_list_next(i))
		{
			if (alpm_filelist_contains(alpm_pkg_get_files(i->data), rel_path))
			{
				print_query_fileowner(rpath, i->data);
				found = 1;
			}
		}
		if (!found)
		{
			pm_printf(ALPM_LOG_ERROR, _("No package owns %s\n"), filename);
		}

	targcleanup:
		if (!found)
		{
			ret++;
		}
		free(filename);
	}

	return ret;
}

static int query_group(alpm_list_t *targets)
{
	alpm_list_t *i, *j;
	const char *grpname = NULL;
	int ret = 0;
	alpm_db_t *db_local = alpm_get_localdb(config->handle);

	if (targets == NULL)
	{
		for (j = alpm_db_get_groupcache(db_local); j; j = alpm_list_next(j))
		{
			alpm_group_t *grp = j->data;
			const alpm_list_t *p;

			for (p = grp->packages; p; p = alpm_list_next(p))
			{
				alpm_pkg_t *pkg = p->data;
				if (!filter(pkg))
				{
					continue;
				}
				printf("%s %s\n", grp->name, alpm_pkg_get_name(pkg));
			}
		}
	}
	else
	{
		for (i = targets; i; i = alpm_list_next(i))
		{
			alpm_group_t *grp;
			grpname = i->data;
			grp = alpm_db_get_group(db_local, grpname);
			if (grp)
			{
				const alpm_list_t *p;
				for (p = grp->packages; p; p = alpm_list_next(p))
				{
					if (!filter(p->data))
					{
						continue;
					}
					if (!config->quiet)
					{
						printf("%s %s\n", grpname,
							   alpm_pkg_get_name(p->data));
					}
					else
					{
						printf("%s\n", alpm_pkg_get_name(p->data));
					}
				}
			}
			else
			{
				pm_printf(ALPM_LOG_ERROR, _("group '%s' was not found\n"), grpname);
				ret++;
			}
		}
	}
	return ret;
}

/* search the local database for a matching package */
static int query_search(alpm_list_t *targets)
{
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	int ret = dump_pkg_search(db_local, targets, 0);
	if (ret == -1)
	{
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, "search failed: %s\n", alpm_strerror(err));
		return 1;
	}

	return ret;
}

// p_query in reversed.cpp
int pacman_query(alpm_list_t *targets)
{
	int ret = 0;
	int match = 0;
	alpm_list_t *i;
	alpm_pkg_t *pkg = NULL;
	alpm_db_t *db_local;

	/* First: operations that do not require targets */

	/* search for a package */
	if (config->op_q_search)
	{
		ret = query_search(targets);
		return ret;
	}

	/* looking for groups */
	if (config->group)
	{
		ret = query_group(targets);
		return ret;
	}

	if (config->op_q_locality || config->op_q_upgrade)
	{
		if (check_syncdbs(1, 1))
		{
			return 1;
		}
	}

	db_local = alpm_get_localdb(config->handle);

	/* operations on all packages in the local DB
	 * valid: no-op (plain -Q), list, info, check
	 * invalid: isfile, owns */
	if (targets == NULL)
	{
		if (config->op_q_isfile || config->op_q_owns)
		{
			pm_printf(ALPM_LOG_ERROR, _("no targets specified (use -h for help)\n"));
			return 1;
		}

		for (i = alpm_db_get_pkgcache(db_local); i; i = alpm_list_next(i))
		{
			pkg = i->data;
			if (filter(pkg))
			{
				int value = display(pkg);
				if (value != 0)
				{
					ret = 1;
				}
				match = 1;
			}
		}
		if (!match)
		{
			ret = 1;
		}
		return ret;
	}

	/* Second: operations that require target(s) */

	/* determine the owner of a file */
	if (config->op_q_owns)
	{
		ret = query_fileowner(targets);
		return ret;
	}

	/* operations on named packages in the local DB
	 * valid: no-op (plain -Q), list, info, check */
	for (i = targets; i; i = alpm_list_next(i))
	{
		const char *strname = i->data;

		/* strip leading part of "local/pkgname" */
		if (strncmp(strname, LOCAL_PREFIX, strlen(LOCAL_PREFIX)) == 0)
		{
			strname += strlen(LOCAL_PREFIX);
		}

		if (config->op_q_isfile)
		{
			alpm_pkg_load(config->handle, strname, 1, 0, &pkg);

			if (pkg == NULL)
			{
				pm_printf(ALPM_LOG_ERROR,
						  _("could not load package '%s': %s\n"), strname,
						  alpm_strerror(alpm_errno(config->handle)));
			}
		}
		else
		{
			pkg = alpm_db_get_pkg(db_local, strname);
			if (pkg == NULL)
			{
				pkg = alpm_find_satisfier(alpm_db_get_pkgcache(db_local), strname);
			}

			if (pkg == NULL)
			{
				pm_printf(ALPM_LOG_ERROR,
						  _("package '%s' was not found\n"), strname);
				if (!config->op_q_isfile && access(strname, R_OK) == 0)
				{
					pm_printf(ALPM_LOG_WARNING,
							  _("'%s' is a file, you might want to use %s.\n"),
							  strname, "-p/--file");
				}
			}
		}

		if (pkg == NULL)
		{
			ret = 1;
			continue;
		}

		if (filter(pkg))
		{
			int value = display(pkg);
			if (value != 0)
			{
				ret = 1;
			}
			match = 1;
		}

		if (config->op_q_isfile)
		{
			alpm_pkg_free(pkg);
			pkg = NULL;
		}
	}

	if (!match)
	{
		ret = 1;
	}

	return ret;
}

// UPDATING FUNCTIONS (FROM SYNC.C)

// cb_trans_conv in reversed.cpp
int sync_prepare_execute(void)
{
	alpm_list_t *i, *packages, *data = NULL;
	int retval = 0;

	/* Step 2: "compute" the transaction based on targets and flags */
	if (alpm_trans_prepare(config->handle, &data) == -1)
	{
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, _("failed to prepare transaction (%s)\n"),
				  alpm_strerror(err));
		switch (err)
		{
		case ALPM_ERR_PKG_INVALID_ARCH:
			for (i = data; i; i = alpm_list_next(i))
			{
				char *pkg = i->data;
				colon_printf(_("package %s does not have a valid architecture\n"), pkg);
				free(pkg);
			}
			break;
		case ALPM_ERR_UNSATISFIED_DEPS:
			for (i = data; i; i = alpm_list_next(i))
			{
				print_broken_dep(i->data);
				alpm_depmissing_free(i->data);
			}
			break;
		case ALPM_ERR_CONFLICTING_DEPS:
			for (i = data; i; i = alpm_list_next(i))
			{
				alpm_conflict_t *conflict = i->data;
				/* only print reason if it contains new information */
				if (conflict->reason->mod == ALPM_DEP_MOD_ANY)
				{
					colon_printf(_("%s and %s are in conflict\n"),
								 conflict->package1, conflict->package2);
				}
				else
				{
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
	if (packages == NULL)
	{
		/* nothing to do: just exit without complaining */
		if (!config->print)
		{
			printf(_(" there is nothing to do\n"));
		}
		goto cleanup;
	}

	/* Step 3: actually perform the operation */
	if (config->print)
	{
		print_packages(packages);
		goto cleanup;
	}

	display_targets();
	printf("\n");

	int confirm;
	if (config->op_s_downloadonly)
	{
		confirm = yesno(_("Proceed with download?"));
	}
	else
	{
		confirm = yesno(_("Proceed with installation?"));
	}
	if (!confirm)
	{
		retval = 1;
		goto cleanup;
	}

	multibar_move_completed_up(true);
	if (alpm_trans_commit(config->handle, &data) == -1)
	{
		alpm_errno_t err = alpm_errno(config->handle);
		pm_printf(ALPM_LOG_ERROR, _("failed to commit transaction (%s)\n"),
				  alpm_strerror(err));
		switch (err)
		{
		case ALPM_ERR_FILE_CONFLICTS:
			for (i = data; i; i = alpm_list_next(i))
			{
				alpm_fileconflict_t *conflict = i->data;
				switch (conflict->type)
				{
				case ALPM_FILECONFLICT_TARGET:
					fprintf(stderr, _("%s exists in both '%s' and '%s'\n"),
							conflict->file, conflict->target, conflict->ctarget);
					break;
				case ALPM_FILECONFLICT_FILESYSTEM:
					if (conflict->ctarget[0])
					{
						fprintf(stderr, _("%s: %s exists in filesystem (owned by %s)\n"),
								conflict->target, conflict->file, conflict->ctarget);
					}
					else
					{
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
			for (i = data; i; i = alpm_list_next(i))
			{
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
	if (trans_release() == -1)
	{
		retval = 1;
	}

	return retval;
}

// Collected from reversed.cpp, not from sync.
void dump_pkg_sync(alpm_pkg_t *pkg_data, alpm_db_t *db)
{
	if (pkg_data != 0)
	{
		string_display(gettext("Repository     :"), alpm_pkg_get_name(pkg_data));
		dump_pkg_full(pkg_data, db);
	}

	return;
}

// Copied direct from reversed.cpp, not from sync.
int sync_synctree(alpm_pkg_t *syncdb, int level)
{
	pmtransflag_t *flags;
	int count = 0;
	i = syncdb;

	if (trans_init(flags) != -1)
	{
		while (i != 0)
		{
			int update_out = alpm_db_update(1 < level, syncdb);

			if (update_out < 0)
			{
				pm_fprintf(gettext("Error: Failed to update database %s. (%s)\n"), alpm_strerrorlast(), alpm_db_get_name(syncdb));
			}
			else if (update_out == 1)
			{
				char *db_name = alpm_db_get_name(syncdb);
				printf((char *)gettext("The database %s is up to date.\n"), db_name);

				count = count + 1;
			}
			else
			{
				count = count + 1;
			}

			i = alpm_list_next(i);
		}

		if (count == 0)
		{
			pm_fprintf(gettext("Error: Could not sync any databases.\n"));
		}
	}

	return count;
}

static int sync_list(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i, *j, *ls = NULL;
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	int ret = 0;

	if (targets)
	{
		for (i = targets; i; i = alpm_list_next(i))
		{
			const char *repo = i->data;
			alpm_db_t *db = NULL;

			for (j = syncs; j; j = alpm_list_next(j))
			{
				alpm_db_t *d = j->data;

				if (strcmp(repo, alpm_db_get_name(d)) == 0)
				{
					db = d;
					break;
				}
			}

			if (db == NULL)
			{
				pm_printf(ALPM_LOG_ERROR,
						  _("repository \"%s\" was not found.\n"), repo);
				ret = 1;
			}

			ls = alpm_list_add(ls, db);
		}
	}
	else
	{
		ls = syncs;
	}

	for (i = ls; i; i = alpm_list_next(i))
	{
		alpm_db_t *db = i->data;

		for (j = alpm_db_get_pkgcache(db); j; j = alpm_list_next(j))
		{
			alpm_pkg_t *pkg = j->data;

			if (!config->quiet)
			{
				const colstr_t *colstr = &config->colstr;
				printf("%s%s %s%s %s%s%s", colstr->repo, alpm_db_get_name(db),
					   colstr->title, alpm_pkg_get_name(pkg),
					   colstr->version, alpm_pkg_get_version(pkg), colstr->nocolor);
				print_installed(db_local, pkg);
				printf("\n");
			}
			else
			{
				printf("%s\n", alpm_pkg_get_name(pkg));
			}
		}
	}

	if (targets)
	{
		alpm_list_free(ls);
	}

	return ret;
}

static int sync_info(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i, *j, *k;
	int ret = 0;

	if (targets)
	{
		for (i = targets; i; i = alpm_list_next(i))
		{
			const char *target = i->data;
			char *name = strdup(target);
			char *repo, *pkgstr;
			int foundpkg = 0, founddb = 0;

			pkgstr = strchr(name, '/');
			if (pkgstr)
			{
				repo = name;
				*pkgstr = '\0';
				++pkgstr;
			}
			else
			{
				repo = NULL;
				pkgstr = name;
			}

			for (j = syncs; j; j = alpm_list_next(j))
			{
				alpm_db_t *db = j->data;
				if (repo && strcmp(repo, alpm_db_get_name(db)) != 0)
				{
					continue;
				}
				founddb = 1;

				for (k = alpm_db_get_pkgcache(db); k; k = alpm_list_next(k))
				{
					alpm_pkg_t *pkg = k->data;

					if (strcmp(alpm_pkg_get_name(pkg), pkgstr) == 0)
					{
						dump_pkg_full(pkg, config->op_s_info > 1);
						foundpkg = 1;
						break;
					}
				}
			}

			if (!founddb)
			{
				pm_printf(ALPM_LOG_ERROR,
						  _("repository '%s' does not exist\n"), repo);
				ret++;
			}
			if (!foundpkg)
			{
				pm_printf(ALPM_LOG_ERROR,
						  _("package '%s' was not found\n"), target);
				ret++;
			}
			free(name);
		}
	}
	else
	{
		for (i = syncs; i; i = alpm_list_next(i))
		{
			alpm_db_t *db = i->data;

			for (j = alpm_db_get_pkgcache(db); j; j = alpm_list_next(j))
			{
				alpm_pkg_t *pkg = j->data;
				dump_pkg_full(pkg, config->op_s_info > 1);
			}
		}
	}

	return ret;
}

/* search the sync dbs for a matching package */
static int sync_search(alpm_list_t *syncs, alpm_list_t *targets)
{
	alpm_list_t *i;
	int found = 0;

	for (i = syncs; i; i = alpm_list_next(i))
	{
		alpm_db_t *db = i->data;
		int ret = dump_pkg_search(db, targets, 1);

		if (ret == -1)
		{
			alpm_errno_t err = alpm_errno(config->handle);
			pm_printf(ALPM_LOG_ERROR, "search failed: %s\n", alpm_strerror(err));
			return 1;
		}

		found += !ret;
	}

	return (found == 0);
}

static int sync_cleancache(int level)
{
	alpm_list_t *i;
	alpm_list_t *sync_dbs = alpm_get_syncdbs(config->handle);
	alpm_db_t *db_local = alpm_get_localdb(config->handle);
	alpm_list_t *cachedirs = alpm_option_get_cachedirs(config->handle);
	int ret = 0;

	if (!config->cleanmethod)
	{
		/* default to KeepInstalled if user did not specify */
		config->cleanmethod = PM_CLEAN_KEEPINST;
	}

	if (level == 1)
	{
		printf(_("Packages to keep:\n"));
		if (config->cleanmethod & PM_CLEAN_KEEPINST)
		{
			printf(_("  All locally installed packages\n"));
		}
		if (config->cleanmethod & PM_CLEAN_KEEPCUR)
		{
			printf(_("  All current sync database packages\n"));
		}
	}
	printf("\n");

	for (i = cachedirs; i; i = alpm_list_next(i))
	{
		const char *cachedir = i->data;
		DIR *dir;
		struct dirent *ent;

		printf(_("Cache directory: %s\n"), (const char *)i->data);

		if (level == 1)
		{
			if (!yesno(_("Do you want to remove all other packages from cache?")))
			{
				printf("\n");
				continue;
			}
			printf(_("removing old packages from cache...\n"));
		}
		else
		{
			if (!noyes(_("Do you want to remove ALL files from cache?")))
			{
				printf("\n");
				continue;
			}
			printf(_("removing all files from cache...\n"));
		}

		dir = opendir(cachedir);
		if (dir == NULL)
		{
			pm_printf(ALPM_LOG_ERROR,
					  _("could not access cache directory %s\n"), cachedir);
			ret++;
			continue;
		}

		rewinddir(dir);
		/* step through the directory one file at a time */
		while ((ent = readdir(dir)) != NULL)
		{
			char path[PATH_MAX];
			int delete = 1;
			alpm_pkg_t *localpkg = NULL, *pkg = NULL;
			const char *local_name, *local_version;

			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			{
				continue;
			}

			if (level <= 1)
			{
				static const char *const glob_skips[] = {
					/* skip signature files - they are removed with their package file */
					"*.sig",
					/* skip package databases within the cache directory */
					"*.db*", "*.files*",
					/* skip source packages within the cache directory */
					"*.src.tar.*"};
				size_t j;

				for (j = 0; j < ARRAYSIZE(glob_skips); j++)
				{
					if (fnmatch(glob_skips[j], ent->d_name, 0) == 0)
					{
						delete = 0;
						break;
					}
				}
				if (delete == 0)
				{
					continue;
				}
			}

			/* build the full filepath */
			snprintf(path, PATH_MAX, "%s%s", cachedir, ent->d_name);

			/* short circuit for removing all files from cache */
			if (level > 1)
			{
				ret += unlink_verbose(path, 0);
				continue;
			}

			/* attempt to load the file as a package. if we cannot load the file,
			 * simply skip it and move on. we don't need a full load of the package,
			 * just the metadata. */
			if (alpm_pkg_load(config->handle, path, 0, 0, &localpkg) != 0)
			{
				pm_printf(ALPM_LOG_DEBUG, "skipping %s, could not load as package\n",
						  path);
				continue;
			}
			local_name = alpm_pkg_get_name(localpkg);
			local_version = alpm_pkg_get_version(localpkg);

			if (config->cleanmethod & PM_CLEAN_KEEPINST)
			{
				/* check if this package is in the local DB */
				pkg = alpm_db_get_pkg(db_local, local_name);
				if (pkg != NULL && alpm_pkg_vercmp(local_version,
												   alpm_pkg_get_version(pkg)) == 0)
				{
					/* package was found in local DB and version matches, keep it */
					pm_printf(ALPM_LOG_DEBUG, "package %s-%s found in local db\n",
							  local_name, local_version);
					delete = 0;
				}
			}
			if (config->cleanmethod & PM_CLEAN_KEEPCUR)
			{
				alpm_list_t *j;
				/* check if this package is in a sync DB */
				for (j = sync_dbs; j && delete; j = alpm_list_next(j))
				{
					alpm_db_t *db = j->data;
					pkg = alpm_db_get_pkg(db, local_name);
					if (pkg != NULL && alpm_pkg_vercmp(local_version,
													   alpm_pkg_get_version(pkg)) == 0)
					{
						/* package was found in a sync DB and version matches, keep it */
						pm_printf(ALPM_LOG_DEBUG, "package %s-%s found in sync db\n",
								  local_name, local_version);
						delete = 0;
					}
				}
			}
			/* free the local file package */
			alpm_pkg_free(localpkg);

			if (delete)
			{
				size_t pathlen = strlen(path);
				ret += unlink_verbose(path, 0);
				/* unlink a signature file if present too */
				if (PATH_MAX - 5 >= pathlen)
				{
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

static int sync_cleandb(const char *dbpath)
{
	DIR *dir;
	struct dirent *ent;
	alpm_list_t *syncdbs;
	int ret = 0;

	dir = opendir(dbpath);
	if (dir == NULL)
	{
		pm_printf(ALPM_LOG_ERROR, _("could not access database directory\n"));
		return 1;
	}

	syncdbs = alpm_get_syncdbs(config->handle);

	rewinddir(dir);
	/* step through the directory one file at a time */
	while ((ent = readdir(dir)) != NULL)
	{
		char path[PATH_MAX];
		struct stat buf;
		int found = 0;
		const char *dname = ent->d_name;
		char *dbname;
		size_t len;
		alpm_list_t *i;

		if (strcmp(dname, ".") == 0 || strcmp(dname, "..") == 0)
		{
			continue;
		}

		/* build the full path */
		snprintf(path, PATH_MAX, "%s%s", dbpath, dname);

		/* remove all non-skipped directories and non-database files */
		if (stat(path, &buf) == -1)
		{
			pm_printf(ALPM_LOG_ERROR, _("could not remove %s: %s\n"),
					  path, strerror(errno));
		}
		if (S_ISDIR(buf.st_mode))
		{
			if (rmrf(path))
			{
				pm_printf(ALPM_LOG_ERROR, _("could not remove %s: %s\n"),
						  path, strerror(errno));
			}
			continue;
		}

		len = strlen(dname);
		if (len > 3 && strcmp(dname + len - 3, ".db") == 0)
		{
			dbname = strndup(dname, len - 3);
		}
		else if (len > 7 && strcmp(dname + len - 7, ".db.sig") == 0)
		{
			dbname = strndup(dname, len - 7);
		}
		else if (len > 6 && strcmp(dname + len - 6, ".files") == 0)
		{
			dbname = strndup(dname, len - 6);
		}
		else if (len > 10 && strcmp(dname + len - 10, ".files.sig") == 0)
		{
			dbname = strndup(dname, len - 10);
		}
		else
		{
			ret += unlink_verbose(path, 0);
			continue;
		}

		for (i = syncdbs; i && !found; i = alpm_list_next(i))
		{
			alpm_db_t *db = i->data;
			found = !strcmp(dbname, alpm_db_get_name(db));
		}

		/* We have a file that doesn't match any syncdb. */
		if (!found)
		{
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
	if (!yesno(_("Do you want to remove unused repositories?")))
	{
		return 0;
	}
	printf(_("removing unused sync repositories...\n"));

	if (vsprintf(&syncdbpath, "%s%s", dbpath, "sync/") < 0) //implicit declaration of asprintf. Did you mean vsprintf?
	{
		ret += 1;
		return ret;
	}
	ret += sync_cleandb(syncdbpath);
	free(syncdbpath);

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

// p_sync in reversed.cpp
int pacman_sync(alpm_list_t *targets)
{
	alpm_list_t *sync_dbs = NULL;

	/* clean the cache */
	if (config->op_s_clean)
	{
		int ret = 0;

		if (trans_init(0, 0) == -1)
		{
			return 1;
		}

		ret += sync_cleancache(config->op_s_clean);
		ret += sync_cleandb_all();

		if (trans_release() == -1)
		{
			ret++;
		}

		return ret;
	}

	if (check_syncdbs(1, 0))
	{
		return 1;
	}

	sync_dbs = alpm_get_syncdbs(config->handle);

	if (config->op_s_sync)
	{
		/* grab a fresh package list */
		colon_printf(_("Synchronizing package databases...\n")); //implicit declaration
		alpm_logaction(config->handle, PACMAN_CALLER_PREFIX,
					   "synchronizing package lists\n");
		if (!sync_syncdbs(config->op_s_sync, sync_dbs))
		{
			return 1;
		}
	}

	if (check_syncdbs(1, 1))
	{
		return 1;
	}

	/* search for a package */
	if (config->op_s_search)
	{
		return sync_search(sync_dbs, targets);
	}

	/* look for groups */
	if (config->group)
	{
		return sync_group(config->group, sync_dbs, targets);
	}

	/* get package info */
	if (config->op_s_info)
	{
		return sync_info(sync_dbs, targets);
	}

	/* get a listing of files in sync DBs */
	if (config->op_q_list)
	{
		return sync_list(sync_dbs, targets);
	}

	if (targets == NULL)
	{
		if (config->op_s_upgrade)
		{
			/* proceed */
		}
		else if (config->op_s_sync)
		{
			return 0;
		}
		else
		{
			/* don't proceed here unless we have an operation that doesn't require a
			 * target list */
			pm_printf(ALPM_LOG_ERROR, _("no targets specified (use -h for help)\n"));
			return 1;
		}
	}

	return sync_trans(targets);
}

// MD5 CHECKSUM FUNCTIONS (FROM CHECK.C)

// SAME AS CHECK
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
	if (rootlen + 1 > PATH_MAX)
	{
		/* we are in trouble here */
		pm_printf(ALPM_LOG_ERROR, _("path too long: %s%s\n"), root, "");
		return 1;
	}

	pkgname = alpm_pkg_get_name(pkg);
	mtree = alpm_pkg_mtree_open(pkg);
	if (mtree == NULL)
	{
		/* TODO: check error to confirm failure due to no mtree file */
		if (!config->quiet)
		{
			printf(_("%s: no mtree file\n"), pkgname);
		}
		return 0;
	}

	while (alpm_pkg_mtree_next(pkg, mtree, &entry) == 0)
	{
		struct stat st;
		const char *path = archive_entry_pathname(entry);
		char filepath[PATH_MAX];
		int filepath_len;
		mode_t type;
		size_t file_errors = 0;
		int backup = 0;
		int exists;

		/* strip leading "./" from path entries */
		if (path[0] == '.' && path[1] == '/')
		{
			path += 2;
		}

		if (*path == '.')
		{
			const char *dbfile = NULL;

			if (strcmp(path, ".INSTALL") == 0)
			{
				dbfile = "install";
			}
			else if (strcmp(path, ".CHANGELOG") == 0)
			{
				dbfile = "changelog";
			}
			else
			{
				continue;
			}

			/* Do not append root directory as alpm_option_get_dbpath is already
			 * an absoute path */
			filepath_len = snprintf(filepath, PATH_MAX, "%slocal/%s-%s/%s",
									alpm_option_get_dbpath(config->handle),
									pkgname, alpm_pkg_get_version(pkg), dbfile);
			if (filepath_len >= PATH_MAX)
			{
				pm_printf(ALPM_LOG_WARNING, _("path too long: %slocal/%s-%s/%s\n"),
						  alpm_option_get_dbpath(config->handle),
						  pkgname, alpm_pkg_get_version(pkg), dbfile);
				continue;
			}
		}
		else
		{
			filepath_len = snprintf(filepath, PATH_MAX, "%s%s", root, path);
			if (filepath_len >= PATH_MAX)
			{
				pm_printf(ALPM_LOG_WARNING, _("path too long: %s%s\n"), root, path);
				continue;
			}
		}

		file_count++;

		exists = check_file_exists(pkgname, filepath, rootlen, &st);
		if (exists == 1)
		{
			errors++;
			continue;
		}
		else if (exists == -1)
		{
			/* NoExtract */
			continue;
		}

		type = archive_entry_filetype(entry);

		if (type != AE_IFDIR && type != AE_IFREG && type != AE_IFLNK)
		{
			pm_printf(ALPM_LOG_WARNING, _("file type not recognized: %s%s\n"), root, path);
			continue;
		}

		if (check_file_type(pkgname, filepath, &st, entry) == 1)
		{
			errors++;
			continue;
		}

		file_errors += check_file_permissions(pkgname, filepath, &st, entry);

		if (type == AE_IFLNK)
		{
			file_errors += check_file_link(pkgname, filepath, &st, entry);
		}

		/* the following checks are expected to fail if a backup file has been
		   modified */
		for (lp = alpm_pkg_get_backup(pkg); lp; lp = lp->next)
		{
			alpm_backup_t *bl = lp->data;

			if (strcmp(path, bl->name) == 0)
			{
				backup = 1;
				break;
			}
		}

		if (type != AE_IFDIR)
		{
			/* file or symbolic link */
			file_errors += check_file_time(pkgname, filepath, &st, entry, backup);
		}

		if (type == AE_IFREG)
		{
			file_errors += check_file_size(pkgname, filepath, &st, entry, backup);
			file_errors += check_file_md5sum(pkgname, filepath, entry, backup);
			file_errors += check_file_sha256sum(pkgname, filepath, entry, backup);
		}

		if (config->quiet && file_errors)
		{
			printf("%s %s\n", pkgname, filepath);
		}

		errors += (file_errors != 0 ? 1 : 0);
	}

	alpm_pkg_mtree_close(pkg, mtree);

	if (!config->quiet)
	{
		printf(_n("%s: %jd total file, ", "%s: %jd total files, ",
				  (unsigned long)file_count),
			   pkgname, (intmax_t)file_count);
		printf(_n("%jd altered file\n", "%jd altered files\n",
				  (unsigned long)errors),
			   (intmax_t)errors);
	}

	return (errors != 0 ? 1 : 0);
}

// CALLBACK FUNCTIONS (FROM CALLBACK.C)
// I AM NOT SURE IF THIS IS CORRECT!!! cb_trans_event in reversed.c
/* callback to handle messages/notifications from libalpm transactions */
void cb_event(void *ctx, alpm_event_t *event)
{
	(void)ctx;
	if (config->print)
	{
		console_cursor_move_end();
		return;
	}
	switch (event->type)
	{
	case ALPM_EVENT_HOOK_START:
		if (event->hook.when == ALPM_HOOK_PRE_TRANSACTION)
		{
			colon_printf(_("Running pre-transaction hooks...\n"));
		}
		else
		{
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
		if (config->noprogressbar)
		{
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
		if (config->noprogressbar)
		{
			alpm_event_package_operation_t *e = &event->package_operation;
			switch (e->operation)
			{
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
		switch (e->operation)
		{
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
		if (config->noprogressbar)
		{
			printf(_("checking package integrity...\n"));
		}
		break;
	case ALPM_EVENT_KEYRING_START:
		if (config->noprogressbar)
		{
			printf(_("checking keyring...\n"));
		}
		break;
	case ALPM_EVENT_KEY_DOWNLOAD_START:
		printf(_("downloading required keys...\n"));
		break;
	case ALPM_EVENT_LOAD_START:
		if (config->noprogressbar)
		{
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

		if (total_enabled)
		{
			init_total_progressbar();
		}
		break;
	case ALPM_EVENT_DISKSPACE_START:
		if (config->noprogressbar)
		{
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
		if (!config->op_s_sync)
		{
			pm_printf(ALPM_LOG_WARNING,
					  "database file for '%s' does not exist (use '%s' to download)\n",
					  event->database_missing.dbname,
					  config->op == PM_OP_FILES ? "-Fy" : "-Sy");
		}
		break;
	case ALPM_EVENT_PACNEW_CREATED:
	{
		alpm_event_pacnew_created_t *e = &event->pacnew_created;
		if (on_progress)
		{
			char *string = NULL;
			pm_sprintf(&string, ALPM_LOG_WARNING, _("%s installed as %s.pacnew\n"),
					   e->file, e->file);
			if (string != NULL)
			{
				output = alpm_list_add(output, string);
			}
		}
		else
		{
			pm_printf(ALPM_LOG_WARNING, _("%s installed as %s.pacnew\n"),
					  e->file, e->file);
		}
	}
	break;
	case ALPM_EVENT_PACSAVE_CREATED:
	{
		alpm_event_pacsave_created_t *e = &event->pacsave_created;
		if (on_progress)
		{
			char *string = NULL;
			pm_sprintf(&string, ALPM_LOG_WARNING, _("%s saved as %s.pacsave\n"),
					   e->file, e->file);
			if (string != NULL)
			{
				output = alpm_list_add(output, string);
			}
		}
		else
		{
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
		if (total_enabled)
		{
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
	if (first_call)
	{
		last_time = get_time_ms();
	}
	else
	{
		int64_t this_time = get_time_ms();
		retval = this_time - last_time;

		/* do not update last_time if interval was too short */
		if (retval < 0 || retval >= UPDATE_SPEED_MS)
		{
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

	if (bar_percent == 0)
	{
		lasthash = 0;
		mouth = 0;
	}

	if (hashlen > 0)
	{
		fputs(" [", stdout);
		for (i = hashlen; i > 0; --i)
		{
			/* if special progress bar enabled */
			if (config->chomp)
			{
				if (i > hashlen - hash)
				{
					putchar('-');
				}
				else if (i == hashlen - hash)
				{
					if (lasthash == hash)
					{
						if (mouth)
						{
							fputs("\033[1;33mC\033[m", stdout);
						}
						else
						{
							fputs("\033[1;33mc\033[m", stdout);
						}
					}
					else
					{
						lasthash = hash;
						mouth = mouth == 1 ? 0 : 1;
						if (mouth)
						{
							fputs("\033[1;33mC\033[m", stdout);
						}
						else
						{
							fputs("\033[1;33mc\033[m", stdout);
						}
					}
				}
				else if (i % 3 == 0)
				{
					fputs("\033[0;37mo\033[m", stdout);
				}
				else
				{
					fputs("\033[0;37m \033[m", stdout);
				}
			} /* else regular progress bar */
			else if (i > hashlen - hash)
			{
				putchar('#');
			}
			else
			{
				putchar('-');
			}
		}
		putchar(']');
	}
	/* print display percent after progress bar */
	/* 5 = 1 space + 3 digits + 1 % */
	if (proglen >= 5)
	{
		printf(" %3d%%", disp_percent);
	}

	putchar('\r');
	fflush(stdout);
}

// PACKAGE FUNCTIONS (FROM PACKAGE.C)
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

void version()
{
	puts("/*@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	puts("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	puts("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	puts("@@@@@@@@@@//////////(/////////////////////@@@@@@@@\n");
	puts("@@@@@(/........................////////////////@@@\n");
	puts("@@//..............................///////////////(\n");
	puts("//................PACMAN..........////////////////\n");
	puts("/,................BY................//////////////\n");
	puts("//................GROUP.4..........///////////////\n");
	puts("@(/(.............................,////////////////\n");
	puts("@@@@//............GET...........///////////////@@@\n");
	puts("@@@@//............THIS..........///////////////@@@\n");
	puts("@@@@//............BREAD.........///////////////@@@\n");
	puts("@@@@//..........................///////////////@@@\n");
	puts("@@@@//..........................///////////////@@@\n");
	puts("@@@@//..........................///////////////@@@\n");
	puts("@@@@//..........................///////////////@@@\n");
	puts("@@@@@@///.................../////////////////@@@@@\n");
	puts("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	puts("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@*/\n");
}

static const char *get_backup_file_status(const char *root,
										  const alpm_backup_t *backup)
{
	char path[PATH_MAX];
	const char *ret;

	snprintf(path, PATH_MAX, "%s%s", root, backup->name);

	/* if we find the file, calculate checksums, otherwise it is missing */
	if (access(path, R_OK) == 0)
	{
		char *md5sum = alpm_compute_md5sum(path);

		if (md5sum == NULL)
		{
			pm_printf(ALPM_LOG_ERROR,
					  _("could not calculate checksums for %s\n"), path);
			return NULL;
		}

		/* if checksums don't match, file has been modified */
		if (strcmp(md5sum, backup->hash) != 0)
		{
			ret = "[modified]";
		}
		else
		{
			ret = "[unmodified]";
		}
		free(md5sum);
	}
	else
	{
		switch (errno)
		{
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
	if (need_alignment)
	{
		need_alignment = 0;
		make_aligned_titles();
	}

	from = alpm_pkg_get_origin(pkg);

	/* set variables here, do all output below */
	bdate = (time_t)alpm_pkg_get_builddate(pkg);
	if (bdate)
	{
		strftime(bdatestr, 50, "%c", localtime(&bdate));
	}
	idate = (time_t)alpm_pkg_get_installdate(pkg);
	if (idate)
	{
		strftime(idatestr, 50, "%c", localtime(&idate));
	}

	switch (alpm_pkg_get_reason(pkg))
	{
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
	if (v)
	{
		if (v & ALPM_PKG_VALIDATION_NONE)
		{
			validation = alpm_list_add(validation, _("None"));
		}
		else
		{
			if (v & ALPM_PKG_VALIDATION_MD5SUM)
			{
				validation = alpm_list_add(validation, _("MD5 Sum"));
			}
			if (v & ALPM_PKG_VALIDATION_SHA256SUM)
			{
				validation = alpm_list_add(validation, _("SHA-256 Sum"));
			}
			if (v & ALPM_PKG_VALIDATION_SIGNATURE)
			{
				validation = alpm_list_add(validation, _("Signature"));
			}
		}
	}
	else
	{
		validation = alpm_list_add(validation, _("Unknown"));
	}

	if (extra || from == ALPM_PKG_FROM_LOCALDB)
	{
		/* compute this here so we don't get a pause in the middle of output */
		requiredby = alpm_pkg_compute_requiredby(pkg);
		optionalfor = alpm_pkg_compute_optionalfor(pkg);
	}

	cols = getcols();

	/* actual output */
	if (from == ALPM_PKG_FROM_SYNCDB)
	{
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

	if (extra || from == ALPM_PKG_FROM_LOCALDB)
	{
		list_display(titles[T_REQUIRED_BY], requiredby, cols);
		list_display(titles[T_OPTIONAL_FOR], optionalfor, cols);
	}
	deplist_display(titles[T_CONFLICTS_WITH], alpm_pkg_get_conflicts(pkg), cols);
	deplist_display(titles[T_REPLACES], alpm_pkg_get_replaces(pkg), cols);

	size = humanize_size(alpm_pkg_get_size(pkg), '\0', 2, &label);
	if (from == ALPM_PKG_FROM_SYNCDB)
	{
		printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_DOWNLOAD_SIZE],
			   config->colstr.nocolor, size, label);
	}
	else if (from == ALPM_PKG_FROM_FILE)
	{
		printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_COMPRESSED_SIZE],
			   config->colstr.nocolor, size, label);
	}
	else
	{
		/* autodetect size for "Installed Size" */
		label = "\0";
	}

	size = humanize_size(alpm_pkg_get_isize(pkg), label[0], 2, &label);
	printf("%s%s%s %.2f %s\n", config->colstr.title, titles[T_INSTALLED_SIZE],
		   config->colstr.nocolor, size, label);

	string_display(titles[T_PACKAGER], alpm_pkg_get_packager(pkg), cols);
	string_display(titles[T_BUILD_DATE], bdatestr, cols);
	if (from == ALPM_PKG_FROM_LOCALDB)
	{
		string_display(titles[T_INSTALL_DATE], idatestr, cols);
		string_display(titles[T_INSTALL_REASON], reason, cols);
	}
	if (from == ALPM_PKG_FROM_FILE || from == ALPM_PKG_FROM_LOCALDB)
	{
		string_display(titles[T_INSTALL_SCRIPT],
					   alpm_pkg_has_scriptlet(pkg) ? _("Yes") : _("No"), cols);
	}

	if (from == ALPM_PKG_FROM_SYNCDB && extra)
	{
		const char *base64_sig = alpm_pkg_get_base64_sig(pkg);
		alpm_list_t *keys = NULL;
		if (base64_sig)
		{
			unsigned char *decoded_sigdata = NULL;
			size_t data_len;
			alpm_decode_signature(base64_sig, &decoded_sigdata, &data_len);
			alpm_extract_keyid(config->handle, alpm_pkg_get_name(pkg),
							   decoded_sigdata, data_len, &keys);
			free(decoded_sigdata);
		}
		else
		{
			keys = alpm_list_add(keys, _("None"));
		}

		string_display(titles[T_MD5_SUM], alpm_pkg_get_md5sum(pkg), cols);
		string_display(titles[T_SHA_256_SUM], alpm_pkg_get_sha256sum(pkg), cols);
		list_display(titles[T_SIGNATURES], keys, cols);

		if (base64_sig)
		{
			FREELIST(keys);
		}
	}
	else
	{
		list_display(titles[T_VALIDATED_BY], validation, cols);
	}

	if (from == ALPM_PKG_FROM_FILE)
	{
		alpm_siglist_t siglist;
		int err = alpm_pkg_check_pgp_signature(pkg, &siglist);
		if (err && alpm_errno(config->handle) == ALPM_ERR_SIG_MISSING)
		{
			string_display(titles[T_SIGNATURES], _("None"), cols);
		}
		else if (err)
		{
			string_display(titles[T_SIGNATURES],
						   alpm_strerror(alpm_errno(config->handle)), cols);
		}
		else
		{
			signature_display(titles[T_SIGNATURES], &siglist, cols);
		}
		alpm_siglist_cleanup(&siglist);
	}

	/* Print additional package info if info flag passed more than once */
	if (from == ALPM_PKG_FROM_LOCALDB && extra)
	{
		dump_pkg_backups(pkg, cols);
	}

	/* final newline to separate packages */
	printf("\n");

	FREELIST(requiredby);
	FREELIST(optionalfor);
	alpm_list_free(validation);
}

/* Display list of backup files and their modification states
 */
void dump_pkg_backups(alpm_pkg_t *pkg, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	const char *root = alpm_option_get_root(config->handle);
	/* package has backup files, so print them */
	for (i = alpm_pkg_get_backup(pkg); i; i = alpm_list_next(i))
	{
		const alpm_backup_t *backup = i->data;
		const char *value;
		char *line;
		size_t needed;
		if (!backup->hash)
		{
			continue;
		}
		value = get_backup_file_status(root, backup);
		needed = strlen(root) + strlen(backup->name) + 1 + strlen(value) + 1;
		line = malloc(needed);
		if (!line)
		{
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

	for (i = 0; i < pkgfiles->count; i++)
	{
		const alpm_file_t *file = pkgfiles->files + i;
		/* Regular: '<pkgname> <root><filepath>\n'
		 * Quiet  : '<root><filepath>\n'
		 */
		if (!quiet)
		{
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

	if ((fp = alpm_pkg_changelog_open(pkg)) == NULL)
	{
		pm_printf(ALPM_LOG_ERROR, _("no changelog available for '%s'.\n"),
				  alpm_pkg_get_name(pkg));
		return;
	}
	else
	{
		fprintf(stdout, _("Changelog for %s:\n"), alpm_pkg_get_name(pkg));
		/* allocate a buffer to get the changelog back in chunks */
		char buf[CLBUF_SIZE];
		size_t ret = 0;
		while ((ret = alpm_pkg_changelog_read(buf, CLBUF_SIZE, pkg, fp)))
		{
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
	if (lpkg)
	{
		const char *lpkgver = alpm_pkg_get_version(lpkg);
		const colstr_t *colstr = &config->colstr;
		if (strcmp(lpkgver, pkgver) == 0)
		{
			printf(" %s[%s]%s", colstr->meta, _("installed"), colstr->nocolor);
		}
		else
		{
			printf(" %s[%s: %s]%s", colstr->meta, _("installed"),
				   lpkgver, colstr->nocolor);
		}
	}
}

// MAIN FUNCTIONS (FROM PACMAN.C)
char *mbasename(char *input)
{
	return strrchr(input, '/');
}

static void usage(int op, const char *const myname)
{
#define addlist(s) (list = alpm_list_add(list, s))
	alpm_list_t *list = NULL, *i;
	/* prefetch some strings for usage below, which moves a lot of calls
	 * out of gettext. */
	char const *const str_opt = _("options");
	char const *const str_file = _("file(s)");
	char const *const str_pkg = _("package(s)");
	char const *const str_usg = _("usage");
	char const *const str_opr = _("operation");

	/* please limit your strings to 80 characters in width */
	if (op == PM_OP_MAIN)
	{
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
	}
	list = alpm_list_msort(list, alpm_list_count(list), options_cmp);
	for (i = list; i; i = alpm_list_next(i))
	{
		fputs((const char *)i->data, stdout);
	}
	alpm_list_free(list);
#undef addlist
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

static void checkargs_files(void)
{
	if(config->op_q_search) {
		invalid_opt(config->op_q_list, "--regex", "--list");
	}
}

/** Helper function for parsing operation from command-line arguments.
 * @param opt Keycode returned by getopt_long
 * @param dryrun If nonzero, application state is NOT changed
 * @return 0 if opt was handled, 1 if it was not handled
 */
static int parsearg_op(int opt, int dryrun)
{
	switch (opt)
	{
	/* operations */
	case 'D':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_DATABASE);
		break;
	case 'F':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_FILES);
		break;
	case 'Q':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_QUERY);
		break;
	case 'R':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_REMOVE);
		break;
	case 'S':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_SYNC);
		break;
	case 'T':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_DEPTEST);
		break;
	case 'U':
		if (dryrun)
			break;
		config->op = (config->op != PM_OP_MAIN ? 0 : PM_OP_UPGRADE);
		break;
	case 'V':
		if (dryrun)
			break;
		config->version = 1;
		break;
	case 'h':
		if (dryrun)
			break;
		config->help = 1;
		break;
	default:
		return 1;
	}
	return 0;
}

static int parsearg_query(int opt)
{
	switch (opt)
	{
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

static int parsearg_sync(int opt)
{
	if (parsearg_upgrade(opt) == 0)
	{
		return 0;
	}
	switch (opt)
	{
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

static int parseargs(int argc, char *argv[])
{
	int opt;
	int option_index = 0;
	int result;
	const char *optstring = "DFQRSTUVb:cdefghiklmnopqr:stuvwxy";
	static const struct option opts[] =
		{
			{"database", no_argument, 0, 'D'},
			{"files", no_argument, 0, 'F'},
			{"query", no_argument, 0, 'Q'},
			{"remove", no_argument, 0, 'R'},
			{"sync", no_argument, 0, 'S'},
			{"deptest", no_argument, 0, 'T'}, /* used by makepkg */
			{"upgrade", no_argument, 0, 'U'},
			{"version", no_argument, 0, 'V'},
			{"help", no_argument, 0, 'h'},

			{"dbpath", required_argument, 0, OP_DBPATH},
			{"cascade", no_argument, 0, OP_CASCADE},
			{"changelog", no_argument, 0, OP_CHANGELOG},
			{"clean", no_argument, 0, OP_CLEAN},
			{"nodeps", no_argument, 0, OP_NODEPS},
			{"deps", no_argument, 0, OP_DEPS},
			{"explicit", no_argument, 0, OP_EXPLICIT},
			{"groups", no_argument, 0, OP_GROUPS},
			{"info", no_argument, 0, OP_INFO},
			{"check", no_argument, 0, OP_CHECK},
			{"list", no_argument, 0, OP_LIST},
			{"foreign", no_argument, 0, OP_FOREIGN},
			{"native", no_argument, 0, OP_NATIVE},
			{"nosave", no_argument, 0, OP_NOSAVE},
			{"owns", no_argument, 0, OP_OWNS},
			{"file", no_argument, 0, OP_FILE},
			{"print", no_argument, 0, OP_PRINT},
			{"quiet", no_argument, 0, OP_QUIET},
			{"root", required_argument, 0, OP_ROOT},
			{"sysroot", required_argument, 0, OP_SYSROOT},
			{"recursive", no_argument, 0, OP_RECURSIVE},
			{"search", no_argument, 0, OP_SEARCH},
			{"regex", no_argument, 0, OP_REGEX},
			{"machinereadable", no_argument, 0, OP_MACHINEREADABLE},
			{"unrequired", no_argument, 0, OP_UNREQUIRED},
			{"upgrades", no_argument, 0, OP_UPGRADES},
			{"sysupgrade", no_argument, 0, OP_SYSUPGRADE},
			{"unneeded", no_argument, 0, OP_UNNEEDED},
			{"verbose", no_argument, 0, OP_VERBOSE},
			{"downloadonly", no_argument, 0, OP_DOWNLOADONLY},
			{"refresh", no_argument, 0, OP_REFRESH},
			{"noconfirm", no_argument, 0, OP_NOCONFIRM},
			{"confirm", no_argument, 0, OP_CONFIRM},
			{"config", required_argument, 0, OP_CONFIG},
			{"ignore", required_argument, 0, OP_IGNORE},
			{"assume-installed", required_argument, 0, OP_ASSUMEINSTALLED},
			{"debug", optional_argument, 0, OP_DEBUG},
			{"force", no_argument, 0, OP_FORCE},
			{"overwrite", required_argument, 0, OP_OVERWRITE_FILES},
			{"noprogressbar", no_argument, 0, OP_NOPROGRESSBAR},
			{"noscriptlet", no_argument, 0, OP_NOSCRIPTLET},
			{"ask", required_argument, 0, OP_ASK},
			{"cachedir", required_argument, 0, OP_CACHEDIR},
			{"hookdir", required_argument, 0, OP_HOOKDIR},
			{"asdeps", no_argument, 0, OP_ASDEPS},
			{"logfile", required_argument, 0, OP_LOGFILE},
			{"ignoregroup", required_argument, 0, OP_IGNOREGROUP},
			{"needed", no_argument, 0, OP_NEEDED},
			{"asexplicit", no_argument, 0, OP_ASEXPLICIT},
			{"arch", required_argument, 0, OP_ARCH},
			{"print-format", required_argument, 0, OP_PRINTFORMAT},
			{"gpgdir", required_argument, 0, OP_GPGDIR},
			{"dbonly", no_argument, 0, OP_DBONLY},
			{"color", required_argument, 0, OP_COLOR},
			{"disable-download-timeout", no_argument, 0, OP_DISABLEDLTIMEOUT},
			{0, 0, 0, 0}};

	/* parse operation */
	while ((opt = getopt_long(argc, argv, optstring, opts, &option_index)) != -1)
	{
		if (opt == 0)
		{
			continue;
		}
		else if (opt == '?')
		{
			/* unknown option, getopt printed an error */
			return 1;
		}
		parsearg_op(opt, 0);
	}

	if (config->op == 0)
	{
		pm_printf(ALPM_LOG_ERROR, _("only one operation may be used at a time\n"));
		return 1;
	}
	if (config->help)
	{
		usage(config->op, mbasename(argv[0]));
		cleanup(0);
	}
	if (config->version)
	{
		version();
		cleanup(0);
	}

	/* parse all other options */
	optind = 1;
	while ((opt = getopt_long(argc, argv, optstring, opts, &option_index)) != -1)
	{
		if (opt == 0)
		{
			continue;
		}
		else if (opt == '?')
		{
			/* this should have failed during first pass already */
			return 1;
		}
		else if (parsearg_op(opt, 1) == 0)
		{
			/* opt is an operation */
			continue;
		}

		switch (config->op)
		{
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
		if (result == 0)
		{
			continue;
		}

		/* fall back to global options */
		result = parsearg_global(opt);
		if (result != 0)
		{
			/* global option parsing failed, abort */
			if (opt < OP_LONG_FLAG_MIN)
			{
				pm_printf(ALPM_LOG_ERROR, _("invalid option '-%c'\n"), opt);
			}
			else
			{
				pm_printf(ALPM_LOG_ERROR, _("invalid option '--%s'\n"),
						  opts[option_index].name);
			}
			return result;
		}
	}

	while (optind < argc)
	{
		/* add the target to our target array */
		pm_targets = alpm_list_add(pm_targets, strdup(argv[optind]));
		optind++;
	}

	switch (config->op)
	{
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

// MAIN FILES (FROM TESTPKG.C)

// Nevermind, main is original. Closest is still from testpkg.c tho.
int main(int argc, char **argv)
{
	// Set up signals
	struct sigaction new_action, old_action;
	new_action.sa_handler = handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	sigaction(2, NULL, &old_action);

	if (old_action.sa_handler != SIG_IGN)
	{
		sigaction(SIGINT, &new_action, NULL);
	}
	sigaction(SIGHUP, NULL, &old_action);

	if (old_action.sa_handler != SIG_IGN)
	{
		sigaction(SIGHUP, &new_action, NULL);
	}
	sigaction(SIGTERM, NULL, &old_action);

	if (old_action.sa_handler != SIG_IGN)
	{
		sigaction(SIGTERM, &new_action, NULL);
	}

	// alpm setup
	struct utsname buf;

	localize();
	uname(buf);

	char buffer[100];
	snprintf(buffer, 100, "pkgmis/%s (%s %s) libalpm/%s", "3.5.4", buf, buf.machine, alpm_version());
	setenv("HTTP_USER_AGENT", buffer, 0);
	config_new();

	if (alpm_initialize() == -1)
	{
		pm_printf(gettext("Error: Necessary libraries could not be initialized\n"));
		exit(1);
	}

	alpm_option_set_root('/');
	alpm_option_set_dbpath("/opt/pkgmis/var/lib/pkgmis/");
	alpm_option_set_logfile();

	parseargs(argc, argv);

	int stdin_no = fileno(stdin);
	stdin_no = isatty(stdin_no);

	char temp_arr[4104];
	ushort **temp_arr_ref;

	if (stdin_no == 0)
	{
		alpm_list_t *temp = alpm_list_find_str(pm_targets, '-');

		if (temp != NULL)
		{
			int i = 0;
			pm_targets = alpm_list_remove_str(pm_targets, '-', 0);

			while (i < 0x1000)
			{
				stdin_no = fgetc(stdin);
				temp_arr[i] = (char)stdin_no;

				if (temp_arr[i] == 0xff)
				{
					break;
				}

				if ((*temp_arr_ref)[temp_arr[i]] & 0x2000 == 0)
				{
					i++;
				}
				else if (i > 0)
				{
					temp_arr[i] = 0;
					char *temp_str = strdup((char *)temp_arr);

					pm_targets = alpm_list_add(pm_targets, temp_str);
					i = 0;
				}
			}

			if (i > 0xfff)
			{
				pm_printf(gettext("Error: Buffer overflow detected while parsing arguments!\n"));
				exit(1);
			}

			if (i > 0)
			{
				temp_arr[i] = 0;
				char *temp_str = strdup((char *)temp_arr);

				pm_targets = alpm_list_add(pm_targets, temp_str);
			}

			FILE *ss = stdin;
			char *cter_out = ctermid((char *)NULL);
			ss = freopen(cter_out, "r", ss);

			if (ss == NULL)
			{
				pm_printf(gettext("Error: stdin could not be reopened.\n"));
			}
		}
	}

	// Root access commands
	parseconfig();
	needs_root();

	if (*config == 4)
	{
		pacman_query(pm_targets);
	}
	else if (*config == 5)
	{
		pacman_sync(pm_targets);
	}
	else
	{
		pm_printf(gettext("Error: No operation was specified. Use the -h flag for help.\n"));
		exit(1);
	}

	return 0;
}