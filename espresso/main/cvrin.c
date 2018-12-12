/*
    module: cvrin.c
    purpose: cube and cover input routines
*/

#include "espresso.h"

static bool line_length_error;
static int lineno;

/*
 *  Yes, I know this routine is a mess
 */
void read_cube(PLA, no_inputs, no_outputs, input, output)
pPLA PLA;
int* input;
int* output;
int no_inputs;
int no_outputs;
{
    register int var, i;
    pcube cf = cube.temp[0], cr = cube.temp[1], cd = cube.temp[2];
    bool savef = FALSE, saved = FALSE, saver = FALSE;

    set_clear(cf, cube.size);

    for (var = 0; var < no_inputs; var++)
	    switch(input[var]) {
	    	case 0:
		    set_insert(cf, var*2);
		    break;
		case 1:
		    set_insert(cf, var*2+1);
		    break;
		case 2:
		    set_insert(cf, var*2+1);
            set_insert(cf, var*2);
		    break;
    	}

    // Loop for last multiple-valued variable
    set_copy(cr, cf);
    set_copy(cd, cf);

    for (i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        //TODO for multi-output Karnaugh tables, this is the place to edit
        switch (output[0]) {
            case 0:
                if (PLA->pla_type & R_type) set_insert(cr, i), saver = TRUE;
                break;
            case 1:
                if (PLA->pla_type & F_type) set_insert(cf, i), savef = TRUE;
                break;
            case 2:
                if (PLA->pla_type & D_type) set_insert(cd, i), saved = TRUE;
                break;
        }
    }

    if (savef) PLA->F = sf_addset(PLA->F, cf);
    if (saved) PLA->D = sf_addset(PLA->D, cd);
    if (saver) PLA->R = sf_addset(PLA->R, cr);

    return;
}


void parse_pla(PLA, no_inputs, no_outputs, input, output, no_lines)
INOUT pPLA PLA;
int no_inputs;
int no_outputs;
int** input;
int** output;
int* no_lines;
{
    int i, var, ch, np, last;
    char word[256];

    lineno = 1;
    line_length_error = FALSE;

    cube.num_binary_vars = no_inputs;
    cube.num_vars = cube.num_binary_vars + 1;
    cube.part_size = ALLOC(int, cube.num_vars);

    cube.part_size[cube.num_vars-1] = no_outputs;
    cube_setup();
    PLA_labels(PLA);

    int line = 0;

	for (i = 0; i < no_lines[1]; i++) {
	    if (PLA->F == NULL) {
		PLA->F = new_cover(no_inputs);
		PLA->D = new_cover(no_inputs);
		PLA->R = new_cover(no_inputs);
	    }
	    read_cube(PLA, no_inputs, no_outputs, input[line], output[line]);
	    line++;
    	}
    return;
}
/*
    read_pla -- read a PLA from a file

    Input stops when ".e" is encountered in the input file, or upon reaching
    end of file.

    Returns the PLA in the variable PLA after massaging the "symbolic"
    representation into a positional cube notation of the ON-set, OFF-set,
    and the DC-set.

    needs_dcset and needs_offset control the computation of the OFF-set
    and DC-set (i.e., if either needs to be computed, then it will be
    computed via complement only if the corresponding option is TRUE.)
    pla_type specifies the interpretation to be used when reading the
    PLA.

    The phase of the output functions is adjusted according to the
    global option "pos" or according to an imbedded .phase option in
    the input file.  Note that either phase option implies that the
    OFF-set be computed regardless of whether the caller needs it
    explicitly or not.

    Bit pairing of the binary variables is performed according to an
    imbedded .pair option in the input file.

    The global cube structure also reflects the sizes of the PLA which
    was just read.  If these fields have already been set, then any
    subsequent PLA must conform to these sizes.

    The global flags trace and summary control the output produced
    during the read.

    Returns a status code as a result:
	EOF (-1) : End of file reached before any data was read
	> 0	 : Operation successful
*/

int read_pla(needs_dcset, needs_offset, pla_type, PLA_return, no_inputs, no_outputs, input, output, no_lines)
IN bool needs_dcset, needs_offset;
IN int pla_type;
OUT pPLA *PLA_return;
int no_inputs;
int no_outputs;
int** input;
int** output;
int* no_lines;
{
    pPLA PLA;
    int i, second, third;
    long time;
    cost_t cost;

    /* Allocate and initialize the PLA structure */
    PLA = *PLA_return = new_PLA();
    PLA->pla_type = pla_type;

    /* Read the pla */
    time = ptime();
    parse_pla(PLA, no_inputs, no_outputs, input, output, no_lines);

    /* Check for nothing on the file -- implies reached EOF */
    if (PLA->F == NULL) {
	return EOF;
    }

    /* This hack merges the next-state field with the outputs */
    for(i = 0; i < cube.num_vars; i++) {
	cube.part_size[i] = ABS(cube.part_size[i]);
    }

    /* Decide how to break PLA into ON-set, OFF-set and DC-set */
    time = ptime();
    if (pos || PLA->phase != NULL || PLA->symbolic_output != NIL(symbolic_t)) {
	needs_offset = TRUE;
    }
    if (needs_offset && (PLA->pla_type==F_type || PLA->pla_type==FD_type)) {
	free_cover(PLA->R);
	PLA->R = complement(cube2list(PLA->F, PLA->D));
    } else if (needs_dcset && PLA->pla_type == FR_type) {
	pcover X;
	free_cover(PLA->D);
	/* hack, why not? */
	X = d1merge(sf_join(PLA->F, PLA->R), cube.num_vars - 1);
	PLA->D = complement(cube1list(X));
	free_cover(X);
    } else if (PLA->pla_type == R_type || PLA->pla_type == DR_type) {
	free_cover(PLA->F);
	PLA->F = complement(cube2list(PLA->D, PLA->R));
    }

    if (trace) {
	totals(time, COMPL_TIME, PLA->R, &cost);
    }

    /* Check for phase rearrangement of the functions */
    if (pos) {
	pcover onset = PLA->F;
	PLA->F = PLA->R;
	PLA->R = onset;
	PLA->phase = new_cube();
	set_diff(PLA->phase, cube.fullset, cube.var_mask[cube.num_vars-1]);
    } else if (PLA->phase != NULL) {
	(void) set_phase(PLA);
    }

    /* Setup minimization for two-bit decoders */
    if (PLA->pair != (ppair) NULL) {
	set_pair(PLA);
    }

    return 1;
}

void PLA_summary(PLA)
pPLA PLA;
{
    int var, i;
    symbolic_list_t *p2;
    symbolic_t *p1;

    printf("# PLA is %s", PLA->filename);
    if (cube.num_binary_vars == cube.num_vars - 1)
	printf(" with %d inputs and %d outputs\n",
	    cube.num_binary_vars, cube.part_size[cube.num_vars - 1]);
    else {
	printf(" with %d variables (%d binary, mv sizes",
	    cube.num_vars, cube.num_binary_vars);
	for(var = cube.num_binary_vars; var < cube.num_vars; var++)
	    printf(" %d", cube.part_size[var]);
	printf(")\n");
    }
    printf("# ON-set cost is  %s\n", print_cost(PLA->F));
    printf("# OFF-set cost is %s\n", print_cost(PLA->R));
    printf("# DC-set cost is  %s\n", print_cost(PLA->D));
    if (PLA->phase != NULL)
	printf("# phase is %s\n", pc1(PLA->phase));
    if (PLA->pair != NULL) {
	printf("# two-bit decoders:");
	for(i = 0; i < PLA->pair->cnt; i++)
	    printf(" (%d %d)", PLA->pair->var1[i], PLA->pair->var2[i]);
	printf("\n");
    }
    if (PLA->symbolic != NIL(symbolic_t)) {
	for(p1 = PLA->symbolic; p1 != NIL(symbolic_t); p1 = p1->next) {
	    printf("# symbolic: ");
	    for(p2=p1->symbolic_list; p2!=NIL(symbolic_list_t); p2=p2->next) {
		printf(" %d", p2->variable);
	    }
	    printf("\n");
	}
    }
    if (PLA->symbolic_output != NIL(symbolic_t)) {
	for(p1 = PLA->symbolic_output; p1 != NIL(symbolic_t); p1 = p1->next) {
	    printf("# output symbolic: ");
	    for(p2=p1->symbolic_list; p2!=NIL(symbolic_list_t); p2=p2->next) {
		printf(" %d", p2->pos);
	    }
	    printf("\n");
	}
    }
    (void) fflush(stdout);
}


pPLA new_PLA()
{
    pPLA PLA;

    PLA = ALLOC(PLA_t, 1);
    PLA->F = PLA->D = PLA->R = (pcover) NULL;
    PLA->phase = (pcube) NULL;
    PLA->pair = (ppair) NULL;
    PLA->label = (char **) NULL;
    PLA->filename = (char *) NULL;
    PLA->pla_type = 0;
    PLA->symbolic = NIL(symbolic_t);
    PLA->symbolic_output = NIL(symbolic_t);
    return PLA;
}


PLA_labels(PLA)
pPLA PLA;
{
    int i;

    PLA->label = ALLOC(char *, cube.size);
    for(i = 0; i < cube.size; i++)
	PLA->label[i] = (char *) NULL;
}


void free_PLA(PLA)
pPLA PLA;
{
    symbolic_list_t *p2, *p2next;
    symbolic_t *p1, *p1next;
    int i;

    if (PLA->F != (pcover) NULL)
	free_cover(PLA->F);
    if (PLA->R != (pcover) NULL)
	free_cover(PLA->R);
    if (PLA->D != (pcover) NULL)
	free_cover(PLA->D);
    if (PLA->phase != (pcube) NULL)
	free_cube(PLA->phase);
    if (PLA->pair != (ppair) NULL) {
	FREE(PLA->pair->var1);
	FREE(PLA->pair->var2);
	FREE(PLA->pair);
    }
    if (PLA->label != NULL) {
	for(i = 0; i < cube.size; i++)
	    if (PLA->label[i] != NULL)
		FREE(PLA->label[i]);
	FREE(PLA->label);
    }
    if (PLA->filename != NULL) {
	FREE(PLA->filename);
    }
    for(p1 = PLA->symbolic; p1 != NIL(symbolic_t); p1 = p1next) {
	for(p2 = p1->symbolic_list; p2 != NIL(symbolic_list_t); p2 = p2next) {
	    p2next = p2->next;
	    FREE(p2);
	}
	p1next = p1->next;
	FREE(p1);
    }
    PLA->symbolic = NIL(symbolic_t);
    for(p1 = PLA->symbolic_output; p1 != NIL(symbolic_t); p1 = p1next) {
	for(p2 = p1->symbolic_list; p2 != NIL(symbolic_list_t); p2 = p2next) {
	    p2next = p2->next;
	    FREE(p2);
	}
	p1next = p1->next;
	FREE(p1);
    }
    PLA->symbolic_output = NIL(symbolic_t);
    FREE(PLA);
}

int label_index(PLA, word, varp, ip)
pPLA PLA;
char *word;
int *varp;
int *ip;
{
    int var, i;

    if (PLA->label == NIL(char *) || PLA->label[0] == NIL(char)) {
	if (sscanf(word, "%d", varp) == 1) {
	    *ip = *varp;
	    return TRUE;
	}
    } else {
	for(var = 0; var < cube.num_vars; var++) {
	    for(i = 0; i < cube.part_size[var]; i++) {
		if (equal(PLA->label[cube.first_part[var]+i], word)) {
		    *varp = var;
		    *ip = i;
		    return TRUE;
		}
	    }
	}
    }
    return FALSE;
}
