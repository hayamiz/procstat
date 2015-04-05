#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>


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
print_help(void) {
	puts("Usage: procstat [ -h ] [ -p PID ] [ -o FILE ] [ -s SEC ]");
	puts("  -p PID	Process ID of target.");
	puts("  -o FILE	Output file path. (default: standard output)");
	puts("  -i SEC	Recording interval. (default: 1.0)");
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
	while ((c = getopt(argc, argv, "p:o:i:h")) != -1) {
		switch (c)
		{
		case 'p':
			option.nr_procs++;
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
		case 'h':
			print_help();
			exit (1);
		default:
			print_help();
			errx(1, "Wrong usage: %s ", argv[1]);
		}
	}
}

static char *linebuf = NULL;
static char path[1024];

static void
loop(void) {
	struct timeval tv_now;
	long int now;
	long int next;
	int idx;
	char **io_path_list;
	char **stat_path_list;

	io_path_list = malloc(sizeof(char *) * option.nr_procs);
	stat_path_list = malloc(sizeof(char *) * option.nr_procs);

	for (idx = 0; idx < option.nr_procs; idx++) {
		pid_t pid = option.pid_list[idx];

		sprintf(path, "/proc/%d/io", pid);
		io_path_list[idx] = strdup(path);
		sprintf(path, "/proc/%d/stat", pid);
		stat_path_list[idx] = strdup(path);
	}

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
		int n;
		int nr_live_procs = 0;

		gettimeofday(&tv_now, NULL);
		now = tv_now.tv_sec * 1000000L + tv_now.tv_usec;
		n = sprintf(lineptr, "{\"time\":%lf,\"procs\":{", now / 1.0e6);
		lineptr += n;

		for (idx = 0; idx < option.nr_procs; idx++) {
			pid_t pid = option.pid_list[idx];
			FILE *io_file, *stat_file;
			char c;

			/* pid_list[idx] is set to 0 when process termination is detected. */
			if (pid == 0)
				continue;

			io_file = fopen(io_path_list[idx], "r");
			stat_file = fopen(stat_path_list[idx], "r");

			if (io_file == NULL || stat_file == NULL) {
				if (io_file != NULL) fclose(io_file);
				if (stat_file != NULL) fclose(stat_file);
				option.pid_list[idx] = 0;
				continue;
			}

			if (nr_live_procs > 0)
				*(lineptr++) = ',';

			n = sprintf(lineptr, "\"%d\":{", pid);
			lineptr += n;

			/* io */
			n = sprintf(lineptr, "\"io\":");
			lineptr += n;
			*(lineptr++) = '"';
			while ((c = fgetc(io_file)) != EOF) {
				if (c == '\n') {
					*(lineptr++) = '\\'; *(lineptr++) = 'n';
				} else {
					*(lineptr++) = c;
				}
			}
			fclose(io_file);
			*(lineptr++) = '"';

			/* stat */
			n = sprintf(lineptr, ",\"stat\":");
			lineptr += n;
			*(lineptr++) = '"';
			while ((c = fgetc(stat_file)) != EOF) {
				if (c == '\n') {
					*(lineptr++) = '\\'; *(lineptr++) = 'n';
				} else {
					*(lineptr++) = c;
				}
			}
			fclose(stat_file);
			*(lineptr++) = '"';

			*(lineptr++) = '}';

			nr_live_procs++;
		}
		*(lineptr++) = '}';
		*(lineptr++) = '}';
		*(lineptr++) = '\0';

		fprintf(out_file, "%s\n", linebuf);

		if (nr_live_procs == 0)
			break;

		next += option.interval;
		gettimeofday(&tv_now, NULL);
		now = tv_now.tv_sec * 1000000L + tv_now.tv_usec;
		if (next - now > 0)
			usleep(next - now);
	}

	if (option.output != NULL) {
		fclose(out_file);
	}

	for (idx = 0; idx < option.nr_procs; idx++) {
		free(io_path_list[idx]);
		free(stat_path_list[idx]);
	}
}

int
main(int argc, char **argv) {
	struct sigaction sigint_act;
	parse_args(argc, argv);

	if (option.nr_procs == 0) {
		fprintf(stderr, "ERROR: no pid given.\n");
		print_help();
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

	free(linebuf);

	return EXIT_SUCCESS;
}
