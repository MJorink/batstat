/* batstat - show battery stats from /sys/class/power_supply */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef SYSFS
#define SYSFS "/sys/class/power_supply"
#endif
/* "%s" slots for the colour codes O and R */
#define K(k) "%s" k "%s="

struct bat {
	char status[32];
	double capacity, energy_now, full, design, cycles;
	double power, current, voltage;
	int has_power, has_cycles;
};

static int watch;
static FILE *out;
static const char *O = "\033[0;33m", *R = "\033[0m";

static void usage(const char *argv0, int code)
{
	const char *p = strrchr(argv0, '/');
	FILE *f = code ? stderr : stdout;
	fprintf(f, "usage: %s [-w [seconds]]\n", p ? p + 1 : argv0);
	fprintf(f, "  -w [seconds]  keep updating the stats (default interval: 2s)\n");
	exit(code);
}

/* first line of dir/name into buf; 0 on failure */
static int readline(const char *dir, const char *name, char *buf, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof path, SYSFS "/%s/%s", dir, name);
	if (!(f = fopen(path, "r")))
		return 0;
	if (!fgets(buf, n, f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = '\0';
	return 1;
}

static void print_threshold(const char *key, const char *dir, const char *name)
{
	char buf[32];
	if (!readline(dir, name, buf, sizeof buf))
		snprintf(buf, sizeof buf, "n/a");
	fprintf(out, "%s%s%s=%s\n", O, key, R, buf);
}

static void parse_uevent(const char *dir, struct bat *b)
{
	char path[512], line[256], *v;
	double cnow = 0, cfull = 0, cdesign = 0, vmin = 0;
	FILE *f;

	memset(b, 0, sizeof *b);
	snprintf(path, sizeof path, SYSFS "/%s/uevent", dir);
	if (!(f = fopen(path, "r")))
		return;
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, "POWER_SUPPLY_", 13) || !(v = strchr(line, '=')))
			continue;
		*v++ = '\0';
		v[strcspn(v, "\n")] = '\0';
		const char *k = line + 13;
		if (!strcmp(k, "STATUS"))                 snprintf(b->status, sizeof b->status, "%s", v);
		else if (!strcmp(k, "CAPACITY"))          b->capacity = atof(v);
		else if (!strcmp(k, "ENERGY_NOW"))        b->energy_now = atof(v);
		else if (!strcmp(k, "ENERGY_FULL"))       b->full = atof(v);
		else if (!strcmp(k, "ENERGY_FULL_DESIGN")) b->design = atof(v);
		else if (!strcmp(k, "CYCLE_COUNT"))       b->cycles = atof(v), b->has_cycles = 1;
		else if (!strcmp(k, "POWER_NOW"))         b->power = atof(v), b->has_power = 1;
		else if (!strcmp(k, "CURRENT_NOW"))       b->current = atof(v);
		else if (!strcmp(k, "VOLTAGE_NOW"))       b->voltage = atof(v);
		else if (!strcmp(k, "CHARGE_NOW"))        cnow = atof(v);
		else if (!strcmp(k, "CHARGE_FULL"))       cfull = atof(v);
		else if (!strcmp(k, "CHARGE_FULL_DESIGN")) cdesign = atof(v);
		else if (!strcmp(k, "VOLTAGE_MIN_DESIGN")) vmin = atof(v);
	}
	fclose(f);
	/* some drivers report charge (uAh) instead of energy (uWh): uAh * uV / 1e6 = uWh */
	if (!b->full && cfull) {
		double volt = vmin ? vmin : b->voltage;
		b->energy_now = cnow * volt / 1e6;
		b->full = cfull * volt / 1e6;
		b->design = cdesign * volt / 1e6;
		/* without a voltage the Wh above are 0: take the % from charge directly */
		if (!b->capacity)
			b->capacity = cnow * 100 / cfull;
	}
}

static void print_energy(double design, double full)
{
	double loss = design - full;
	fprintf(out, K("FULL_DESIGN") "%.2f Wh\n", O, R, design / 1e6);
	fprintf(out, K("FULL") "%.2f Wh\n", O, R, full / 1e6);
	fprintf(out, K("ENERGY_LOSS") "%.2f Wh (%.0f%%)\n", O, R, loss / 1e6,
	       design > 0 ? loss * 100 / design : 0);
}

static int show(void)
{
	struct dirent **ents;
	struct bat b, tot = {0};
	double draw = 0;
	int n, found = 0, discharging = 0;
	char type[32];

	if ((n = scandir(SYSFS, &ents, NULL, alphasort)) < 0)
		n = 0;
	for (int i = 0; i < n; i++) {
		const char *name = ents[i]->d_name;
		if (name[0] != '.' && readline(name, "type", type, sizeof type) &&
		    !strcmp(type, "Battery") &&
		    /* mice, controllers etc. are scope=Device: not system batteries */
		    !(readline(name, "scope", type, sizeof type) && !strcmp(type, "Device"))) {
			found++;
			parse_uevent(name, &b);
			fprintf(out, "\n%s:\n", name);
			/* some drivers omit CAPACITY: derive it like TOTAL does */
			fprintf(out, K("CAPACITY") "%.0f%%\n", O, R, b.capacity ? b.capacity :
			        b.full > 0 ? b.energy_now * 100 / b.full : 0);
			print_energy(b.design, b.full);
			if (b.has_cycles)
				fprintf(out, K("CYCLE_COUNT") "%d\n", O, R, (int)b.cycles);
			else
				fprintf(out, K("CYCLE_COUNT") "n/a\n", O, R);
			print_threshold("CHARGE_START", name, "charge_control_start_threshold");
			print_threshold("CHARGE_STOP", name, "charge_control_end_threshold");
			fprintf(out, "\n");

			tot.energy_now += b.energy_now;
			tot.full += b.full;
			tot.design += b.design;
			/* system draw = sum of discharging batteries; on AC, sysfs has no system draw */
			if (!strcmp(b.status, "Discharging")) {
				discharging++;
				/* some drivers report discharge as negative */
				double p = b.has_power ? b.power : b.current / 1e6 * b.voltage;
				draw += p < 0 ? -p : p;
			}
		}
		free(ents[i]);
	}
	if (n > 0)
		free(ents);

	if (!found) {
		fprintf(watch ? out : stderr, "no batteries found\n");
		return 0;
	}
	if (found < 2 && !watch)
		return found;

	fprintf(out, "\nTOTAL:\n");
	if (found > 1) {
		fprintf(out, K("CAPACITY") "%.0f%%\n", O, R, tot.full > 0 ? tot.energy_now * 100 / tot.full : 0);
		print_energy(tot.design, tot.full);
	}
	if (watch) {
		if (discharging)
			fprintf(out, K("POWER_USAGE") "%.2f W\n", O, R, draw / 1e6);
		else
			fprintf(out, K("POWER_USAGE") "n/a (not discharging)\n", O, R);
	}
	fprintf(out, "\n");
	return found;
}

static void restore_cursor(int sig)
{
	(void)sig;
	write(STDOUT_FILENO, "\033[?25h", 6);
	_exit(0);
}

static void on_tstp(int sig)
{
	(void)sig;
	write(STDOUT_FILENO, "\033[?25h", 6);
	signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP); /* stops once this handler returns */
}

static void on_cont(int sig)
{
	(void)sig;
	write(STDOUT_FILENO, "\033[?25l", 6);
	signal(SIGTSTP, on_tstp);
}

int main(int argc, char **argv)
{
	double interval = 2;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--watch")) {
			watch = 1;
			const char *a = argv[i + 1];
			if (a && *a && a[strspn(a, "0123456789.")] == '\0' &&
			    strchr(a, '.') == strrchr(a, '.')) {
				interval = atof(a);
				i++;
			}
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0], 0);
		} else {
			usage(argv[0], 1);
		}
	}

	/* tiny or huge values make nanosleep return at once: a busy loop */
	if (watch && (interval < 0.1 || interval > 86400))
		usage(argv[0], 1);
	int tty = isatty(STDOUT_FILENO);
	if (!tty)
		O = R = "";

	out = stdout;
	if (!watch)
		return show() ? 0 : 1;

	if (tty) {
		signal(SIGINT, restore_cursor);
		signal(SIGTERM, restore_cursor);
		signal(SIGQUIT, restore_cursor);
		signal(SIGHUP, restore_cursor);
		signal(SIGTSTP, on_tstp);
		signal(SIGCONT, on_cont);
		fputs("\033[?25l\033[2J", stdout); /* hide cursor, clear once */
	}

	struct timespec ts = { (time_t)interval, (long)((interval - (time_t)interval) * 1e9) };
	for (;;) {
		/* render off-screen, then overwrite in place: a full clear each tick flickers */
		char *buf;
		size_t len;
		if (!(out = open_memstream(&buf, &len))) {
			if (tty)
				fputs("\033[?25h", stdout);
			return 1;
		}
		show();
		fclose(out);
		if (!tty) { /* piped: plain output, no terminal codes */
			fputs(buf, stdout);
		} else {
			fputs("\033[H", stdout);
			for (char *l = buf, *nl; *l; l = nl + 1) {
				if (!(nl = strchr(l, '\n'))) {
					fputs(l, stdout);
					break;
				}
				fwrite(l, 1, nl - l, stdout);
				fputs("\033[K\n", stdout); /* erase leftovers of a longer previous line */
			}
			fputs("\033[J", stdout); /* erase leftover lines below */
		}
		fflush(stdout);
		free(buf);
		nanosleep(&ts, NULL);
	}
}
