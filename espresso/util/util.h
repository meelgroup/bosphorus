#ifndef UTIL_H
#define UTIL_H

/*#define USE_MM*/		/* choose libmm.a as the memory allocator */

/* these are too entrenched to get away with changing the name */
#define strsav		util_strsav

#define NIL(type)		((type *) 0)

#ifdef USE_MM
/*
 *  assumes the memory manager is libmm.a
 *	- allows malloc(0) or realloc(obj, 0)
 *	- catches out of memory (and calls MMout_of_memory())
 *	- catch free(0) and realloc(0, size) in the macros
 */
#define ALLOC(type, num)	\
    ((type *) malloc(sizeof(type) * (num)))
#define REALLOC(type, obj, num)	\
    (obj) ? ((type *) realloc((char *) obj, sizeof(type) * (num))) : \
	    ((type *) malloc(sizeof(type) * (num)))
#define FREE(obj)		\
    ((obj) ? (free((char *) (obj)), (obj) = 0) : 0)
#else
/*
 *  enforce strict semantics on the memory allocator
 *	- when in doubt, delete the '#define USE_MM' above
 */
#define ALLOC(type, num)	\
    ((type *) MMalloc((long) sizeof(type) * (long) (num)))
#define REALLOC(type, obj, num)	\
    ((type *) MMrealloc((char *) (obj), (long) sizeof(type) * (long) (num)))
#define FREE(obj)		\
    ((obj) ? (free((char *) (obj)), (obj) = 0) : 0)
#endif


/* Ultrix (and SABER) have 'fixed' certain functions which used to be int */
#if defined(ultrix) || defined(SABER)
#define VOID_HACK void
#else
#define VOID_HACK int
#endif


/* No machines seem to have much of a problem with these */
#include <stdio.h>
#include <ctype.h>


/* Some machines fail to define some functions in stdio.h */
#ifndef __STDC__
extern FILE *popen(), *tmpfile();
extern int pclose();
#ifndef clearerr		/* is a macro on many machines, but not all */
extern VOID_HACK clearerr();
#endif

/* this line causing compilation to fail; hacked out at Stanford...4/29/88
#ifndef rewind
extern VOID_HACK rewind();
#endif
*** end of hack */

#endif


/* most machines don't give us a header file for these */
#ifdef __STDC__
#include <stdlib.h>
#else
extern VOID_HACK abort(), free(), exit(), perror();
extern char *getenv(), *sprintf(), *malloc(), *realloc();
extern int system();
extern double atof();
#endif


/* some call it strings.h, some call it string.h; others, also have memory.h */
#ifdef __STDC__
#include <string.h>
#else
/* ANSI C string.h -- 1/11/88 Draft Standard */
extern char *strcpy(), *strncpy(), *strcat(), *strncat(), *strerror();
extern char *strpbrk(), *strtok(), *strchr(), *strrchr(), *strstr();
extern int strcoll(), strxfrm(), strncmp(), strlen(), strspn(), strcspn();
extern char *memmove(), *memccpy(), *memchr(), *memcpy(), *memset();
extern int memcmp(), strcmp();
#endif


#ifdef __STDC__
#include <assert.h>
#else
#ifndef NDEBUG
#define assert(ex) {\
    if (! (ex)) {\
	(void) fprintf(stderr,\
	    "Assertion failed: file %s, line %d\n\"%s\"\n",\
	    __FILE__, __LINE__, "ex");\
	(void) fflush(stdout);\
	abort();\
    }\
}
#else
#define assert(ex) ;
#endif
#endif


#define fail(why) {\
    (void) fprintf(stderr, "Fatal error: file %s, line %d\n%s\n",\
	__FILE__, __LINE__, why);\
    (void) fflush(stdout);\
    abort();\
}


#ifdef lint
#undef putc			/* correct lint '_flsbuf' bug */
#undef ALLOC			/* allow for lint -h flag */
#undef REALLOC
#define ALLOC(type, num)	(((type *) 0) + (num))
#define REALLOC(type, obj, num)	((obj) + (num))
#endif


/* These arguably do NOT belong in util.h */
#define ABS(a)			((a) < 0 ? -(a) : (a))
#define MAX(a,b)		((a) > (b) ? (a) : (b))
#define MIN(a,b)		((a) < (b) ? (a) : (b))


#ifndef USE_MM
extern char *MMalloc();
extern void MMout_of_memory();
extern void (*MMoutOfMemory)();
extern char *MMrealloc();
#endif

extern long util_cpu_time();
extern int util_getopt();
extern void util_getopt_reset();
extern char *util_path_search();
extern char *util_file_search();
extern int util_pipefork();
extern void util_print_cpu_stats();
extern char *util_print_time();
extern int util_save_image();
extern char *util_strsav();
extern char *util_tilde_expand();
extern void util_restart();


/* util_getopt() global variables (ack !) */
extern int util_optind;
extern char *util_optarg;

#endif
