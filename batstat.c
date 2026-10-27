/* batstat - show battery stats from /sys/class/power_supply */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SYSFS "/sys/class/power_supply"
#define O "\033[0;33m"
#define R "\033[0m"

struct bat {
	char status[32];
	double capacity, energy_now, full, design, cycles;
	double power, current, voltage;
	int has_power;
};

static int watch;
static FILE *out;

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
	if (!fgets(buf, n, f))
		buf[0] = '\0';
	fclose(f);
	buf[strcspn(buf, "\n")] = '\0';
	return 1;
}

static long readnum(const char *dir, const char *name)
{
	char buf[32];
	return readline(dir, name, buf, sizeof buf) ? atol(buf) : 0;
}

static void parse_uevent(const char *dir, struct bat *b)
{
	char path[512], line[256], *v;
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
		else if (!strcmp(k, "CYCLE_COUNT"))       b->cycles = atof(v);
		else if (!strcmp(k, "POWER_NOW"))         b->power = atof(v), b->has_power = 1;
		else if (!strcmp(k, "CURRENT_NOW"))       b->current = atof(v);
		else if (!strcmp(k, "VOLTAGE_NOW"))       b->voltage = atof(v);
	}
	fclose(f);
}

static void print_energy(double design, double full)
{
	double loss = design - full;
	fprintf(out, O "FULL_DESIGN" R "=%.2f Wh\n", design / 1e6);
	fprintf(out, O "FULL" R "=%.2f Wh\n", full / 1e6);
	fprintf(out, O "ENERGY_LOSS" R "=%.2f Wh (%.0f%%)\n", loss / 1e6,
	       design > 0 ? loss * 100 / design : 0);
}

static void show(void)
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
		    !strcmp(type, "Battery")) {
			found++;
			parse_uevent(name, &b);
			fprintf(out, "\n%s:\n", name);
			fprintf(out, O "CAPACITY" R "=%.0f%%\n", b.capacity);
			print_energy(b.design, b.full);
			fprintf(out, O "CYCLE_COUNT" R "=%d\n", (int)b.cycles);
			fprintf(out, O "CHARGE_START" R "=%ld\n", readnum(name, "charge_start_threshold"));
			fprintf(out, O "CHARGE_STOP" R "=%ld\n\n", readnum(name, "charge_stop_threshold"));

			tot.energy_now += b.energy_now;
			tot.full += b.full;
			tot.design += b.design;
			tot.cycles += b.cycles;
			/* system draw = sum of discharging batteries; on AC, sysfs has no system draw */
			if (!strcmp(b.status, "Discharging")) {
				discharging++;
				draw += b.has_power ? b.power : b.current / 1e6 * b.voltage;
			}
		}
		free(ents[i]);
	}
	free(n ? ents : NULL);

	if (!found) {
		fprintf(watch ? out : stderr, "no batteries found\n");
		return;
	}
	if (found < 2 && !watch)
		return;

	fprintf(out, "\nTOTAL:\n");
	if (found > 1) {
		fprintf(out, O "CAPACITY" R "=%.0f%%\n", tot.full > 0 ? tot.energy_now * 100 / tot.full : 0);
		print_energy(tot.design, tot.full);
		fprintf(out, O "CYCLE_COUNT" R "=%d\n", (int)tot.cycles);
	}
	if (watch) {
		if (discharging)
			fprintf(out, O "POWER_USAGE" R "=%.2f W\n", draw / 1e6);
		else
			fprintf(out, O "POWER_USAGE" R "=n/a (not discharging)\n");
	}
	fprintf(out, "\n");
}

static void restore_cursor(int sig)
{
	(void)sig;
	write(STDOUT_FILENO, "\033[?25h", 6);
	_exit(0);
}

int main(int argc, char **argv)
{
	double interval = 2;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--watch")) {
			watch = 1;
			const char *a = argv[i + 1];
			if (a && *a && a[strspn(a, "0123456789.")] == '\0') {
				interval = atof(a);
				i++;
			}
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0], 0);
		} else {
			usage(argv[0], 1);
		}
	}

	out = stdout;
	if (!watch) {
		show();
		return 0;
	}

	signal(SIGINT, restore_cursor);
	signal(SIGTERM, restore_cursor);
	fputs("\033[?25l\033[2J", stdout); /* hide cursor, clear once */

	struct timespec ts = { (time_t)interval, (long)((interval - (time_t)interval) * 1e9) };
	for (;;) {
		/* render off-screen, then overwrite in place: a full clear each tick flickers */
		char *buf;
		size_t len;
		if (!(out = open_memstream(&buf, &len)))
			return 1;
		show();
		fclose(out);
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
		fflush(stdout);
		free(buf);
		nanosleep(&ts, NULL);
	}
}
