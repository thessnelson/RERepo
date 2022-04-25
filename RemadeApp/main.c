//This will be a completely restructured version of this file bc we need it.
#if defined(GIT_VERSION)
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif
//Libraries
//From pacman.c
#include <stdlib.h> /* atoi */
#include <stdio.h>
#include <ctype.h> /* isspace */
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h> /* uname */
#include <locale.h> /* setlocale */
#include <errno.h>
//From callback.c
#include <sys/time.h> /* gettimeofday */
#include <time.h>
#include <wchar.h>
//Nothing new from check.c
//From conf.c
#include <locale.h> /* setlocale */
#include <fcntl.h> /* open */
#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
//Nothing new from package.c
//From query.c
#include <stdint.h>
//From sync.c
#include <dirent.h>
#include <fnmatch.h>
//From testpkg.c
#include <stdarg.h>
//Nothing new from util-common.c
//From util.c
#include <sys/ioctl.h>
#include <termios.h>
#include <wctype.h>
/* alpm */
#include <alpm.h>
#include <alpm_list.h>

//INTRO FILES (FROM TESTPKG.C)

//UTILITY FILES (FROM UTIL.C)

//MORE UTILITY FILES (FROM UTIL-COMMON)

//CONFIGURATION FILES (FROM CONF.C)

//QUERY FILES (FROM QUERY.C)

//UPDATING FUNCTIONS (FROM SYNC.C)

//MD5 CHECKSUM FUNCTIONS (FROM CHECK.C)

//CALLBACK FUNCTIONS (FROM CALLBACK.C)

//PACKAGE FUNCTIONS (FROM PACKAGE.C)
//handler()

void version() {
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


//MAIN FUNCTIONS (FROM PACMAN.C)
char* mbasename(char* input) {
    return strrchr(input, '/');
}

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