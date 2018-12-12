/*
    module: cvrout.c
    purpose: cube and cover output routines
*/

#include "espresso.h"

void pls_group(PLA, fp)
pPLA PLA;
FILE *fp;
{
    int var, i, col, len;

    fprintf(fp, "\n.group");
    col = 6;
    for(var = 0; var < cube.num_vars-1; var++) {
    fprintf(fp, " ("), col += 2;
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        len = strlen(PLA->label[i]);
        if (col + len > 75)
        fprintf(fp, " \\\n"), col = 0;
        else if (i != 0)
        putc(' ', fp), col += 1;
        fprintf(fp, "%s", PLA->label[i]), col += len;
    }
    fprintf(fp, ")"), col += 1;
    }
    fprintf(fp, "\n");
}


void pls_label(PLA, fp)
pPLA PLA;
FILE *fp;
{
    int var, i, col, len;

    fprintf(fp, ".label");
    col = 6;
    for(var = 0; var < cube.num_vars; var++)
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        len = strlen(PLA->label[i]);
        if (col + len > 75)
        fprintf(fp, " \\\n"), col = 0;
        else
        putc(' ', fp), col += 1;
        fprintf(fp, "%s", PLA->label[i]), col += len;
    }
}

void fpr_header(fp, PLA, output_type)
FILE *fp;
pPLA PLA;
int output_type;
{
    register int i, var;
    int first, last;

    /* .type keyword gives logical type */
    if (output_type != F_type) {
    fprintf(fp, ".type ");
    if (output_type & F_type) putc('f', fp);
    if (output_type & D_type) putc('d', fp);
    if (output_type & R_type) putc('r', fp);
    putc('\n', fp);
    }

    /* Check for binary or multiple-valued labels */
    if (cube.num_mv_vars <= 1) {
    fprintf(fp, ".i %d\n", cube.num_binary_vars);
    if (cube.output != -1)
        fprintf(fp, ".o %d\n", cube.part_size[cube.output]);
    } else {
    fprintf(fp, ".mv %d %d", cube.num_vars, cube.num_binary_vars);
    for(var = cube.num_binary_vars; var < cube.num_vars; var++)
        fprintf(fp, " %d", cube.part_size[var]);
    fprintf(fp, "\n");
    }

    /* binary valued labels */
    if (PLA->label != NIL(char *) && PLA->label[1] != NIL(char)
        && cube.num_binary_vars > 0) {
    fprintf(fp, ".ilb");
    for(var = 0; var < cube.num_binary_vars; var++)
        fprintf(fp, " %s", INLABEL(var));
    putc('\n', fp);
    }

    /* output-part (last multiple-valued variable) labels */
    if (PLA->label != NIL(char *) &&
        PLA->label[cube.first_part[cube.output]] != NIL(char)
        && cube.output != -1) {
    fprintf(fp, ".ob");
    for(i = 0; i < cube.part_size[cube.output]; i++)
        fprintf(fp, " %s", OUTLABEL(i));
    putc('\n', fp);
    }

    /* multiple-valued labels */
    for(var = cube.num_binary_vars; var < cube.num_vars-1; var++) {
    first = cube.first_part[var];
    last = cube.last_part[var];
    if (PLA->label != NULL && PLA->label[first] != NULL) {
        fprintf(fp, ".label var=%d", var);
        for(i = first; i <= last; i++) {
        fprintf(fp, " %s", PLA->label[i]);
        }
        putc('\n', fp);
    }
    }

    if (PLA->phase != (pcube) NULL) {
    first = cube.first_part[cube.output];
    last = cube.last_part[cube.output];
    fprintf(fp, "#.phase ");
    for(i = first; i <= last; i++)
        putc(is_in_set(PLA->phase,i) ? '1' : '0', fp);
    fprintf(fp, "\n");
    }
}

void pls_output(PLA)
IN pPLA PLA;
{
    register pcube last, p;

    printf(".option unmerged\n");
    makeup_labels(PLA);
    pls_label(PLA, stdout);
    pls_group(PLA, stdout);
    printf(".p %d\n", PLA->F->count);
    foreach_set(PLA->F, last, p) {
    print_expanded_cube(stdout, p, PLA->phase);
    }
    printf(".end\n");
}

void print_cube_orig(fp, c, out_map)
register FILE *fp;
register pcube c;
register char *out_map;
{
    register int i, var, ch;
    int last;

    for(var = 0; var < cube.num_binary_vars; var++) {
    ch = "?01-" [GETINPUT(c, var)];
    putc(ch, fp);
    }
    for(var = cube.num_binary_vars; var < cube.num_vars - 1; var++) {
    putc(' ', fp);
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        ch = "01" [is_in_set(c, i) != 0];
        putc(ch, fp);
    }
    }
    if (cube.output != -1) {
    last = cube.last_part[cube.output];
    putc(' ', fp);
    for(i = cube.first_part[cube.output]; i <= last; i++) {
        ch = out_map [is_in_set(c, i) != 0];
        putc(ch, fp);
    }
    }
    putc('\n', fp);
}

void print_expanded_cube(fp, c, phase)
register FILE *fp;
register pcube c;
pcube phase;
{
    register int i, var, ch;
    char *out_map;

    for(var = 0; var < cube.num_binary_vars; var++) {
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        ch = "~1" [is_in_set(c, i) != 0];
        putc(ch, fp);
    }
    }
    for(var = cube.num_binary_vars; var < cube.num_vars - 1; var++) {
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        ch = "1~" [is_in_set(c, i) != 0];
        putc(ch, fp);
    }
    }
    if (cube.output != -1) {
    var = cube.num_vars - 1;
    putc(' ', fp);
    for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
        if (phase == (pcube) NULL || is_in_set(phase, i)) {
        out_map = "~1";
        } else {
        out_map = "~0";
        }
        ch = out_map[is_in_set(c, i) != 0];
        putc(ch, fp);
    }
    }
    putc('\n', fp);
}

void fprint_pla_orig(fp, PLA, output_type)
INOUT FILE *fp;
IN pPLA PLA;
IN int output_type;
{
    int num;
    register pcube last, p;

    if (output_type == PLEASURE_type) {
    pls_output(PLA);
    } else if (output_type == EQNTOTT_type) {
    eqn_output(PLA);
    } else {
    fpr_header(fp, PLA, output_type);

    num = 0;
    if (output_type & F_type) num += (PLA->F)->count;
    if (output_type & D_type) num += (PLA->D)->count;
    if (output_type & R_type) num += (PLA->R)->count;
    fprintf(fp, ".p %d\n", num);

    /* quick patch 01/17/85 to support TPLA ! */
    if (output_type == F_type) {
        foreach_set(PLA->F, last, p) {
        print_cube_orig(fp, p, "01");
        }
        fprintf(fp, ".e\n");
    } else {
        if (output_type & F_type) {
        foreach_set(PLA->F, last, p) {
            print_cube_orig(fp, p, "~1");
        }
        }
        if (output_type & D_type) {
        foreach_set(PLA->D, last, p) {
            print_cube_orig(fp, p, "~2");
        }
        }
        if (output_type & R_type) {
        foreach_set(PLA->R, last, p) {
            print_cube_orig(fp, p, "~0");
        }
        }
        fprintf(fp, ".end\n");
    }
    }
}

void fprint_pla(PLA, output_type, no_inputs, no_outputs, input, output, no_lines)
IN pPLA PLA;
IN int output_type;
int** input;
int** output;
int* no_lines;
{
    int num;
    register pcube last, p;
    num = 0;
    if (output_type & F_type) {
        num += (PLA->F)->count;
        no_lines[1] = (PLA->F)->count;
        //printf("no_lines[1]: %d \n", no_lines[1]);
    }
    if (output_type & D_type) {
        num += (PLA->D)->count;
        no_lines[2] = (PLA->D)->count;
        //printf("no_lines[2]: %d \n", no_lines[2]);
    }
    if (output_type & R_type) {
        num += (PLA->R)->count;
        no_lines[0] = (PLA->R)->count;
        //printf("no_lines[0]: %d \n", no_lines[0]);
    }

    int line = 0;
    if (output_type & F_type) {
        foreach_set(PLA->F, last, p) {
            print_cube(p, "~1", input[line], output[line]);
            line++;
        }
    }
    if (output_type & D_type) {
        foreach_set(PLA->D, last, p) {
            print_cube(p, "~2", input[line], output[line]);
            line++;
        }
    }
    if (output_type & R_type) {
        foreach_set(PLA->R, last, p) {
            print_cube(p, "~0", input[line], output[line]);
            line++;
        }
    }
}


/*
    eqntott output mode -- output algebraic equations
*/
void eqn_output(PLA)
pPLA PLA;
{
    register pcube p, last;
    register int i, var, col, len;
    int x;
    bool firstand, firstor;

    if (cube.output == -1)
	fatal("Cannot have no-output function for EQNTOTT output mode");
    if (cube.num_mv_vars != 1)
	fatal("Must have binary-valued function for EQNTOTT output mode");
    makeup_labels(PLA);

    /* Write a single equation for each output */
    for(i = 0; i < cube.part_size[cube.output]; i++) {
	printf("%s = ", OUTLABEL(i));
	col = strlen(OUTLABEL(i)) + 3;
	firstor = TRUE;

	/* Write product terms for each cube in this output */
	foreach_set(PLA->F, last, p)
	    if (is_in_set(p, i + cube.first_part[cube.output])) {
		if (firstor)
		    printf("("), col += 1;
		else
		    printf(" | ("), col += 4;
		firstor = FALSE;
		firstand = TRUE;

		/* print out a product term */
		for(var = 0; var < cube.num_binary_vars; var++)
		    if ((x=GETINPUT(p, var)) != DASH) {
			len = strlen(INLABEL(var));
			if (col+len > 72)
			    printf("\n    "), col = 4;
			if (! firstand)
			    printf("&"), col += 1;
			firstand = FALSE;
			if (x == ZERO)
			    printf("!"), col += 1;
			printf("%s", INLABEL(var)), col += len;
		    }
		printf(")"), col += 1;
	    }
	printf(";\n\n");
    }
}


char *fmt_cube(c, out_map, s)
register pcube c;
register char *out_map, *s;
{
    register int i, var, last, len = 0;

    for(var = 0; var < cube.num_binary_vars; var++) {
	s[len++] = "?01-" [GETINPUT(c, var)];
    }
    for(var = cube.num_binary_vars; var < cube.num_vars - 1; var++) {
	s[len++] = ' ';
	for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
	    s[len++] = "01" [is_in_set(c, i) != 0];
	}
    }
    if (cube.output != -1) {
	last = cube.last_part[cube.output];
	s[len++] = ' ';
	for(i = cube.first_part[cube.output]; i <= last; i++) {
	    s[len++] = out_map [is_in_set(c, i) != 0];
	}
    }
    s[len] = '\0';
    return s;
}


void print_cube(c, out_map, input, output)
register pcube c;
register char *out_map;
int* output;
int* input;
{
    register int i, var, ch;
    int last;

    int where = 0;
    for(var = 0; var < cube.num_binary_vars; var++) {
	ch = "?01-" [GETINPUT(c, var)];
	//printf("%c", ch);
	switch(ch) {
		case '0':
			input[where] = 0;
			break;
		case '1':
			input[where] = 1;
			break;
		case '-':
			input[where] = 2;
			break;
	}
	where++;
    }
    for(var = cube.num_binary_vars; var < cube.num_vars - 1; var++) {
	    //printf(" ", ch);
	for(i = cube.first_part[var]; i <= cube.last_part[var]; i++) {
	    ch = "01" [is_in_set(c, i) != 0];
	    //printf("%c", ch);
	    switch(ch) {
		    case '0':
			    input[where] = 0;
			    break;
		    case '1':
			    input[where] = 1;
			    break;
		    case '-':
			    input[where] = 2;
			    break;
	    }
	    where++;
	}
    }

    where = 0;
    if (cube.output != -1) {
	last = cube.last_part[cube.output];
	//printf(" ", ch);
	for(i = cube.first_part[cube.output]; i <= last; i++) {
	    ch = out_map [is_in_set(c, i) != 0];
	    //printf("%c", ch);
	    switch(ch) {
		    case '0':
			    output[where] = 0;
			    break;
		    case '1':
			    output[where] = 1;
			    break;
		    case '-':
			    output[where] = 2;
			    break;
	    }
	    where++;
	}
    }
    //printf("\n");
}

char *pc1(c) pcube c;
{static char s1[256];return fmt_cube(c, "01", s1);}
char *pc2(c) pcube c;
{static char s2[256];return fmt_cube(c, "01", s2);}


void debug_print(T, name, level)
pcube *T;
char *name;
int level;
{
    register pcube *T1, p, temp;
    register int cnt;

    cnt = CUBELISTSIZE(T);
    temp = new_cube();
    if (verbose_debug && level == 0)
	printf("\n");
    printf("%s[%d]: ord(T)=%d\n", name, level, cnt);
    if (verbose_debug) {
	printf("cofactor=%s\n", pc1(T[0]));
	for(T1 = T+2, cnt = 1; (p = *T1++) != (pcube) NULL; cnt++)
	    printf("%4d. %s\n", cnt, pc1(set_or(temp, p, T[0])));
    }
    free_cube(temp);
}


void debug1_print(T, name, num)
pcover T;
char *name;
int num;
{
    register int cnt = 1;
    register pcube p, last;

    if (verbose_debug && num == 0)
	printf("\n");
    printf("%s[%d]: ord(T)=%d\n", name, num, T->count);
    if (verbose_debug)
	foreach_set(T, last, p)
	    printf("%4d. %s\n", cnt++, pc1(p));
}


void cprint(T)
pcover T;
{
    register pcube p, last;

    foreach_set(T, last, p)
	printf("%s\n", pc1(p));
}


int makeup_labels(PLA)
pPLA PLA;
{
    int var, i, ind;

    if (PLA->label == (char **) NULL)
	PLA_labels(PLA);

    for(var = 0; var < cube.num_vars; var++)
	for(i = 0; i < cube.part_size[var]; i++) {
	    ind = cube.first_part[var] + i;
	    if (PLA->label[ind] == (char *) NULL) {
		PLA->label[ind] = ALLOC(char, 15);
		if (var < cube.num_binary_vars)
		    if ((i % 2) == 0)
			(void) sprintf(PLA->label[ind], "v%d.bar", var);
		    else
			(void) sprintf(PLA->label[ind], "v%d", var);
		else
		    (void) sprintf(PLA->label[ind], "v%d.%d", var, i);
	    }
	}
}

