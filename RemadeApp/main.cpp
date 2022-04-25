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
