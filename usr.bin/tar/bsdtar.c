/*-
 * Copyright (c) 2003-2004 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#else
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};
#define	no_argument 0
#define	required_argument 1
#endif
#ifdef HAVE_NL_LANGINFO_D_MD_ORDER
#include <langinfo.h>
#endif
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bsdtar.h"

static int		 bsdtar_getopt(struct bsdtar *, const char *optstring,
    const struct option **poption);
static void		 long_help(struct bsdtar *);
static void		 only_mode(struct bsdtar *, char mode, const char *opt,
			     const char *valid);
static char **		 rewrite_argv(struct bsdtar *,
			     int *argc, char ** src_argv,
			     const char *optstring);

/*
 * The leading '+' here forces the GNU version of getopt() (as well as
 * both the GNU and BSD versions of getopt_long) to stop at the first
 * non-option.  Otherwise, GNU getopt() permutes the arguments and
 * screws up -C processing.
 */
const char *tar_opts = "+Bb:C:cF:f:HhjkLlmnOoPprtT:UuvW:wX:xyZz";

/*
 * Most of these long options are deliberately not documented.  They
 * are provided only to make life easier for people who also use GNU tar.
 * The only long options documented in the manual page are the ones
 * with no corresponding short option, such as --exclude, --nodump,
 * and --fast-read.
 *
 * On systems that lack getopt_long, long options can be specified
 * using -W longopt and -W longopt=value, e.g. "-W nodump" is the same
 * as "--nodump" and "-W exclude=pattern" is the same as "--exclude
 * pattern".  This does not rely the GNU getopt() "W;" extension, so
 * should work correctly on any system with a POSIX-compliant getopt().
 */

/* Fake short equivalents for long options that otherwise lack them. */
#define	OPTION_EXCLUDE 1
#define	OPTION_FAST_READ 2
#define	OPTION_HELP 3
#define	OPTION_INCLUDE 4
#define	OPTION_NODUMP 5
#define	OPTION_NO_SAME_PERMISSIONS 6
#define	OPTION_NULL 7
#define	OPTION_ONE_FILE_SYSTEM 8

const struct option tar_longopts[] = {
	{ "absolute-paths",     no_argument,       NULL, 'P' },
	{ "append",             no_argument,       NULL, 'r' },
	{ "block-size",         required_argument, NULL, 'b' },
	{ "bunzip2",            no_argument,       NULL, 'j' },
	{ "bzip",               no_argument,       NULL, 'j' },
	{ "bzip2",              no_argument,       NULL, 'j' },
	{ "cd",                 required_argument, NULL, 'C' },
	{ "confirmation",       no_argument,       NULL, 'w' },
	{ "create",             no_argument,       NULL, 'c' },
	{ "dereference",	no_argument,	   NULL, 'L' },
	{ "directory",          required_argument, NULL, 'C' },
	{ "exclude",            required_argument, NULL, OPTION_EXCLUDE },
	{ "exclude-from",       required_argument, NULL, 'X' },
	{ "extract",            no_argument,       NULL, 'x' },
	{ "fast-read",          no_argument,       NULL, OPTION_FAST_READ },
	{ "file",               required_argument, NULL, 'f' },
	{ "format",             required_argument, NULL, 'F' },
	{ "gunzip",             no_argument,       NULL, 'z' },
	{ "gzip",               no_argument,       NULL, 'z' },
	{ "help",               no_argument,       NULL, OPTION_HELP },
	{ "include",            required_argument, NULL, OPTION_INCLUDE },
	{ "interactive",        no_argument,       NULL, 'w' },
	{ "keep-old-files",     no_argument,       NULL, 'k' },
	{ "list",               no_argument,       NULL, 't' },
	{ "modification-time",  no_argument,       NULL, 'm' },
	{ "nodump",             no_argument,       NULL, OPTION_NODUMP },
	{ "norecurse",          no_argument,       NULL, 'n' },
	{ "no-recursion",       no_argument,       NULL, 'n' },
	{ "no-same-owner",	no_argument,	   NULL, 'o' },
	{ "no-same-permissions",no_argument,	   NULL, OPTION_NO_SAME_PERMISSIONS },
	{ "null",		no_argument,	   NULL, OPTION_NULL },
	{ "one-file-system",	no_argument,	   NULL, OPTION_ONE_FILE_SYSTEM },
	{ "preserve-permissions", no_argument,     NULL, 'p' },
	{ "read-full-blocks",	no_argument,	   NULL, 'B' },
	{ "same-permissions",   no_argument,       NULL, 'p' },
	{ "to-stdout",          no_argument,       NULL, 'O' },
	{ "unlink",		no_argument,       NULL, 'U' },
	{ "unlink-first",	no_argument,       NULL, 'U' },
	{ "update",             no_argument,       NULL, 'u' },
	{ "verbose",            no_argument,       NULL, 'v' },
	{ NULL, 0, NULL, 0 }
};

int
main(int argc, char **argv)
{
	struct bsdtar		*bsdtar, bsdtar_storage;
	const struct option	*option;
	struct passwd		*pwent;
	int			 opt;
	char			 mode;
	char			 possible_help_request;
	char			 buff[16];

	/*
	 * Use a pointer for consistency, but stack-allocated storage
	 * for ease of cleanup.
	 */
	bsdtar = &bsdtar_storage;
	memset(bsdtar, 0, sizeof(*bsdtar));
	bsdtar->fd = -1; /* Mark as "unused" */

	if (setlocale(LC_ALL, "") == NULL)
		bsdtar_warnc(bsdtar, 0, "Failed to set default locale");
#ifdef HAVE_NL_LANGINFO_D_MD_ORDER
	bsdtar->day_first = (*nl_langinfo(D_MD_ORDER) == 'd');
#endif
	mode = '\0';
	possible_help_request = 0;

	/* Look up uid/uname of current user for future reference */
	bsdtar->user_uid = geteuid();
	bsdtar->user_uname = NULL;
	if ((pwent = getpwuid(bsdtar->user_uid))) {
		bsdtar->user_uname = (char *)malloc(strlen(pwent->pw_name)+1);
		if (bsdtar->user_uname)
			strcpy(bsdtar->user_uname, pwent->pw_name);
	}

	/* Default: open tape drive. */
	bsdtar->filename = getenv("TAPE");
	if (bsdtar->filename == NULL)
		bsdtar->filename = _PATH_DEFTAPE;

	/* Default: preserve mod time on extract */
	bsdtar->extract_flags = ARCHIVE_EXTRACT_TIME;

	if (bsdtar->user_uid == 0)
		bsdtar->extract_flags |= ARCHIVE_EXTRACT_OWNER;

	bsdtar->progname = strrchr(*argv, '/');
	if (bsdtar->progname != NULL)
		bsdtar->progname++;
	else
		bsdtar->progname = *argv;

	/* Rewrite traditional-style tar arguments, if used. */
	argv = rewrite_argv(bsdtar, &argc, argv, tar_opts);

	bsdtar->argv = argv;
	bsdtar->argc = argc;

	/* Process all remaining arguments now. */
        while ((opt = bsdtar_getopt(bsdtar, tar_opts, &option)) != -1) {
		switch (opt) {
		case 'B': /* GNU tar */
			/* libarchive doesn't need this; just ignore it. */
			break;
		case 'b': /* SUSv2 */
			bsdtar->bytes_per_block = 512 * atoi(optarg);
			break;
		case 'C': /* GNU tar */
			/* Defer first -C until after -f is opened. */
			bsdtar->start_dir = optarg;
			break;
		case 'c': /* SUSv2 */
			if (mode != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c",
				    opt, mode);
			mode = opt;
			break;
		case OPTION_EXCLUDE: /* GNU tar */
			exclude(bsdtar, optarg);
			break;
		case 'F':
			bsdtar->create_format = optarg;
			break;
		case 'f': /* SUSv2 */
			bsdtar->filename = optarg;
			if (strcmp(bsdtar->filename, "-") == 0)
				bsdtar->filename = NULL;
			break;
		case OPTION_FAST_READ: /* GNU tar */
			bsdtar->option_fast_read = 1;
			break;
		case 'H': /* BSD convention */
			bsdtar->symlink_mode = 'H';
			break;
		case 'h': /* Linux Standards Base, gtar; synonym for -L */
			bsdtar->symlink_mode = 'L';
			/* Hack: -h by itself is the "help" command. */
			possible_help_request = 1;
			break;
		case OPTION_HELP:
			long_help(bsdtar);
			exit(0);
			break;
		case OPTION_INCLUDE:
			include(bsdtar, optarg);
			break;
		case 'j': /* GNU tar */
			if (bsdtar->create_compression != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c", opt,
				    bsdtar->create_compression);
			bsdtar->create_compression = opt;
			break;
		case 'k': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE;
			break;
		case 'L': /* BSD convention */
			bsdtar->symlink_mode = 'L';
			break;
	        case 'l': /* SUSv2; note that GNU -l conflicts */
			bsdtar->option_warn_links = 1;
			break;
		case 'm': /* SUSv2 */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_TIME;
			break;
		case 'n': /* GNU tar */
			bsdtar->option_no_subdirs = 1;
			break;
		case OPTION_NODUMP: /* star */
			bsdtar->option_honor_nodump = 1;
			break;
		case OPTION_NO_SAME_PERMISSIONS: /* GNU tar */
			/*
			 * This is always the default in FreeBSD's
			 * version of GNU tar; it's also the default
			 * behavior for bsdtar, so treat the
			 * command-line option as a no-op.
			 */
			break;
		case OPTION_NULL: /* GNU tar */
			bsdtar->option_null++;
			break;
		case 'O': /* GNU tar */
			bsdtar->option_stdout = 1;
			break;
		case 'o': /* SUSv2; note that GNU -o conflicts */
			bsdtar->extract_flags &= ~ARCHIVE_EXTRACT_OWNER;
			break;
		case OPTION_ONE_FILE_SYSTEM: /* -l in GNU tar */
			bsdtar->option_dont_traverse_mounts = 1;
			break;
#if 0
		/*
		 * The common BSD -P option is not necessary, since
		 * our default is to archive symlinks, not follow
		 * them.  This is convenient, as -P conflicts with GNU
		 * tar anyway.
		 */
		case 'P': /* BSD convention */
			/* Default behavior, no option necessary. */
			break;
#endif
		case 'P': /* GNU tar */
			bsdtar->option_absolute_paths = 1;
			break;
		case 'p': /* GNU tar, star */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_PERM;
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_ACL;
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_FFLAGS;
			break;
		case 'r': /* SUSv2 */
			if (mode != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c",
				    opt, mode);
			mode = opt;
			break;
		case 'T': /* GNU tar */
			bsdtar->names_from_file = optarg;
			break;
		case 't': /* SUSv2 */
			if (mode != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c",
				    opt, mode);
			mode = opt;
			bsdtar->verbose++;
			break;
		case 'U': /* GNU tar */
			bsdtar->extract_flags |= ARCHIVE_EXTRACT_UNLINK;
			bsdtar->option_unlink_first = 1;
			break;
		case 'u': /* SUSv2 */
			if (mode != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c",
				    opt, mode);
			mode = opt;
			break;
		case 'v': /* SUSv2 */
			bsdtar->verbose++;
			break;
		case 'w': /* SUSv2 */
			bsdtar->option_interactive = 1;
			break;
		case 'X': /* GNU tar */
			exclude_from_file(bsdtar, optarg);
			break;
		case 'x': /* SUSv2 */
			if (mode != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c",
				    opt, mode);
			mode = opt;
			break;
		case 'y': /* FreeBSD version of GNU tar */
			if (bsdtar->create_compression != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c", opt,
				    bsdtar->create_compression);
			bsdtar->create_compression = opt;
			break;
		case 'Z': /* GNU tar */
			if (bsdtar->create_compression != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c", opt,
				    bsdtar->create_compression);
			bsdtar->create_compression = opt;
			break;
		case 'z': /* GNU tar, star, many others */
			if (bsdtar->create_compression != '\0')
				bsdtar_errc(bsdtar, 1, 0,
				    "Can't specify both -%c and -%c", opt,
				    bsdtar->create_compression);
			bsdtar->create_compression = opt;
			break;
		default:
			usage(bsdtar);
		}
	}

	/*
	 * Sanity-check options.
	 */
	if (mode == '\0' && possible_help_request) {
		long_help(bsdtar);
		exit(0);
	}

	if (mode == '\0')
		bsdtar_errc(bsdtar, 1, 0,
		    "Must specify one of -c, -r, -t, -u, -x");

	/* Check boolean options only permitted in certain modes. */
	if (bsdtar->option_absolute_paths)
		only_mode(bsdtar, mode, "-P", "xcru");
	if (bsdtar->option_dont_traverse_mounts)
		only_mode(bsdtar, mode, "-X", "cru");
	if (bsdtar->option_fast_read)
		only_mode(bsdtar, mode, "--fast-read", "xt");
	if (bsdtar->option_honor_nodump)
		only_mode(bsdtar, mode, "--nodump", "cru");
	if (bsdtar->option_no_subdirs)
		only_mode(bsdtar, mode, "-n", "cru");
	if (bsdtar->option_stdout)
		only_mode(bsdtar, mode, "-O", "x");
	if (bsdtar->option_warn_links)
		only_mode(bsdtar, mode, "-l", "cr");

	/* Check other parameters only permitted in certain modes. */
	if (bsdtar->create_compression == 'Z' && mode == 'c') {
		bsdtar_warnc(bsdtar, 0, ".Z compression not supported");
		usage(bsdtar);
	}
	if (bsdtar->create_compression != '\0') {
		strcpy(buff, "-?");
		buff[1] = bsdtar->create_compression;
		only_mode(bsdtar, mode, buff, "cxt");
	}
	if (bsdtar->create_format != NULL)
		only_mode(bsdtar, mode, "-F", "c");
	if (bsdtar->symlink_mode != '\0') {
		strcpy(buff, "-?");
		buff[1] = bsdtar->symlink_mode;
		only_mode(bsdtar, mode, buff, "cru");
	}

        bsdtar->argc -= optind;
        bsdtar->argv += optind;

	switch(mode) {
	case 'c':
		tar_mode_c(bsdtar);
		break;
	case 'r':
		tar_mode_r(bsdtar);
		break;
	case 't':
		tar_mode_t(bsdtar);
		break;
	case 'u':
		tar_mode_u(bsdtar);
		break;
	case 'x':
		tar_mode_x(bsdtar);
		break;
	}

	if (bsdtar->user_uname != NULL)
		free(bsdtar->user_uname);

	cleanup_exclusions(bsdtar);
	return (bsdtar->return_value);
}

/*
 * Verify that the mode is correct.
 */
static void
only_mode(struct bsdtar *bsdtar, char mode,
    const char *opt, const char *valid_modes)
{
	if (strchr(valid_modes, mode) == NULL)
		bsdtar_errc(bsdtar, 1, 0,
		    "Option %s is not permitted in mode -%c",
		    opt, mode);
}


/*-
 * Convert traditional tar arguments into new-style.
 * For example,
 *     tar tvfb file.tar 32 --exclude FOO
 * will be converted to
 *     tar -t -v -f file.tar -b 32 --exclude FOO
 *
 * This requires building a new argv array.  The initial bundled word
 * gets expanded into a new string that looks like "-t\0-v\0-f\0-b\0".
 * The new argv array has pointers into this string intermingled with
 * pointers to the existing arguments.  Arguments are moved to
 * immediately follow their options.
 *
 * The optstring argument here is the same one passed to getopt(3).
 * It is used to determine which option letters have trailing arguments.
 */
char **
rewrite_argv(struct bsdtar *bsdtar, int *argc, char ** src_argv,
    const char *optstring)
{
	char **new_argv, **dest_argv;
	const char *p;
	char *src, *dest;

	if (src_argv[1] == NULL || src_argv[1][0] == '-')
		return (src_argv);

	*argc += strlen(src_argv[1]) - 1;
	new_argv = malloc((*argc + 1) * sizeof(new_argv[0]));
	if (new_argv == NULL)
		bsdtar_errc(bsdtar, 1, errno, "No Memory");

	dest_argv = new_argv;
	*dest_argv++ = *src_argv++;

	dest = malloc(strlen(*src_argv) * 3);
	if (dest == NULL)
		bsdtar_errc(bsdtar, 1, errno, "No memory");
	for (src = *src_argv++; *src != '\0'; src++) {
		*dest_argv++ = dest;
		*dest++ = '-';
		*dest++ = *src;
		*dest++ = '\0';
		/* If option takes an argument, insert that into the list. */
		for (p = optstring; p != NULL && *p != '\0'; p++) {
			if (*p != *src)
				continue;
			if (p[1] != ':')	/* No arg required, done. */
				break;
			if (*src_argv == NULL)	/* No arg available? Error. */
				bsdtar_errc(bsdtar, 1, 0,
				    "Option %c requires an argument",
				    *src);
			*dest_argv++ = *src_argv++;
			break;
		}
	}

	/* Copy remaining arguments, including trailing NULL. */
	while ((*dest_argv++ = *src_argv++) != NULL)
		;

	return (new_argv);
}

void
usage(struct bsdtar *bsdtar)
{
	const char	*p;

	p = bsdtar->progname;

	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  List:    %s -tf <archive-filename>\n", p);
	fprintf(stderr, "  Extract: %s -xf <archive-filename>\n", p);
	fprintf(stderr, "  Create:  %s -cf <archive-filename> [filenames...]\n", p);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "  Help:    %s --help\n", p);
#else
	fprintf(stderr, "  Help:    %s -h\n", p);
#endif
	exit(1);
}

static const char *long_help_msg[] = {
	"First option must be a mode specifier:\n",
	"  -c Create  -r Add/Replace  -t List  -u Update  -x Extract\n",
	"Common Options:\n",
	"  -b #  Use # 512-byte records per I/O block\n",
	"  -f <filename>  Location of archive (default " _PATH_DEFTAPE ")\n",
	"  -v    Verbose\n",
	"  -w    Interactive\n",
	"Create: %p -c [options] [<file> | <dir> | @<archive> | C=<dir> ]\n",
	"  <file>, <dir>  add these items to archive\n",
	"  -z, -j  Compress archive with gzip/bzip2\n",
	"  -F {ustar|pax|cpio|shar}  Select archive format\n",
#ifdef HAVE_GETOPT_LONG
	"  --exclude <pattern>  Skip files that match pattern\n",
#else
	"  -W exclude=<pattern>  Skip files that match pattern\n",
#endif
	"  C=<dir>  Change to <dir> before processing remaining files\n",
	"  @<archive>  Add entries from <archive> to output\n",
	"List: %p -t [options] [<patterns>]\n",
	"  <patterns>  If specified, list only entries that match\n",
	"Extract: %p -x [options] [<patterns>]\n",
	"  <patterns>  If specified, extract only entries that match\n",
	"  -k    Keep (don't overwrite) existing files\n",
	"  -m    Don't restore modification times\n",
	"  -O    Write entries to stdout, don't restore to disk\n",
	"  -p    Restore permissions (including ACLs, owner, file flags)\n",
	NULL
};


/*
 * Note that the word 'bsdtar' will always appear in the first line
 * of output.
 *
 * In particular, /bin/sh scripts that need to test for the presence
 * of bsdtar can use the following template:
 *
 * if (tar --help 2>&1 | grep bsdtar >/dev/null 2>&1 ) then \
 *          echo bsdtar; else echo not bsdtar; fi
 */
static void
long_help(struct bsdtar *bsdtar)
{
	const char	*prog;
	const char	*p;
	const char	**msg;

	prog = bsdtar->progname;

	fflush(stderr);
	if (strcmp(prog,"bsdtar")!=0)
		p = "(bsdtar)";
	else
		p = "";

	fprintf(stdout, "%s%s: manipulate archive files\n", prog, p);

	for (msg = long_help_msg; *msg != NULL; msg++) {
		for (p = *msg; p != NULL; p++) {
			if (*p == '\0')
				break;
			else if (*p == '%') {
				if (p[1] == 'p') {
					fputs(prog, stdout);
					p++;
				} else
					putchar('%');
			} else
				putchar(*p);
		}
	}
	fflush(stderr);
}

static int
bsdtar_getopt(struct bsdtar *bsdtar, const char *optstring,
    const struct option **poption)
{
	char *p, *q;
	const struct option *option;
	int opt;
	int option_index;
	size_t option_length;

	option_index = -1;
	*poption = NULL;

#ifdef HAVE_GETOPT_LONG
	opt = getopt_long(bsdtar->argc, bsdtar->argv, optstring,
	    tar_longopts, &option_index);
	if (option_index > -1)
		*poption = tar_longopts + option_index;
#else
	opt = getopt(bsdtar->argc, bsdtar->argv, optstring);
#endif

	/* Support long options through -W longopt=value */
	if (opt == 'W') {
		p = optarg;
		q = strchr(optarg, '=');
		if (q != NULL) {
			option_length = q - p;
			optarg = q + 1;
		} else {
			option_length = strlen(p);
			optarg = NULL;
		}
		option = tar_longopts;
		while (option->name != NULL &&
		    (strlen(option->name) < option_length ||
		    strncmp(p, option->name, option_length) != 0 )) {
			option++;
		}

		if (option->name != NULL) {
			*poption = option;
			opt = option->val;

			/* Check if there's another match. */
			option++;
			while (option->name != NULL &&
			    (strlen(option->name) < option_length ||
			    strncmp(p, option->name, option_length) != 0)) {
				option++;
			}
			if (option->name != NULL)
				bsdtar_errc(bsdtar, 1, 0,
				    "Ambiguous option %s "
				    "(matches both %s and %s)",
				    p, (*poption)->name, option->name);

		} else {
			opt = '?';
			/* TODO: Set up a fake 'struct option' for
			 * error reporting... ? ? ? */
		}
	}

	return (opt);
}
