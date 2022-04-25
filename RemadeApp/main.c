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

//I HAVE PASTED UP TO LINE 839!!

//INTRO FILES (FROM TESTPKG.C)

//UTILITY FILES (FROM UTIL.C)
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

//MORE UTILITY FILES (FROM UTIL-COMMON)

//CONFIGURATION FILES (FROM CONF.C)
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

//QUERY FILES (FROM QUERY.C)
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

//UPDATING FUNCTIONS (FROM SYNC.C)

//Collected from reversed.cpp, not from sync.
void dump_pkg_sync(pmpkg_t* pkg_data, pmdb_t* db) {
    if (pkg_data != 0) {
        string_display(gettext("Repository     :"), alpm_pkg_get_name(pkg_data));
        dump_pkg_full(pkg_data, db);
    }

    return;
}

//MD5 CHECKSUM FUNCTIONS (FROM CHECK.C)

//SAME AS CHECK
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