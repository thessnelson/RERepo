#include <stdio.h> /* printf */
#include <stdarg.h> /* va_list */

#include <alpm.h>
#include "util.h" /* For Localization */

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