/* LINTLIBRARY */

#include <stdio.h>
#include "util.h"


#ifdef BSD
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

extern int end, etext, edata;

#endif

void
util_print_cpu_stats(fp)
FILE *fp;
{
#ifdef BSD
    struct rusage rusage;
    struct rlimit rlp;
    int text, data, vm_limit, vm_soft_limit;
    double user, system, scale;
    char hostname[257];
    caddr_t sbrk();
    int vm_text, vm_init_data, vm_uninit_data, vm_sbrk_data;

    /* Get the hostname */
    (void) gethostname(hostname, 256);
    hostname[256] = '\0';		/* just in case */

    /* Get the virtual memory sizes */
    vm_text = ((int) (&etext)) / 1024.0 + 0.5;
    vm_init_data = ((int) (&edata) - (int) (&etext)) / 1024.0 + 0.5;
    vm_uninit_data = ((int) (&end) - (int) (&edata)) / 1024.0 + 0.5;
    vm_sbrk_data = ((int) sbrk(0) - (int) (&end)) / 1024.0 + 0.5;

    /* Get virtual memory limits */
    (void) getrlimit(RLIMIT_DATA, &rlp);
    vm_limit = rlp.rlim_max / 1024.0 + 0.5;
    vm_soft_limit = rlp.rlim_cur / 1024.0 + 0.5;

    /* Get usage stats */
    (void) getrusage(RUSAGE_SELF, &rusage);
    user = rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec/1.0e6;
    system = rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec/1.0e6;
    scale = (user + system)*100.0;
    if (scale == 0.0) scale = 0.001;

    (void) fprintf(fp, "Runtime Statistics\n");
    (void) fprintf(fp, "------------------\n");
    (void) fprintf(fp, "Machine name: %s\n", hostname);
    (void) fprintf(fp, "User time   %6.1f seconds\n", user);
    (void) fprintf(fp, "System time %6.1f seconds\n\n", system);

    text = rusage.ru_ixrss / scale + 0.5;
    data = (rusage.ru_idrss + rusage.ru_isrss) / scale + 0.5;
    (void) fprintf(fp, "Average resident text size       = %5dK\n", text);
    (void) fprintf(fp, "Average resident data+stack size = %5dK\n", data);
    (void) fprintf(fp, "Maximum resident size            = %5dK\n\n", 
	rusage.ru_maxrss/2);
    (void) fprintf(fp, "Virtual text size                = %5dK\n", 
	vm_text);
    (void) fprintf(fp, "Virtual data size                = %5dK\n", 
	vm_init_data + vm_uninit_data + vm_sbrk_data);
    (void) fprintf(fp, "    data size initialized        = %5dK\n", 
	vm_init_data);
    (void) fprintf(fp, "    data size uninitialized      = %5dK\n", 
	vm_uninit_data);
    (void) fprintf(fp, "    data size sbrk               = %5dK\n", 
	vm_sbrk_data);
    (void) fprintf(fp, "Virtual memory limit             = %5dK (%dK)\n\n", 
	vm_soft_limit, vm_limit);

    (void) fprintf(fp, "Major page faults = %d\n", rusage.ru_majflt);
    (void) fprintf(fp, "Minor page faults = %d\n", rusage.ru_minflt);
    (void) fprintf(fp, "Swaps = %d\n", rusage.ru_nswap);
    (void) fprintf(fp, "Input blocks = %d\n", rusage.ru_inblock);
    (void) fprintf(fp, "Output blocks = %d\n", rusage.ru_oublock);
    (void) fprintf(fp, "Context switch (voluntary) = %d\n", rusage.ru_nvcsw);
    (void) fprintf(fp, "Context switch (involuntary) = %d\n", rusage.ru_nivcsw);
#else
    (void) fprintf(fp, "Usage statistics not available\n");
#endif
}
