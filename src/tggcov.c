/*
 * ggcov - A GTK frontend for exploring gcov coverage data
 * Copyright (c) 2003-2015 Greg Banks <gnb@users.sourceforge.net>
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
#include "cov_priv.H"
#include "filename.h"
#include "estring.H"
#include "tok.H"
#include "fakepopt.h"
#include "report.H"
#include "callgraph_diagram.H"
#include "check_scenegen.H"

char *argv0;

class tggcov_params_t : public cov_project_params_t
{
public:
    tggcov_params_t();
    ~tggcov_params_t();

    ARGPARSE_STRING_PROPERTY(reports);
    ARGPARSE_BOOL_PROPERTY(annotate_flag);
    ARGPARSE_BOOL_PROPERTY(blocks_flag);
    ARGPARSE_BOOL_PROPERTY(header_flag);
    ARGPARSE_BOOL_PROPERTY(lines_flag);
    ARGPARSE_BOOL_PROPERTY(status_flag);
    ARGPARSE_BOOL_PROPERTY(new_format_flag);
    ARGPARSE_BOOL_PROPERTY(check_callgraph_flag);
    ARGPARSE_BOOL_PROPERTY(dump_callgraph_flag);
    ARGPARSE_STRING_PROPERTY(output_filename);

public:
    void setup_parser(argparse::parser_t &parser)
    {
	cov_project_params_t::setup_parser(parser);
	parser.add_option('R', "report")
	      .description("display named reports or \"all\"")
	      .setter((argparse::arg_setter_t)&tggcov_params_t::set_reports);
	parser.add_option('a', "annotate")
	      .description("save annotated source to FILE.tggcov")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_annotate_flag);
	parser.add_option('B', "blocks")
	      .description("in annotated source, display block numbers")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_blocks_flag);
	parser.add_option('H', "header")
	      .description("in annotated source, display header line")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_header_flag);
	parser.add_option('L', "lines")
	      .description("in annotated source, display line numbers")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_lines_flag);
	parser.add_option('S', "status")
	      .description("in annotated source, display line status")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_status_flag);
	parser.add_option('N', "new-format")
	      .description("in annotated source, display count in new gcc 3.3 format")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_new_format_flag);
	parser.add_option('G', "check-callgraph")
	      .description("generate and check callgraph diagram")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_check_callgraph_flag);
	parser.add_option('P', "dump-callgraph")
	      .description("dump callgraph data in text form")
	      .setter((argparse::noarg_setter_t)&tggcov_params_t::set_dump_callgraph_flag);
	parser.add_option('o', "output")
	      .description("output file for annotation")
	      .setter((argparse::arg_setter_t)&tggcov_params_t::set_output_filename);
	parser.set_other_option_help("[OPTIONS] [executable|source|directory]...");
    }

    void post_args()
    {
	cov_project_params_t::post_args();
	if (debug_enabled(D_DUMP|D_VERBOSE))
	{
	    duprintf1("blocks_flag=%d\n", blocks_flag_);
	    duprintf1("header_flag=%d\n", header_flag_);
	    duprintf1("lines_flag=%d\n", lines_flag_);
	    duprintf1("reports=\"%s\"\n", reports_.data());
	}
    }
};

tggcov_params_t::tggcov_params_t()
 : annotate_flag_(0),
   blocks_flag_(0),
   header_flag_(0),
   lines_flag_(0),
   status_flag_(0),
   new_format_flag_(0),
   check_callgraph_flag_(0),
   dump_callgraph_flag_(0)
{
}

tggcov_params_t::~tggcov_params_t()
{
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

#define BLOCKS_WIDTH    8

static void
annotate_file(tggcov_params_t &params, cov_file_t *f)
{
    cov_file_annotator_t annotator(f);
    if (!annotator.is_valid())
	return;

    char *ggcov_filename;
    FILE *outfp = NULL;
    if (params.get_output_filename() && !strcmp(params.get_output_filename(), "-"))
    {
	ggcov_filename = NULL;
	outfp = stdout;
    }
    else if (params.get_output_filename())
    {
	estring e = params.get_output_filename();
	e.replace_all("{}", file_basename_c(f->name()));
	ggcov_filename = e.take();
    }
    else
    {
	ggcov_filename = g_strconcat(f->name(), ".tggcov", (char *)0);
    }

    if (ggcov_filename)
    {
	fprintf(stderr, "Writing %s\n", ggcov_filename);
	if ((outfp = fopen(ggcov_filename, "w")) == 0)
	{
	    perror(ggcov_filename);
	    g_free(ggcov_filename);
	    return;
	}
	g_free(ggcov_filename);
    }

    if (params.get_header_flag())
    {
	fprintf(outfp, "    Count       ");
	if (params.get_blocks_flag())
	    fprintf(outfp, "Block(s)");
	if (params.get_lines_flag())
	    fprintf(outfp, " Line   ");
	if (params.get_status_flag())
	    fprintf(outfp, " Status ");
	fprintf(outfp, " Source\n");

	fprintf(outfp, "============    ");
	if (params.get_blocks_flag())
	    fprintf(outfp, "======= ");
	if (params.get_lines_flag())
	    fprintf(outfp, "======= ");
	if (params.get_status_flag())
	    fprintf(outfp, "======= ");
	fprintf(outfp, "=======\n");
    }

    while (annotator.next())
    {
	if (params.get_new_format_flag())
	{
	    if (annotator.status() != cov::UNINSTRUMENTED &&
		annotator.status() != cov::SUPPRESSED)
	    {
		if (annotator.count())
		    fprintf(outfp, "%9llu:%5lu:",
			    (unsigned long long)annotator.count(),
			    annotator.lineno());
		else
		    fprintf(outfp, "    #####:%5lu:", annotator.lineno());
	    }
	    else
		fprintf(outfp, "        -:%5lu:", annotator.lineno());
	}
	else
	{
	    if (annotator.status() != cov::UNINSTRUMENTED &&
		annotator.status() != cov::SUPPRESSED)
	    {
		if (annotator.count())
		    fprintf(outfp, "%12llu    ", (unsigned long long)annotator.count());
		else
		    fputs("      ######    ", outfp);
	    }
	    else
		fputs("\t\t", outfp);
	}
	if (params.get_blocks_flag())
	{
	    char blocks_buf[BLOCKS_WIDTH];
	    annotator.format_blocks(blocks_buf, BLOCKS_WIDTH-1);
	    fprintf(outfp, "%*s ", BLOCKS_WIDTH-1, blocks_buf);
	}
	if (params.get_lines_flag())
	{
	    fprintf(outfp, "%7lu ", annotator.lineno());
	}
	if (params.get_status_flag())
	{
	    fprintf(outfp, "%7s ", cov::short_name(annotator.status()));
	}
	fputs(annotator.text(), outfp);
    }

    if (outfp != stdout)
	fclose(outfp);
}

static void
annotate(tggcov_params_t &params)
{
    for (list_iterator_t<cov_file_t> iter = cov_file_t::first() ; *iter ; ++iter)
	annotate_file(params, *iter);
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

static int report_lastlines;

static void
do_report(FILE *fp, const report_t *rep)
{
    /* ensure there is one line of empty space between each non-empty report */
    if (report_lastlines > 0)
	fputc('\n', fp);

    report_lastlines = (*rep->func)(fp, "stdout");
}

static void
report(tggcov_params_t &params)
{
    FILE *fp = stdout;
    const report_t *rep;
    gboolean did_msg1 = FALSE;

    report_lastlines = -1;
    const char *reports = params.get_reports();
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

static void
check_callgraph(void)
{
    check_scenegen_t *sg = new check_scenegen_t;
    callgraph_diagram_t *diag = new callgraph_diagram_t;

    diag->prepare();
    diag->render(sg);

    if (!sg->check())
	exit(1);

    delete diag;
    delete sg;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/

static void
set_printable_location(cov_location_var &loc, const cov_location_t *from)
{
    if (from == 0)
	loc.set("-", 0);
    else
	loc.set(cov_file_t::minimise_name(from->filename), from->lineno);
}

static void
dump_callgraph(void)
{
    FILE *fp;
    const char *cgfilename = file_make_absolute("callgraph.tggcov");

    fprintf(stderr, "Writing %s\n", cgfilename);
    if ((fp = fopen(cgfilename, "w")) == 0)
    {
	perror(cgfilename);
	return;
    }

    ptrarray_t<cov_callnode_t> *nodes = new ptrarray_t<cov_callnode_t>;
    for (cov_callspace_iter_t csitr = cov_callgraph.first() ; *csitr ; ++csitr)
    {
	for (cov_callnode_iter_t cnitr = (*csitr)->first() ; *cnitr ; ++cnitr)
	    nodes->append(*cnitr);
    }
    nodes->sort(cov_callnode_t::compare_by_name);

    fprintf(fp, "# tggcov callgraph version 1\n");
    fprintf(fp, "base %s\n", cov_file_t::common_path());
    unsigned int i;
    for (i = 0 ; i < nodes->length() ; i++)
    {
	cov_callnode_t * cn = nodes->nth(i);

	fprintf(fp, "callnode %s\n\tsource %s\n",
	    cn->name.data(),
	    (cn->function != 0 ? cn->function->file()->minimal_name() : "-"));

	for (list_iterator_t<cov_callarc_t> caitr = cn->out_arcs.first() ; *caitr ; ++caitr)
	{
	    cov_callarc_t *ca = *caitr;

	    fprintf(fp, "\tcallarc %s\n\t\tcount %llu\n",
		ca->to->name.data(),
		(unsigned long long)ca->count);
	}

	if (cn->function != 0)
	{
	    fprintf(fp, "function %s\n", cn->function->name());
	    cov_call_iterator_t *itr;

	    itr = new cov_function_call_iterator_t(cn->function);
	    while (itr->next())
	    {
		fprintf(fp, "\tblock %u\n", itr->block()->bindex());
		cov_location_var loc;
		set_printable_location(loc, itr->location());
		fprintf(fp, "\t\tcall %s\n\t\t\tcount %llu\n\t\t\tlocation %s\n",
			(itr->name() != 0 ? itr->name() : "-"),
			(unsigned long long)itr->count(),
			loc.describe());
	    }
	    delete itr;

	    const cov_location_t *first = cn->function->get_first_location();
	    const cov_location_t *last = cn->function->get_last_location();
	    itr = new cov_range_call_iterator_t(first, last);
	    while (itr->next())
	    {
		cov_location_var loc;
		set_printable_location(loc, itr->location());
		fprintf(fp, "\tlocation %s\n", loc.describe());
		fprintf(fp, "\t\tcall %s\n\t\t\tcount %llu\n",
			(itr->name() != 0 ? itr->name() : "-"),
			(unsigned long long)itr->count());
	    }
	    delete itr;
	}
    }

    fclose(fp);
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

    tggcov_params_t params;
    argparse::parser_t parser(params);
    if (parser.parse(argc, argv) < 0)
    {
	exit(1);	/* error message emitted in parse_args() */
    }

    int r = cov_read_files(params);
    if (r < 0)
	exit(1);    /* error message in cov_read_files() */
    if (r == 0)
	exit(0);    /* error message in cov_read_files() */

    cov_dump(stderr);

    if (params.get_reports())
	report(params);
    if (params.get_annotate_flag())
	annotate(params);
    if (params.get_check_callgraph_flag())
	check_callgraph();
    if (params.get_dump_callgraph_flag())
	dump_callgraph();

    return 0;
}

/*-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-*/
/*END*/
