
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

static struct {
	int nr_procs;
	pid_t *pid_list;

	useconds_t interval;

	char *output;
} option;

static FILE *out_file;

static volatile sig_atomic_t is_running = true;

static void
sigint_handler(int signum, siginfo_t *info, void *handler) {
	is_running = false;
}

static void
parse_args(int argc, char **argv) {
	pid_t pid;
	double interval;
	char c;

	option.nr_procs = 0;
	option.pid_list = NULL;
	option.output = NULL;
	option.interval = 1000000L;

	opterr = 0;
	while ((c = getopt(argc, argv, "p:o:i:")) != -1) {
		switch (c)
		{
		case 'p':
			option.nr_procs ++;
			option.pid_list = realloc(option.pid_list, sizeof(pid_t) * option.nr_procs);
			pid = atoi(optarg);
			option.pid_list[option.nr_procs - 1] = pid;
			break;
		case 'o':
			if (option.output != NULL)
				free(option.output);
			option.output = strdup(optarg);
			break;
		case 'i':
			interval = strtod(optarg, NULL);
			option.interval = interval * 1000000L; /* in microseconds */
			break;
		default:
			abort ();
		}
	}
}

static char *linebuf = NULL;

static void
loop(void) {
	struct timeval tv_now;
	long int now;
	long int next;
	char path[1024];

	if (option.output != NULL) {
		out_file = fopen(option.output, "w");
	} else {
		out_file = stdout;
		setbuf(stdout, NULL);
	}

	/* init time */
	gettimeofday(&tv_now, NULL);
	now = tv_now.tv_sec * 1000000L + tv_now.tv_usec;
	next = now;
	while(is_running) {
		char *lineptr = linebuf;
		int idx;
		int n;

		gettimeofday(&tv_now, NULL);
		now = tv_now.tv_sec * 1000000L + tv_now.tv_usec;
		n = sprintf(lineptr, "{\"time\":%lf,\"procs\":{", now / 1.0e6);
		lineptr += n;

		for (idx = 0; idx < option.nr_procs; idx++) {
			pid_t pid = option.pid_list[idx];
			FILE *f;
			char c;

			if (idx > 0)
				*(lineptr++) = ',';

			n = sprintf(lineptr, "\"%d\":{", pid);
			lineptr += n;

			/* io */
			n = sprintf(lineptr, "\"io\":");
			lineptr += n;
			sprintf(path, "/proc/%d/io", pid);
			f = fopen(path, "r");
			*(lineptr++) = '"';
			while ((c = fgetc(f)) != EOF) {
				if (c == '\n') {
					*(lineptr++) = '\\'; *(lineptr++) = 'n';
				} else {
					*(lineptr++) = c;
				}
			}
			fclose(f);
			*(lineptr++) = '"';

			/* stat */
			n = sprintf(lineptr, ",\"stat\":");
			lineptr += n;
			sprintf(path, "/proc/%d/stat", pid);
			f = fopen(path, "r");
			*(lineptr++) = '"';
			while ((c = fgetc(f)) != EOF) {
				if (c == '\n') {
					*(lineptr++) = '\\'; *(lineptr++) = 'n';
				} else {
					*(lineptr++) = c;
				}
			}
			fclose(f);
			*(lineptr++) = '"';

			*(lineptr++) = '}';
		}
		*(lineptr++) = '}';
		*(lineptr++) = '}';
		*(lineptr++) = '\0';

		fprintf(out_file, "%s\n", linebuf);

		next += option.interval;
		gettimeofday(&tv_now, NULL);
		now = tv_now.tv_sec * 1000000L + tv_now.tv_usec;
		if (next - now > 0)
			usleep(next - now);
	}

	if (option.output != NULL) {
		fclose(out_file);
	}
}

int
main(int argc, char **argv) {
    struct sigaction sigint_act;

	parse_args(argc, argv);

	if (option.nr_procs == 0) {
		fprintf(stderr, "ERROR: no pid given.\n");
		return EXIT_FAILURE;
	}

	linebuf = malloc(sizeof(char) * 1024L * 64L * option.nr_procs);

	/* init signal handlers */
	bzero(&sigint_act, sizeof(struct sigaction));
    sigint_act.sa_sigaction = sigint_handler;
    sigint_act.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigaction(SIGINT, &sigint_act, NULL) != 0) {
        perror("failed to set SIGINT handler");
        exit(EXIT_FAILURE);
    }

	if (sigaction(SIGTERM, &sigint_act, NULL) != 0) {
        perror("failed to set SIGTERM handler");
        exit(EXIT_FAILURE);
    }

	loop();

	return EXIT_SUCCESS;
}
