/*
 * ggcov - A GTK frontend for exploring gcov coverage data
 * Copyright (c) 2001-2003 Greg Banks <gnb@alphalink.com.au>
 * 
 *
 * TODO: attribution for decode-gcov.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * tggcov is a libcov-based reimplementation of the gcov commandline
 * program.  It's primary use is for regression testing, to allow
 * automated comparisons of the statistics generated by libcov and
 * the real ones from gcov.
 *
 * TODO: update
 */

#include "common.h"
#include "cov.H"
#include "filename.h"
#include "estring.H"
#include "tok.H"
#include "fakepopt.h"
#include "report.H"

CVSID("$Id: tggcov.c,v 1.11 2004-04-02 14:27:01 gnb Exp $");

char *argv0;
GList *files;	    /* incoming specification from commandline */

static int recursive = FALSE;	/* needs to be int (not gboolean) for popt */
static char *suppressed_ifdefs = 0;
static char *object_dir = 0;
static int header_flag = FALSE;
static int blocks_flag = FALSE;
static int lines_flag = FALSE;
static int new_format_flag = FALSE;
static int annotate_flag = FALSE;
static const char *debug_str = 0;
static const char *reports = 0;

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
/* 
 * TODO: This function should be common with the identical code in ggcov.c
 */

static void
read_gcov_files(void)
{
    GList *iter;
    
    cov_init();

    if (suppressed_ifdefs != 0)
    {
    	tok_t tok(/*force copy*/(const char *)suppressed_ifdefs, ", \t");
	const char *v;
	
	while ((v = tok.next()) != 0)
    	    cov_suppress_ifdef(v);
    }

    cov_pre_read();
    
    if (object_dir != 0)
    	cov_add_search_directory(object_dir);

    if (files == 0)
    {
    	if (!cov_read_directory(".", recursive))
	    exit(1);
    }
    else
    {
	for (iter = files ; iter != 0 ; iter = iter->next)
	{
	    const char *filename = (const char *)iter->data;
	    
	    if (file_is_directory(filename) == 0)
	    	cov_add_search_directory(filename);
    	}

	for (iter = files ; iter != 0 ; iter = iter->next)
	{
	    const char *filename = (const char *)iter->data;
	    
	    if (file_is_directory(filename) == 0)
	    {
	    	if (!cov_read_directory(filename, recursive))
		    exit(1);
	    }
	    else if (file_is_regular(filename) == 0)
	    {
	    	if (cov_is_source_filename(filename))
		{
		    if (!cov_read_source_file(filename))
			exit(1);
		}
		else
		{
		    if (!cov_read_object_file(filename))
			exit(1);
		}
	    }
	    else
	    {
	    	fprintf(stderr, "%s: don't know how to handle this filename\n",
		    	filename);
		exit(1);
	    }
	}
    }
    
    cov_post_read();
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#define BLOCKS_WIDTH	8

static void
annotate_file(cov_file_t *f)
{
    const char *cfilename = f->name();
    FILE *infp, *outfp;
    unsigned long lineno;
    cov_line_t *ln;
    char *ggcov_filename;
    char buf[1024];
    
    if ((infp = fopen(cfilename, "r")) == 0)
    {
    	perror(cfilename);
	return;
    }

    ggcov_filename = g_strconcat(cfilename, ".tggcov", 0);
    fprintf(stderr, "Writing %s\n", ggcov_filename);
    if ((outfp = fopen(ggcov_filename, "w")) == 0)
    {
    	perror(ggcov_filename);
	g_free(ggcov_filename);
	fclose(infp);
	return;
    }
    g_free(ggcov_filename);
    
    if (header_flag)
    {
    	fprintf(outfp, "    Count       ");
	if (blocks_flag)
    	    fprintf(outfp, "Block(s)");
	if (lines_flag)
    	    fprintf(outfp, " Line   ");
    	fprintf(outfp, " Source\n");

    	fprintf(outfp, "============    ");
	if (blocks_flag)
    	    fprintf(outfp, "======= ");
	if (lines_flag)
    	    fprintf(outfp, "======= ");
    	fprintf(outfp, "=======\n");
    }
    
    lineno = 0;
    while (fgets(buf, sizeof(buf), infp) != 0)
    {
    	++lineno;
	ln = f->nth_line(lineno);

	if (new_format_flag)
	{
	    if (ln->status() != cov::UNINSTRUMENTED &&
	    	ln->status() != cov::SUPPRESSED)
	    {
		if (ln->count())
		    fprintf(outfp, "%9llu:%5lu:", ln->count(), lineno);
		else
		    fprintf(outfp, "    #####:%5lu:", lineno);
	    }
	    else
		fprintf(outfp, "        -:%5lu:", lineno);
	}
	else
	{
	    if (ln->status() != cov::UNINSTRUMENTED &&
	    	ln->status() != cov::SUPPRESSED)
	    {
		if (ln->count())
		    fprintf(outfp, "%12lld    ", ln->count());
		else
		    fputs("      ######    ", outfp);
	    }
	    else
		fputs("\t\t", outfp);
	}
	if (blocks_flag)
	{
    	    char blocks_buf[BLOCKS_WIDTH];
    	    ln->format_blocks(blocks_buf, BLOCKS_WIDTH-1);
	    fprintf(outfp, "%*s ", BLOCKS_WIDTH-1, blocks_buf);
	}
	if (lines_flag)
	{
	    fprintf(outfp, "%7lu ", lineno);
	}
	fputs(buf, outfp);
    }
    
    fclose(infp);
    fclose(outfp);
}

static void
annotate(void)
{
    list_iterator_t<cov_file_t> iter;
    
    for (iter = cov_file_t::first() ; iter != (cov_file_t *)0 ; ++iter)
    	annotate_file(*iter);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

static int report_lastlines;

static void
do_report(FILE *fp, const report_t *rep)
{
    /* ensure there is one line of empty space between each non-empty report */
    if (report_lastlines > 0)
    	fputc('\n', fp);

    report_lastlines = (*rep->func)(fp);
}

static void
report(void)
{
    FILE *fp = stdout;
    const report_t *rep;
    gboolean did_msg1 = FALSE;

    report_lastlines = -1;
    if (reports == 0 || *reports == '\0' || !strcmp(reports, "all"))
    {
    	/* call all reports */
	for (rep = all_reports ; rep->name != 0 ; rep++)
	    do_report(fp, rep);
    }
    else if (!strcmp(reports, "list"))
    {
    	/* print available reports and exit */
	printf("Available reports:\n");
	for (rep = all_reports ; rep->name != 0 ; rep++)
	    printf("    %s\n", rep->name);
	fflush(stdout);
	exit(0);
    }
    else
    {
    	/* call only the named reports */
	tok_t tok(reports, ", ");
	const char *name;
	
	while ((name = tok.next()) != 0)
	{
	    for (rep = all_reports ; rep->name != 0 ; rep++)
	    {
	    	if (!strcmp(rep->name, name))
		{
		    do_report(fp, rep);
		    break;
		}
	    }
	    if (rep->name == 0)
	    {
	    	fprintf(stderr, "%s: unknown report name \"%s\"", argv0, name);
		if (!did_msg1)
		{
	    	    fputs(", use \"-R list\" to print listing.", stderr);
		    did_msg1 = TRUE;
		}
		fputc('\n', stderr);
	    }
	}
    }
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
/*
 * With the old GTK, we're forced to parse our own arguments the way
 * the library wants, with popt and in such a way that we can't use the
 * return from poptGetNextOpt() to implement multiple-valued options
 * (e.g. -o dir1 -o dir2).  This limits our ability to parse arguments
 * for both old and new GTK builds.  Worse, gtk2 doesn't depend on popt
 * at all, so some systems will have gtk2 and not popt, so we have to
 * parse arguments in a way which avoids potentially buggy duplicate
 * specification of options, i.e. we simulate popt in fakepopt.c!
 */
static poptContext popt_context;
static struct poptOption popt_options[] =
{
    {
    	"recursive",	    	    	    	/* longname */
	'r',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&recursive,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"recursively scan directories for source", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"suppress-ifdef",	    	    	/* longname */
	'X',  	    	    	    	    	/* shortname */
	POPT_ARG_STRING,  	    	    	/* argInfo */
	&suppressed_ifdefs,     	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"suppress source which is conditional on the "
	"given cpp define/s (comma-separated)", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"report",	    	    	    	/* longname */
	'R',  	    	    	    	    	/* shortname */
	POPT_ARG_STRING,  	    	    	/* argInfo */
	&reports,     	    	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"display named reports or \"all\"",	/* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"annotate",	    	    	    	/* longname */
	'a',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&annotate_flag,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"save annotated source to FILE.tggcov", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"blocks",	    	    	    	/* longname */
	'B',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&blocks_flag,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"in annotated source, display block numbers",/* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"header",	    	    	    	/* longname */
	'H',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&header_flag,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"in annotated source, display header line", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"lines",	    	    	    	/* longname */
	'L',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&lines_flag,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"in annotated source, display line numbers", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"new-format",	    	    	    	/* longname */
	'N',  	    	    	    	    	/* shortname */
	POPT_ARG_NONE,  	    	    	/* argInfo */
	&new_format_flag,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"in annotated source, display count in new gcc 3.3 format", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"object-dir",	    	    	    	/* longname */
	'o',  	    	    	    	    	/* shortname */
	POPT_ARG_STRING,  	    	    	/* argInfo */
	&object_dir,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"directory in which to find .o,.bb,.bbg,.da files", /* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    {
    	"debug",	    	    	    	/* longname */
	'D',  	    	    	    	    	/* shortname */
	POPT_ARG_STRING,  	    	    	/* argInfo */
	&debug_str,     	    	    	/* arg */
	0,  	    	    	    	    	/* val 0=don't return */
	"enable tggcov debugging features",  	/* descrip */
	0	    	    	    	    	/* argDescrip */
    },
    POPT_AUTOHELP
    { 0, 0, 0, 0, 0, 0, 0 }
};

static void
parse_args(int argc, char **argv)
{
    const char *file;
    
    argv0 = argv[0];
    
    popt_context = poptGetContext(PACKAGE, argc, (const char**)argv,
    	    	    	    	  popt_options, 0);
    poptSetOtherOptionHelp(popt_context,
    	    	           "[OPTIONS] [executable|source|directory]...");

    int rc;
    while ((rc = poptGetNextOpt(popt_context)) > 0)
    	;
    if (rc < -1)
    {
    	fprintf(stderr, "%s:%s at or near %s\n",
	    argv[0],
	    poptStrerror(rc),
	    poptBadOption(popt_context, POPT_BADOPTION_NOALIAS));
    	exit(1);
    }
    
    while ((file = poptGetArg(popt_context)) != 0)
	files = g_list_append(files, (gpointer)file);
	
    poptFreeContext(popt_context);
    
    if (debug_str != 0)
    	debug_set(debug_str);

    if (debug_enabled(D_DUMP|D_VERBOSE))
    {
    	GList *iter;
	string_var token_str = debug_enabled_tokens();

	duprintf1("parse_args: recursive=%d\n", recursive);
	duprintf1("parse_args: suppressed_ifdefs=%s\n", suppressed_ifdefs);
	duprintf1("parse_args: blocks_flag=%d\n", blocks_flag);
	duprintf1("parse_args: header_flag=%d\n", header_flag);
	duprintf1("parse_args: lines_flag=%d\n", lines_flag);
	duprintf1("parse_args: reports=\"%s\"\n", reports);
	duprintf2("parse_args: debug = 0x%lx (%s)\n", debug, token_str.data());

	duprintf0("parse_args: files = ");
	for (iter = files ; iter != 0 ; iter = iter->next)
	    duprintf1(" \"%s\"", (char *)iter->data);
	duprintf0(" }\n");
    }
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#define DEBUG_GLIB 1
#if DEBUG_GLIB

static const char *
log_level_to_str(GLogLevelFlags level)
{
    static char buf[32];

    switch (level & G_LOG_LEVEL_MASK)
    {
    case G_LOG_LEVEL_ERROR: return "ERROR";
    case G_LOG_LEVEL_CRITICAL: return "CRITICAL";
    case G_LOG_LEVEL_WARNING: return "WARNING";
    case G_LOG_LEVEL_MESSAGE: return "MESSAGE";
    case G_LOG_LEVEL_INFO: return "INFO";
    case G_LOG_LEVEL_DEBUG: return "DEBUG";
    default:
    	snprintf(buf, sizeof(buf), "%d", level);
	return buf;
    }
}

void
log_func(
    const char *domain,
    GLogLevelFlags level,
    const char *msg,
    gpointer user_data)
{
    fprintf(stderr, "%s:%s:%s\n",
    	(domain == 0 ? PACKAGE : domain),
	log_level_to_str(level),
	msg);
    if (level & G_LOG_FLAG_FATAL)
    	exit(1);
}

#endif
/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

int
main(int argc, char **argv)
{
#if DEBUG_GLIB
    g_log_set_handler("GLib",
    	    	      (GLogLevelFlags)(G_LOG_LEVEL_MASK|G_LOG_FLAG_FATAL),
    	    	      log_func, /*user_data*/0);
#endif

    parse_args(argc, argv);
    read_gcov_files();

    cov_dump(stderr);

    if (reports)
	report();
    if (annotate_flag)
    	annotate();

    return 0;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
/*END*/
