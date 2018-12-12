/*
 *  Main driver for espresso
 *
 *  Rewritten for version #2.2 to use getopt(3), to make passing
 *  arguments to subcommands a little easier.
 *
 *  Old style -do xxx, -out xxx, etc. are still supported.
 */

#include "espresso.h"
#include "main.h"		/* table definitions for options */
#include "unistd.h"

static FILE *last_fp;
static int input_type = FD_type;

minimise_karnaugh(int no_inputs, int no_outputs, int** input, int** output, int* no_lines, bool onlymerge)
{
	int i, j, first, last, strategy, out_type, option;
	pPLA PLA, PLA1;
	pcover F, Fold, Dold;
	pset last1, p;
	cost_t cost;
	bool error, exact_cover;
	long start;
	extern char *optarg;
	extern int optind;

	start = ptime();

	error = FALSE;
	init_runtime();
#ifdef RANDOM
	srandom(314973);
#endif

    /* use getopt(3) to unpack the command line arguments */
    option = 0;         // default -D: ESPRESSO
    out_type = F_type;  //default -o: default is ON-set only */
    debug = 0;          // default -d: no debugging info
    verbose_debug = FALSE;  // default -v: not verbose
    print_solution = FALSE; // default -x: print the solution (!)
    summary = FALSE;    // default -s: no summary
    trace = FALSE;  // default -t: no trace information
    strategy = 0;   // default -S: strategy number
    first = -1; // default -R: select range
    last = -1;
    remove_essential = TRUE;	// default -e:
    force_irredundant = TRUE;
    unwrap_onset = TRUE;
    single_expand = FALSE;
    pos = FALSE;
    //pos = TRUE;
    recompute_onset = FALSE;
    use_super_gasp = FALSE;
    use_random_order = FALSE;
    kiss = FALSE;
    echo_comments = TRUE;
    echo_unknown_commands = TRUE;
    exact_cover = FALSE;	// for -qm option, the default

    if (onlymerge) option = 22;

    PLA = NIL(PLA_t);
    getPLA(optind++, option, &PLA, out_type, no_inputs, no_outputs, input, output, no_lines);

    /*printf("before minim:\n");
    EXECUTE(fprint_pla_orig(stdout, PLA, out_type), WRITE_TIME, PLA->F, cost);
    printf("-----------\n");*/

    Fold = sf_save(PLA->F);
    PLA->F = espresso(PLA->F, PLA->D, PLA->R);
    EXECUTE(error=verify(PLA->F,Fold,PLA->D), VERIFY_TIME, PLA->F, cost);
    if (error) {
        print_solution = FALSE;
        PLA->F = Fold;
        (void) check_consistency(PLA);
    } else {
        free_cover(Fold);
    }

    /*printf("after minim:\n");
    EXECUTE(fprint_pla_orig(stdout, PLA, out_type), WRITE_TIME, PLA->F, cost);
    printf("-----------\n");*/
    fprint_pla(PLA, out_type, no_inputs, no_outputs, input, output, no_lines);

    /* Crash and burn if there was a verify error */
    if (error) {
        fatal("cover verification failed");
    }

    /* cleanup all used memory */
    free_PLA(PLA);
    FREE(cube.part_size);
    setdown_cube();             /* free the cube/cdata structure data */
    sf_cleanup();               /* free unused set structures */
    sm_cleanup();               /* sparse matrix cleanup */
}


getPLA(opt, option, PLA, out_type, no_inputs, no_outputs, input, output, no_lines)
int opt;
int option;
pPLA *PLA;
int out_type;
int no_inputs;
int no_outputs;
int** input;
int** output;
int* no_lines;
{
    FILE *fp;
    int needs_dcset, needs_offset;
    char *fname;

	needs_dcset = option_table[option].needs_dcset;
	needs_offset = option_table[option].needs_offset;

    if (read_pla(needs_dcset, needs_offset, input_type, PLA, no_inputs, no_outputs, input, output, no_lines) == EOF) {
	    printf("problem reading PLA!\n");
	exit(1);
    }

    last_fp = fp;
}


init_runtime()
{
    total_name[READ_TIME] =     "READ       ";
    total_name[WRITE_TIME] =    "WRITE      ";
    total_name[COMPL_TIME] =    "COMPL      ";
    total_name[REDUCE_TIME] =   "REDUCE     ";
    total_name[EXPAND_TIME] =   "EXPAND     ";
    total_name[ESSEN_TIME] =    "ESSEN      ";
    total_name[IRRED_TIME] =    "IRRED      ";
    total_name[GREDUCE_TIME] =  "REDUCE_GASP";
    total_name[GEXPAND_TIME] =  "EXPAND_GASP";
    total_name[GIRRED_TIME] =   "IRRED_GASP ";
    total_name[MV_REDUCE_TIME] ="MV_REDUCE  ";
    total_name[RAISE_IN_TIME] = "RAISE_IN   ";
    total_name[VERIFY_TIME] =   "VERIFY     ";
    total_name[PRIMES_TIME] =   "PRIMES     ";
    total_name[MINCOV_TIME] =   "MINCOV     ";
}



