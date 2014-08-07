/*  Boolector: Satisfiablity Modulo Theories (SMT) solver.
 *
 *  Copyright (C) 2007-2009 Robert Daniel Brummayer.
 *  Copyright (C) 2007-2014 Armin Biere.
 *  Copyright (C) 2012-2014 Aina Niemetz.
 *  Copyright (C) 2012-2014 Mathias Preiner.
 *
 *  All rights reserved.
 *
 *  This file is part of Boolector.
 *  See COPYING for more information on using this software.
 */

#include "btormain.h"
#include "boolector.h"
#include "btorconfig.h"
#include "btorexit.h"
#include "btormem.h"
#include "btoropt.h"
#include "btorparse.h"

#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct BtorMainApp BtorMainApp;
static BtorMainApp *static_app;

static int static_verbosity;
static int static_set_alarm;
static int static_caught_sig;
#ifdef BTOR_HAVE_GETRUSAGE
static int static_start_time;
#endif

static void (*sig_int_handler) (int);
static void (*sig_segv_handler) (int);
static void (*sig_abrt_handler) (int);
static void (*sig_term_handler) (int);
static void (*sig_bus_handler) (int);

static void (*sig_alrm_handler) (int);

/*------------------------------------------------------------------------*/

typedef struct BtorMainOpts
{
  BtorOpt first; /* dummy for iteration */
  /* ------------------------------------ */
  BtorOpt help;
  BtorOpt copyright;
  BtorOpt version;
  BtorOpt time;
  BtorOpt output;
#ifdef BTOR_USE_LINGELING
  BtorOpt lingeling;
  BtorOpt lingeling_nofork;
  BtorOpt lingeling_opts;
#endif
#ifdef BTOR_USE_PICOSAT
  BtorOpt picosat;
#endif
#ifdef BTOR_USE_MINISAT
  BtorOpt minisat;
#endif
  /* ------------------------------------ */
  BtorOpt last; /* dummy for iteration */
} BtorMainOpts;

#define BTORMAIN_FIRST_OPT(OPTS) (&OPTS.first + 1)
#define BTORMAIN_LAST_OPT(OPTS) (&OPTS.last - 1)

#define BTORMAIN_INIT_OPT(OPT, INTL, SHRT, LNG, VAL, MIN, MAX, DESC) \
  do                                                                 \
  {                                                                  \
    OPT.internal = INTL;                                             \
    OPT.shrt     = SHRT;                                             \
    OPT.lng      = LNG;                                              \
    OPT.dflt = OPT.val = VAL;                                        \
    OPT.min            = MIN;                                        \
    OPT.max            = MAX;                                        \
    OPT.desc           = DESC;                                       \
  } while (0)

typedef struct BtorMainApp
{
  Btor *btor;
  BtorMemMgr *mm;
  BtorMainOpts opts;
  int done;
  int err;
  char *infile_name;
  FILE *infile;
  int close_infile;
  FILE *outfile;
  int close_outfile;
} BtorMainApp;

static BtorMainApp *
btormain_new_btormain (Btor *btor)
{
  assert (btor);

  BtorMainApp *res;
  BtorMemMgr *mm;

  mm = btor_new_mem_mgr ();
  BTOR_CNEWN (mm, res, 1);
  res->mm      = mm;
  res->btor    = btor;
  res->infile  = stdin;
  res->outfile = stdout;
  return res;
}

static void
btormain_delete_btormain (BtorMainApp *app)
{
  assert (app);
  BtorMemMgr *mm = app->mm;

  boolector_delete (app->btor);
  BTOR_DELETEN (mm, app, 1);
  btor_delete_mem_mgr (mm);
}

static void
btormain_init_opts (BtorMainApp *app)
{
  assert (app);

  BTORMAIN_INIT_OPT (
      app->opts.help, 0, "h", "help", 0, 0, 1, "print this message and exit");
  BTORMAIN_INIT_OPT (app->opts.copyright,
                     0,
                     "c",
                     "copyright",
                     0,
                     0,
                     1,
                     "print copyright and exit");
  BTORMAIN_INIT_OPT (
      app->opts.version, 0, "V", "version", 0, 0, 1, "print version and exit");
  BTORMAIN_INIT_OPT (
      app->opts.time, 0, "t", "time", 0, 0, -1, "set time limit");
  BTORMAIN_INIT_OPT (app->opts.output,
                     1,
                     "o",
                     "output",
                     0,
                     0,
                     0,
                     "set output file for dumping");
#if defined(BTOR_USE_LINGELING)
  BTORMAIN_INIT_OPT (app->opts.lingeling,
                     1,
                     0,
                     "lingeling",
                     0,
                     0,
                     1,
                     "force Lingeling as SAT solver");
  BTORMAIN_INIT_OPT (app->opts.lingeling_opts,
                     1,
                     0,
                     "lingeling_opts",
                     0,
                     0,
                     0,
                     "set lingeling option(s) '--<opt>=<val>'");
  BTORMAIN_INIT_OPT (app->opts.lingeling_nofork,
                     1,
                     0,
                     "lingeing_nofork",
                     0,
                     0,
                     0,
                     "do not use 'fork/clone' for Lingeling");
#endif
#ifdef BTOR_USE_PICOSAT
  BTORMAIN_INIT_OPT (app->opts.picosat,
                     1,
                     0,
                     "picosat",
                     0,
                     0,
                     1,
                     "force PicoSAT as SAT solver");
#endif
#ifdef BTOR_USE_MINISAT
  BTORMAIN_INIT_OPT (app->opts.minisat,
                     1,
                     0,
                     "minisat",
                     0,
                     0,
                     1,
                     "force MiniSAT as SAT solver");
#endif
}

/*------------------------------------------------------------------------*/

static void
btormain_error (BtorMainApp *app, char *msg, ...)
{
  assert (app);

  va_list list;
  va_start (list, msg);
  fputs ("boolector: ", stderr);
  vfprintf (stderr, msg, list);
  fprintf (stderr, "\n");
  va_end (list);
  app->err = BTOR_ERR_EXIT;
}

static void
btormain_msg (char *msg, ...)
{
  assert (msg);

  va_list list;
  va_start (list, msg);
  fprintf (stdout, "[btormain] ");
  vfprintf (stdout, msg, list);
  fprintf (stdout, "\n");
  va_end (list);
}

/*------------------------------------------------------------------------*/

#define LEN_OPTSTR 35
#define LEN_PARAMSTR 16

static void
print_opt (BtorMainApp *app, BtorOpt *opt)
{
  assert (app);
  assert (opt);

  char optstr[LEN_OPTSTR], paramstr[LEN_PARAMSTR], *lngstr;
  int i, len;

  memset (optstr, ' ', LEN_OPTSTR * sizeof (char));
  optstr[LEN_OPTSTR - 1] = '\0';

  if (!strcmp (opt->lng, "look_ahead") || !strcmp (opt->lng, "in_depth")
      || !strcmp (opt->lng, "interval"))
    sprintf (paramstr, "<w>");
  else if (!strcmp (opt->lng, "time"))
    sprintf (paramstr, "<seconds>");
  else if (!strcmp (opt->lng, "output"))
    sprintf (paramstr, "<file>");
  else if (!strcmp (opt->lng, "rewrite_level"))
    sprintf (paramstr, "<n>");
  else if (!strcmp (opt->lng, "lingeling_opts"))
    sprintf (paramstr, "[,<opt>=<val>]+");
  else
    paramstr[0] = '\0';

  assert (
      !strcmp (opt->lng, "lingeling_opts")
      || (opt->shrt
          && 2 * strlen (paramstr) + strlen (opt->shrt) + strlen (opt->lng) + 7
                 <= LEN_OPTSTR)
      || (!opt->shrt
          && 2 * strlen (paramstr) + strlen (opt->lng) + 7 <= LEN_OPTSTR));

  len = strlen (opt->lng);
  BTOR_NEWN (app->mm, lngstr, len + 1);
  for (i = 0; i < len; i++) lngstr[i] = opt->lng[i] == '_' ? '-' : opt->lng[i];
  lngstr[len] = '\0';

  sprintf (optstr,
           "  %s%s%s%s%s--%s%s%s",
           opt->shrt ? "-" : "",
           opt->shrt ? opt->shrt : "",
           opt->shrt && strlen (paramstr) > 0 ? " " : "",
           opt->shrt ? paramstr : "",
           opt->shrt ? ", " : "",
           lngstr,
           strlen (paramstr) > 0 ? "=" : "",
           paramstr);

  BTOR_DELETEN (app->mm, lngstr, len + 1);

  len = strlen (optstr);
  for (i = len; i < LEN_OPTSTR - 1; i++) optstr[i] = ' ';
  optstr[LEN_OPTSTR - 1] = '\0';

  fprintf (app->outfile, "%s %s\n", optstr, opt->desc);

  // TODO default values
}

static void
print_help (BtorMainApp *app)
{
  assert (app);

  BtorOpt to;
  BtorOpt *o;
  FILE *out = app->outfile;

  fprintf (out, "usage: boolector [<option>...][<input>]\n");
  fprintf (out, "\n");
  fprintf (out, "where <option> is one of the following:\n");
  fprintf (out, "\n");

  for (o = BTORMAIN_FIRST_OPT (app->opts); o <= BTORMAIN_LAST_OPT (app->opts);
       o++)
  {
    if (o->internal) continue;
    if (!strcmp (o->lng, "time") || !strcmp (o->lng, "output"))
      fprintf (out, "\n");
    print_opt (app, o);
  }
  fprintf (out, "\n");

  for (o = (BtorOpt *) boolector_first_opt (app->btor);
       o <= (BtorOpt *) boolector_last_opt (app->btor);
       o++)
  {
    if (o->internal) continue;
    if (!strcmp (o->lng, "incremental") || !strcmp (o->lng, "beta_reduce_all")
        || !strcmp (o->lng, "no_pretty_print"))
      fprintf (out, "\n");
    if (!strcmp (o->lng, "input_format"))
    {
      fprintf (app->outfile, "\n");
      to.shrt = 0;
      to.lng  = "btor";
      to.desc = "force BTOR input format";
      print_opt (app, &to);
      to.shrt = 0;
      to.lng  = "smt";
      to.desc = "force SMT-LIB v2 input format";
      print_opt (app, &to);
      to.shrt = 0;
      to.lng  = "smt1";
      to.desc = "force SMT-LIB v1 input format";
      print_opt (app, &to);
      fprintf (app->outfile, "\n");
    }
    else if (!strcmp (o->lng, "output_number_format"))
    {
      print_opt (app, &app->opts.output);
      fprintf (app->outfile, "\n");
      to.shrt = "x";
      to.lng  = "hex";
      to.desc = "force hexadecimal number output";
      print_opt (app, &to);
      to.shrt = "d";
      to.lng  = "dec";
      to.desc = "force decimal number output";
      print_opt (app, &to);
    }
    else if (!strcmp (o->lng, "output_format"))
    {
      fprintf (app->outfile, "\n");
      to.shrt = "db";
      to.lng  = "dump_btor";
      to.desc = "dump formula in BTOR format";
      print_opt (app, &to);
      to.shrt = "ds";
      to.lng  = "dump_smt";
      to.desc = "dump formula in SMT-LIB v2 format";
      print_opt (app, &to);
      to.shrt = "ds1";
      to.lng  = "dump_smt1";
      to.desc = "dump formula in SMT-LIB v1 format";
      print_opt (app, &to);
      fprintf (app->outfile, "\n");
    }
    else
      print_opt (app, o);
  }

#if defined(BTOR_USE_LINGELING)
  fprintf (app->outfile, "\n");
  print_opt (app, &app->opts.lingeling);
  print_opt (app, &app->opts.lingeling_nofork);
  print_opt (app, &app->opts.lingeling_opts);
#elif defined(BTOR_USE_PICOSAT)
  print_opt (app, &app->opts.picosat);
#elif defined(BTOR_USE_MINISAT)
  print_opt (app, &app->opts.minisat);
#else
#error "no SAT solver configured"
#endif
  app->done = 1;
}

static void
print_copyright (BtorMainApp *app)
{
  assert (app);

  FILE *out = app->outfile;

  fprintf (out, "This software is\n");
  fprintf (out, "Copyright (c) 2007-2009 Robert Brummayer\n");
  fprintf (out, "Copyright (c) 2007-2014 Armin Biere\n");
  fprintf (out, "Copyright (c) 2012-2014 Aina Niemetz, Mathias Preiner\n");
  fprintf (out, "Copyright (c) 2013 Christian Reisenberger\n");
  fprintf (out, "Institute for Formal Models and Verification\n");
  fprintf (out, "Johannes Kepler University, Linz, Austria\n");
#ifdef BTOR_USE_LINGELING
  fprintf (out, "\n");
  fprintf (out, "This software is linked against Lingeling\n");
  fprintf (out, "Copyright (c) 2010-2014 Armin Biere\n");
  fprintf (out, "Institute for Formal Models and Verification\n");
  fprintf (out, "Johannes Kepler University, Linz, Austria\n");
#endif
#ifdef BTOR_USE_PICOSAT
  fprintf (out, "\n");
  fprintf (out, "This software is linked against PicoSAT\n");
  fprintf (out, "Copyright (c) 2006-2014 Armin Biere\n");
  fprintf (out, "Institute for Formal Models and Verification\n");
  fprintf (out, "Johannes Kepler University, Linz, Austria\n");
#endif
#ifdef BTOR_USE_MINISAT
  fprintf (out, "\n");
  fprintf (out, "This software is linked against MiniSAT\n");
  fprintf (out, "Copyright (c) 2003-2013, Niklas Een, Niklas Sorensson\n");
#endif
  app->done = 1;
}

static void
print_version (BtorMainApp *app)
{
  assert (app);
  fprintf (app->outfile, "%s\n", BTOR_VERSION);
  app->done = 1;
}

static void
print_static_stats (void)
{
#ifdef BTOR_HAVE_GETRUSAGE
  double delta_time = delta_time = btor_time_stamp () - static_start_time;
  btormain_msg ("%.1f seconds\n", delta_time);
#else
  btormain_msg ("can not determine run-time in seconds (no getrusage)");
#endif
}

static void
print_sat_result (BtorMainApp *app, int sat_result)
{
  assert (app);
  if (sat_result == BOOLECTOR_UNSAT)
    fprintf (app->outfile, "unsat\n");
  else if (sat_result == BOOLECTOR_SAT)
    fprintf (app->outfile, "sat\n");
  else
  {
    assert (sat_result == BOOLECTOR_UNKNOWN);
    fprintf (app->outfile, "unknown\n");
  }
}

/*------------------------------------------------------------------------*/

static void
reset_sig_handlers (void)
{
  (void) signal (SIGINT, sig_int_handler);
  (void) signal (SIGSEGV, sig_segv_handler);
  (void) signal (SIGABRT, sig_abrt_handler);
  (void) signal (SIGTERM, sig_term_handler);
  (void) signal (SIGBUS, sig_bus_handler);
}

static void
catch_sig (int sig)
{
  if (!static_caught_sig)
  {
    static_caught_sig = 1;
    btormain_msg ("CAUGHT SIGNAL %d", sig);
    fputs ("unknown\n", stdout);
    fflush (stdout);
    if (static_verbosity > 0)
    {
      boolector_print_stats (static_app->btor);
      print_static_stats ();
      btormain_msg ("CAUGHT SIGNAL %d", sig);
    }
  }
  reset_sig_handlers ();
  raise (sig);
  exit (sig);
}

static void
set_sig_handlers (void)
{
  sig_int_handler  = signal (SIGINT, catch_sig);
  sig_segv_handler = signal (SIGSEGV, catch_sig);
  sig_abrt_handler = signal (SIGABRT, catch_sig);
  sig_term_handler = signal (SIGTERM, catch_sig);
  sig_bus_handler  = signal (SIGBUS, catch_sig);
}

static void
reset_alarm (void)
{
  alarm (0);
  (void) signal (SIGALRM, sig_alrm_handler);
}

static void
catch_alarm (int sig)
{
  (void) sig;
  assert (sig == SIGALRM);
  if (static_set_alarm > 0)
  {
    btormain_msg ("ALARM TRIGGERED: time limit %d seconds reached\n",
                  static_set_alarm);
    fputs ("unknown\n", stdout);
    fflush (stdout);
    if (static_verbosity > 0)
    {
      boolector_print_stats (static_app->btor);
      print_static_stats ();
    }
  }
  reset_alarm ();
  exit (0);
}

static void
set_alarm (void)
{
  sig_alrm_handler = signal (SIGALRM, catch_alarm);
  assert (static_set_alarm > 0);
  alarm (static_set_alarm);
}

/*------------------------------------------------------------------------*/

static int
has_suffix (const char *str, const char *suffix)
{
  int l, k, d;
  l = strlen (str);
  k = strlen (suffix);
  d = l - k;
  if (d < 0) return 0;
  return !strcmp (str + d, suffix);
}

/*------------------------------------------------------------------------*/

int
boolector_main (int argc, char **argv)
{
  int res, sat_res;
  int i, j, k, len, shrt, readval, val, log, forced_sat_solver;
  int inc, incid, incla, incint, dump;
  int parse_result, parse_status;
  char opt[50], *cmd, *valstr, *lingeling_opts, *parse_error_msg;
  BtorOpt *o;

#ifdef BTOR_HAVE_GETRUSAGE
  static_start_time = btor_time_stamp ();
#endif
  res              = BTOR_UNKNOWN_EXIT;
  sat_res          = BOOLECTOR_UNKNOWN;
  static_verbosity = 0;
  log              = 0;
  lingeling_opts   = 0;
  inc = incid = incla = incint = dump = 0;

  static_app = btormain_new_btormain (boolector_new ());

  btormain_init_opts (static_app);

  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] != '-')
    {
      if (static_app->close_infile)
      {
        btormain_error (static_app, "multiple input files");
        goto DONE;
      }
      static_app->infile_name = argv[i];
      if (!btor_file_exists (static_app->infile_name))
        static_app->infile = 0;
      else if (has_suffix (static_app->infile_name, ".gz")
               || has_suffix (static_app->infile_name, ".bz2")
               || has_suffix (static_app->infile_name, "7z"))
      {
        BTOR_NEWN (static_app->mm, cmd, strlen (static_app->infile_name + 40));
        if (has_suffix (static_app->infile_name, ".gz"))
          sprintf (cmd, "gunzip -c %s", static_app->infile_name);
        else if (has_suffix (static_app->infile_name, ".bz2"))
          sprintf (cmd, "bzcat %s", static_app->infile_name);
        else if (has_suffix (static_app->infile_name, ".7z"))
          sprintf (cmd, "7z x -so %s 2>/dev/null", static_app->infile_name);
        if ((static_app->infile = popen (cmd, "r")))
          static_app->close_infile = 2;
        BTOR_DELETEN (
            static_app->mm, cmd, strlen (static_app->infile_name + 40));
      }
      else if ((static_app->infile = fopen (static_app->infile_name, "r")))
        static_app->close_infile = 1;

      if (!static_app->infile)
      {
        btormain_error (
            static_app, "can not read '%s'", static_app->infile_name);
        goto DONE;
      }

      continue;
    }

    k       = 0;
    val     = 0;
    readval = 0;
    len     = strlen (argv[i]);
    shrt    = argv[i][1] == '-' ? 0 : 1;
    for (j = shrt ? 1 : 2; j < len && argv[i][j] != '='; j++, k++)
      opt[k] = argv[i][j] == '-' ? '_' : argv[i][j];
    opt[k] = '\0';
    valstr = argv[i] + j + 1;
    if (argv[i][j] == '=')
    {
      val     = atoi (valstr);
      readval = 1;
    }

    if ((shrt && static_app->opts.help.shrt
         && !strcmp (opt, static_app->opts.help.shrt))
        || !strcmp (opt, static_app->opts.help.lng))
    {
      print_help (static_app);
      goto DONE;
    }
    else if ((shrt && static_app->opts.copyright.shrt
              && !strcmp (opt, static_app->opts.copyright.shrt))
             || !strcmp (opt, static_app->opts.copyright.lng))
    {
      print_copyright (static_app);
      goto DONE;
    }
    else if ((shrt && static_app->opts.version.shrt
              && !strcmp (opt, static_app->opts.version.shrt))
             || !strcmp (opt, static_app->opts.version.lng))
    {
      print_version (static_app);
      goto DONE;
    }
    else if ((shrt && static_app->opts.time.shrt
              && !strcmp (opt, static_app->opts.time.shrt))
             || !strcmp (opt, static_app->opts.time.lng))
    {
      if (!readval && ++i >= argc)
      {
        btormain_error (
            static_app,
            "missing argument for '%s%s'",
            shrt ? "-" : "--",
            shrt ? static_app->opts.time.shrt : static_app->opts.time.lng);
        goto DONE;
      }
      else if (!readval)
        val = atoi (argv[i]);

      static_set_alarm = val;
      if (static_set_alarm <= 0)
      {
        btormain_error (
            static_app,
            "invalid argument for '%s%s'",
            shrt ? "-" : "--",
            shrt ? static_app->opts.time.shrt : static_app->opts.time.lng);
        goto DONE;
      }
      boolector_set_opt (static_app->btor, "time", static_set_alarm);
    }
    else if ((shrt && static_app->opts.output.shrt
              && !strcmp (opt, static_app->opts.output.shrt))
             || !strcmp (opt, static_app->opts.output.lng))
    {
      if (!readval && ++i > argc)
      {
        btormain_error (
            static_app,
            "missing argument for '%s%s'",
            shrt ? "-" : "--",
            shrt ? static_app->opts.output.shrt : static_app->opts.output.lng);
        goto DONE;
      }

      if (static_app->close_outfile)
      {
        btormain_error (static_app, "multiple output files");
        goto DONE;
      }

      static_app->outfile = fopen (argv[i], "w");
      if (!static_app->outfile)
      {
        btormain_error (static_app, "can not create '%s'", argv[i]);
        goto DONE;
      }
      static_app->close_outfile = 1;
    }
#ifdef BTOR_USE_LINGELING
    else if ((shrt && static_app->opts.lingeling_opts.shrt
              && !strcmp (opt, static_app->opts.lingeling_opts.shrt))
             || !strcmp (opt, static_app->opts.lingeling_opts.lng))
    {
      if (!readval && ++i > argc)
      {
        btormain_error (static_app,
                        "missing argument for '%s%s'",
                        shrt ? "-" : "--",
                        shrt ? static_app->opts.lingeling_opts.shrt
                             : static_app->opts.lingeling_opts.lng);
        goto DONE;
      }

      lingeling_opts = valstr;
    }
#endif
    else
    {
      if (!strcmp (opt, "btor"))
        boolector_set_opt (
            static_app->btor, "input_format", BTOR_INPUT_FORMAT_BTOR);
      else if (!strcmp (opt, "smt"))
        boolector_set_opt (
            static_app->btor, "input_format", BTOR_INPUT_FORMAT_SMT2);
      else if (!strcmp (opt, "smt1"))
        boolector_set_opt (
            static_app->btor, "input_format", BTOR_INPUT_FORMAT_SMT1);
      else if (!strcmp (opt, "x") || !strcmp (opt, "hex"))
        boolector_set_opt (
            static_app->btor, "output_number_format", BTOR_OUTPUT_BASE_HEX);
      else if (!strcmp (opt, "d") || !strcmp (opt, "dec"))
        boolector_set_opt (
            static_app->btor, "output_number_format", BTOR_OUTPUT_BASE_DEC);
      else if (!strcmp (opt, "db") || !strcmp (opt, "dump_btor"))
      {
        dump = BTOR_OUTPUT_FORMAT_BTOR;
        boolector_set_opt (static_app->btor, "output_format", dump);
      }
      else if (!strcmp (opt, "ds") || !strcmp (opt, "dump_smt"))
      {
        dump = BTOR_OUTPUT_FORMAT_SMT2;
        boolector_set_opt (static_app->btor, "output_format", dump);
      }
      else if (!strcmp (opt, "ds1") || !strcmp (opt, "dump_smt1"))
      {
        dump = BTOR_OUTPUT_FORMAT_SMT1;
        boolector_set_opt (static_app->btor, "output_format", dump);
      }
      else
      {
        for (o = btor_first_opt (static_app->btor);
             o <= btor_last_opt (static_app->btor);
             o++)
        {
          if ((shrt && o->shrt && !strcmp (o->shrt, opt))
              || !strcmp (o->lng, opt))
            break;
        }

        if (o > btor_last_opt (static_app->btor))
        {
          btormain_error (
              static_app, "invalid option '%s%s'", shrt ? "-" : "--", opt);
          goto DONE;
        }

        if ((shrt && o->shrt && !strcmp (o->shrt, "i"))
            || !strcmp (o->lng, "incremental"))
        {
          inc |= BTOR_PARSE_MODE_BASIC_INCREMENTAL;
          boolector_set_opt (static_app->btor, o->lng, inc);
        }
        else if ((shrt && o->shrt && !strcmp (o->shrt, "I"))
                 || !strcmp (o->lng, "incremental_all"))
        {
          boolector_set_opt (static_app->btor,
                             o->lng,
                             BTOR_PARSE_MODE_INCREMENTAL_BUT_CONTINUE);
          inc |= BTOR_PARSE_MODE_INCREMENTAL_BUT_CONTINUE;
          boolector_set_opt (static_app->btor, "incremental", inc);
        }
        else if (!strcmp (o->lng, "incremental_in_depth"))
        {
          if (incla || incint)
          {
            btormain_error (static_app,
                            "Can only use one out of '--%s', '--%s', or '--%s'",
                            "incremental-in-depth",
                            "incremental-look-ahead",
                            "incremental-interval");
            goto DONE;
          }

          if (!readval && ++i >= argc)
          {
            btormain_error (static_app,
                            "missing argument for '%s%s'",
                            shrt ? "-" : "--",
                            shrt ? o->shrt : o->lng);
            goto DONE;
          }
          else if (!readval)
            val = atoi (argv[i]);

          if (val < 1)
          {
            btormain_error (static_app,
                            "incremental in-depth width must be >= 1");
            goto DONE;
          }

          boolector_set_opt (static_app->btor, o->lng, val);
          incid = 1;
        }
        else if (!strcmp (o->lng, "incremental_look_ahead"))
        {
          if (incid || incint)
          {
            btormain_error (static_app,
                            "Can only use one out of '--%s', '--%s', or '--%s'",
                            "incremental-in-depth",
                            "incremental-look-ahead",
                            "incremental-interval");
            goto DONE;
          }

          if (!readval && ++i >= argc)
          {
            btormain_error (static_app,
                            "missing argument for '%s%s'",
                            shrt ? "-" : "--",
                            shrt ? o->shrt : o->lng);
            goto DONE;
          }
          else if (!readval)
            val = atoi (argv[i]);

          if (val < 1)
          {
            btormain_error (static_app,
                            "incremental look-ahead width must be >= 1");
            goto DONE;
          }

          boolector_set_opt (static_app->btor, o->lng, val);
          incla = 1;
        }
        else if (!strcmp (o->lng, "incremental_inverval"))
        {
          if (incid || incla)
          {
            btormain_error (static_app,
                            "Can only use one out of '--%s', '--%s', or '--%s'",
                            "incremental-in-depth",
                            "incremental-look-ahead",
                            "incremental-interval");
            goto DONE;
          }

          if (!readval && ++i >= argc)
          {
            btormain_error (static_app,
                            "missing argument for '%s%s'",
                            shrt ? "-" : "--",
                            shrt ? o->shrt : o->lng);
            goto DONE;
          }
          else if (!readval)
            val = atoi (argv[i]);

          if (val < 1)
          {
            btormain_error (static_app,
                            "incremental interval width must be >= 1");
            goto DONE;
          }

          boolector_set_opt (static_app->btor, o->lng, val);
          incint = 1;
        }
        else if ((shrt && o->shrt && !strcmp (o->shrt, "dp"))
                 || !strcmp (o->lng, "dual_prop"))
        {
          if (boolector_get_opt (static_app->btor, "just")->val)
          {
            btormain_error (
                static_app,
                "multiple exclusive optimization techniques enabled");
            goto DONE;
          }
          boolector_set_opt (static_app->btor, o->lng, 1);
        }
        else if ((shrt && o->shrt && !strcmp (o->shrt, "ju"))
                 || !strcmp (o->lng, "just"))
        {
          if (boolector_get_opt (static_app->btor, "dual_prop")->val)
          {
            btormain_error (
                static_app,
                "multiple exclusive optimization techniques enabled");
            goto DONE;
          }
          boolector_set_opt (static_app->btor, o->lng, 1);
        }
        else if ((shrt && o->shrt && !strcmp (o->shrt, "rwl"))
                 || !strcmp (o->lng, "rewrite_level"))
        {
          if (!readval && ++i >= argc)
          {
            btormain_error (static_app,
                            "missing argument for '%s%s'",
                            shrt ? "-" : "--",
                            shrt ? o->shrt : o->lng);
            goto DONE;
          }
          else if (!readval)
            val = atoi (argv[i]);

          if (val > 3 || val < 0)
          {
            btormain_error (static_app, "rewrite level not in [0,3]");
            goto DONE;
          }

          boolector_set_opt (static_app->btor, o->lng, val);
        }
        else if (!strcmp (o->lng, "rewrite_level_pbr"))
        {
          if (!readval && ++i >= argc)
          {
            btormain_error (static_app, "missing argument for '--%s'", o->lng);
            goto DONE;
          }
          else if (!readval)
            val = atoi (argv[i]);

          if (val > 3 || val < 0)
          {
            btormain_error (static_app, "rewrite level not in [0,3]");
            goto DONE;
          }

          boolector_set_opt (static_app->btor, o->lng, val);
        }
#ifndef NBTORLOG
        else if ((shrt && o->shrt && !strcmp (o->shrt, "l"))
                 || !strcmp (o->lng, "loglevel"))
        {
          log += 1;
        }
#endif
        else if ((shrt && o->shrt && !strcmp (o->shrt, "v"))
                 || !strcmp (o->lng, "verbosity"))
        {
          static_verbosity += 1;
        }
        else
          boolector_set_opt (static_app->btor, o->lng, 1);
      }
    }
  }

  assert (!static_app->done && !static_app->err);

#ifndef NBTORLOG
  boolector_set_opt (static_app->btor, "loglevel", log);
#endif
  boolector_set_opt (static_app->btor, "verbosity", static_verbosity);

  if (!inc && (incid || incla || incint))
  {
    boolector_set_opt (
        static_app->btor, "incremental", BTOR_PARSE_MODE_BASIC_INCREMENTAL);
  }

  forced_sat_solver = 0;
#ifdef BTOR_USE_LINGELING
  if (static_app->opts.lingeling.val)
  {
    if (forced_sat_solver++)
    {
      btormain_error (static_app, "multiple sat solvers forced");
      goto DONE;
    }
    if (!boolector_set_sat_solver_lingeling (
            static_app->btor,
            lingeling_opts,
            static_app->opts.lingeling_nofork.val))
      btormain_error (
          static_app, "invalid options to Lingeling: '%s'", lingeling_opts);
  }
#endif
#ifdef BTOR_USE_PICOSAT
  if (static_app->opts.picosat.val)
  {
    if (forced_sat_solver++)
    {
      btormain_error (static_app, "multiple sat solvers forced");
      goto DONE;
    }
    boolector_set_sat_solver_picosat (static_app->btor);
  }
#endif
#ifdef BTOR_USE_MINISAT
  if (static_app->opts.minisat.val)
  {
    if (forced_sat_solver++)
    {
      btormain_error (static_app, "multiple sat solvers forced");
      goto DONE;
    }
    boolector_set_sat_solver_minisat (static_app->btor);
  }
#endif
  if (!forced_sat_solver)
  {
#if defined(BTOR_USE_LINGELING)
    if (!boolector_set_sat_solver_lingeling (
            static_app->btor,
            lingeling_opts,
            static_app->opts.lingeling_nofork.val))
      btormain_error (
          static_app, "invalid options to Lingeling: '%s'", lingeling_opts);
#elif defined(BTOR_USE_PICOSAT)
    boolector_set_sat_solver_picosat (static_app->btor);
#elif defined(BTOR_USE_MINISAT)
    boolector_set_sat_solver_minisat (static_app->btor);
#else
#error "no SAT solver configured"
#endif
  }

  if (static_verbosity)
  {
    if (inc) btormain_msg ("incremental mode through command line option");
    if (incid)
      btormain_msg (
          "incremental in-depth window of %d",
          boolector_get_opt (static_app->btor, "incremental_in_depth")->val);
    if (incla)
      btormain_msg (
          "incremental look-ahead window of %d",
          boolector_get_opt (static_app->btor, "incremental_look_ahead")->val);
    if (incint)
      btormain_msg (
          "incremental interval window of %d",
          boolector_get_opt (static_app->btor, "incremental_interval")->val);

    btormain_msg ("Boolector Version %s %s\n", BTOR_VERSION, BTOR_ID);
    btormain_msg ("%s\n", BTOR_CFLAGS);
    btormain_msg ("released %s\n", BTOR_RELEASED);
    btormain_msg ("compiled %s\n", BTOR_COMPILED);
    if (*BTOR_CC) btormain_msg ("%s\n", BTOR_CC);

    btormain_msg ("setting signal handlers");
  }
  set_sig_handlers ();

  if (static_set_alarm)
  {
    if (static_verbosity)
      btormain_msg ("setting time limit to %d seconds\n", static_set_alarm);
    set_alarm ();
  }
  else if (static_verbosity)
    btormain_msg ("no time limit given");

  if (inc && static_verbosity) btormain_msg ("starting incremental mode");

  if ((val = boolector_get_opt (static_app->btor, "input_format")->val))
  {
    switch (val)
    {
      case BTOR_INPUT_FORMAT_BTOR:
        if (static_verbosity)
          btormain_msg ("BTOR input forced through cmd line options");
        parse_result = boolector_parse_btor (static_app->btor,
                                             static_app->infile,
                                             static_app->infile_name,
                                             &parse_error_msg,
                                             &parse_status);
        break;
      case BTOR_INPUT_FORMAT_SMT1:
        if (static_verbosity)
          btormain_msg ("SMT-LIB v1 input forced through cmd line options");
        parse_result = boolector_parse_smt1 (static_app->btor,
                                             static_app->infile,
                                             static_app->infile_name,
                                             &parse_error_msg,
                                             &parse_status);
        break;
      case BTOR_INPUT_FORMAT_SMT2:
        if (static_verbosity)
          btormain_msg ("SMT-LIB v2 input forced through cmd line options");
        parse_result = boolector_parse_smt2 (static_app->btor,
                                             static_app->infile,
                                             static_app->infile_name,
                                             &parse_error_msg,
                                             &parse_status);
        break;
    }
  }
  else
    parse_result = boolector_parse (static_app->btor,
                                    static_app->infile,
                                    static_app->infile_name,
                                    &parse_error_msg,
                                    &parse_status);

  if (parse_result == BOOLECTOR_PARSE_ERROR)
  {
    btormain_error (static_app, parse_error_msg);
    goto DONE;
  }

  if (inc)
  {
    if (parse_result == BOOLECTOR_SAT)
    {
      if (static_verbosity)
        btormain_msg ("one formula SAT in incremental mode");
      sat_res = BOOLECTOR_SAT;
    }
    else if (parse_result == BOOLECTOR_UNSAT)
    {
      if (static_verbosity)
        btormain_msg ("all formulas UNSAT in incremental mode");
      sat_res = BOOLECTOR_UNSAT;
    }

    print_sat_result (static_app, sat_res);

    if (boolector_get_opt (static_app->btor, "model_gen")->val
        && sat_res == BOOLECTOR_SAT)
      boolector_print_model (static_app->btor, static_app->outfile);

    if (static_verbosity) boolector_print_stats (static_app->btor);
  }
  else if (dump)
  {
    switch (dump)
    {
      case BTOR_OUTPUT_FORMAT_BTOR:
        if (static_verbosity) btormain_msg ("dumping BTOR expressions");
        boolector_dump_btor (static_app->btor, static_app->outfile);
        break;
      case BTOR_OUTPUT_FORMAT_SMT1:
        if (static_verbosity) btormain_msg ("dumping in SMT-LIB v1 format");
        boolector_dump_smt1 (static_app->btor, static_app->outfile);
        break;
      default:
        assert (dump == BTOR_OUTPUT_FORMAT_SMT2);
        if (static_verbosity) btormain_msg ("dumping in SMT 2.0 format");
        boolector_dump_smt2 (static_app->btor, static_app->outfile);
    }
    goto DONE;
  }

  sat_res = boolector_sat (static_app->btor);
  assert (sat_res != BOOLECTOR_UNKNOWN);

  /* check if status is equal to benchmark status */
  if (sat_res == BOOLECTOR_SAT && parse_status == BOOLECTOR_UNSAT)
    btormain_error (static_app,
                    "'sat' but status of benchmark in '%s' is 'unsat'",
                    static_app->infile_name);
  else if (sat_res == BOOLECTOR_UNSAT && parse_status == BOOLECTOR_SAT)
    btormain_error (static_app,
                    "'unsat' but status of benchmark in '%s' is 'sat'\n",
                    static_app->infile_name);
  else
    print_sat_result (static_app, sat_res);

  if (boolector_get_opt (static_app->btor, "model_gen")->val
      && sat_res == BOOLECTOR_SAT)
    boolector_print_model (static_app->btor, static_app->outfile);

  if (static_verbosity)
  {
    boolector_print_stats (static_app->btor);
    print_static_stats ();
  }

DONE:
  if (static_app->done)
    res = BTOR_SUCC_EXIT;
  else if (static_app->err)
    res = BTOR_ERR_EXIT;
  else if (sat_res == BOOLECTOR_UNSAT)
    res = BTOR_UNSAT_EXIT;
  else if (sat_res == BOOLECTOR_SAT)
    res = BTOR_SAT_EXIT;

  assert (res == BTOR_ERR_EXIT || res == BTOR_SUCC_EXIT || res == BTOR_SAT_EXIT
          || res == BTOR_UNSAT_EXIT || res == BTOR_UNKNOWN_EXIT);

  if (static_app->close_infile == 1)
    fclose (static_app->infile);
  else if (static_app->close_infile == 2)
    pclose (static_app->infile);
  if (static_app->close_outfile) fclose (static_app->outfile);

  btormain_delete_btormain (static_app);
  reset_sig_handlers ();

  return res;
}
