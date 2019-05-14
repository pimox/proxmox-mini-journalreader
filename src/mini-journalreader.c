/*

    Copyright (C) 2019 Proxmox Server Solutions GmbH

    Copyright: journalreader is under GNU GPL, the GNU General Public License.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 dated June, 1991.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
    02111-1307, USA.

    Author: Dominik Csapak <d.csapak@proxmox.com>

*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>

#define BUFSIZE 4096

static char buf[BUFSIZE+1];
static size_t offset = 0;

uint64_t convert_argument(const char *argument) {
    errno = 0;
    char * end;
    uint64_t value = strtoull(argument, &end, 10);
    if (errno != 0 || *end != '\0') {
	fprintf(stderr, "%s is not a valid number\n", argument);
	exit(1);
    }

    return value;
}

uint64_t get_timestamp(sd_journal *j) {
    uint64_t timestamp;
    int r = sd_journal_get_realtime_usec(j, &timestamp);
    if (r < 0) {
	fprintf(stderr, "Failed  %s\n", strerror(-r));
	return 0xFFFFFFFFFFFFFFFF;
    }
    return timestamp;
}

void print_to_buf(const char * string, uint32_t length) {
    if (!length) {
	return;
    }
    size_t string_offset = 0;
    size_t remaining = length;
    while (offset + remaining > BUFSIZE) {
	strncpy(buf+offset, string+string_offset, BUFSIZE-offset);
	string_offset += BUFSIZE-offset;
	remaining = length - string_offset;
	write (1, buf, BUFSIZE);
	offset = 0;
    }
    strncpy(buf+offset, string+string_offset, remaining);
    offset += remaining;
}

bool printed_first_cursor = false;

void print_cursor(sd_journal *j) {
    int r;
    char *cursor = NULL;
    r = sd_journal_get_cursor(j, &cursor);
    if (r < 0) {
	fprintf(stderr, "Failed to get cursor: %s\n", strerror(-r));
	exit(1);
    }
    print_to_buf(cursor, strlen(cursor));
    print_to_buf("\n", 1);
    free(cursor);
}

void print_first_cursor(sd_journal *j) {
    if (!printed_first_cursor) {
	print_cursor(j);
	printed_first_cursor = true;
    }
}

static uint64_t last_timestamp;
static char timestring[16];
static char bootid[32];

void print_reboot(sd_journal *j) {
    const char *d;
    size_t l;
    int r = sd_journal_get_data(j, "_BOOT_ID", (const void **)&d, &l);
    if (r < 0) {
	fprintf(stderr, "Failed  %s\n", strerror(-r));
	return;
    }

    // remove '_BOOT_ID='
    d += 9;
    l -= 9;

    if (bootid[0] != '\0') { // we have some bootid
	if (strncmp(bootid, d, l)) { // a new bootid found
	    strncpy(bootid, d, l);
	    print_to_buf("-- Reboot --\n", 13);
	}
    } else {
	    strncpy(bootid, d, l);
    }
}

void print_timestamp(sd_journal *j) {
    uint64_t timestamp;
    int r = sd_journal_get_realtime_usec(j, &timestamp);
    if (r < 0) {
	fprintf(stderr, "Failed  %s\n", strerror(-r));
	return;
    }

    if (timestamp >= (last_timestamp+(1000*1000))) {
	timestamp = timestamp / (1000*1000); // usec to sec
	struct tm time;
	localtime_r((time_t *)&timestamp, &time);
	strftime(timestring, 16, "%b %d %T", &time);
	last_timestamp = timestamp;
    }

    print_to_buf(timestring, 15);
}

void print_pid(sd_journal *j) {
    const char *d;
    size_t l;
    int r = sd_journal_get_data(j, "_PID", (const void **)&d, &l);
    if (r < 0) {
	// we sometimes have no pid
	return;
    }

    // remove '_PID='
    d += 5;
    l -= 5;

    print_to_buf("[", 1);
    print_to_buf(d, l);
    print_to_buf("]", 1);
}

bool print_field(sd_journal *j, const char *field) {
    const char *d;
    size_t l;
    int r = sd_journal_get_data(j, field, (const void **)&d, &l);
    if (r < 0) {
	// some fields do not exists
	return false;
    }

    int fieldlen = strlen(field)+1;
    d += fieldlen;
    l -= fieldlen;
    print_to_buf(d, l);
    return true;
}


void print_line(sd_journal *j) {
    print_reboot(j);
    print_timestamp(j);
    print_to_buf(" ", 1);
    print_field(j, "_HOSTNAME");
    print_to_buf(" ", 1);
    if (!print_field(j, "SYSLOG_IDENTIFIER") &&
	!print_field(j, "_COMM")) {
	print_to_buf("unknown", strlen("unknown") - 1);
    }
    print_pid(j);
    print_to_buf(": ", 2);
    print_field(j, "MESSAGE");
    print_to_buf("\n", 1);
}

void usage(char *progname) {
    fprintf(stderr, "usage: %s [OPTIONS]\n", progname);
    fprintf(stderr, "  -b begin\tbegin at this epoch\n");
    fprintf(stderr, "  -e end\tend at this epoch\n");
    fprintf(stderr, "  -d directory\tpath to journal directory\n");
    fprintf(stderr, "  -n number\tprint the last number entries\n");
    fprintf(stderr, "  -f from\tprint from this cursor\n");
    fprintf(stderr, "  -t to\tprint to this cursor\n");
    fprintf(stderr, "  -h \t\tthis help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "giving a range conflicts with -n\n");
    fprintf(stderr, "-b and -f conflict\n");
    fprintf(stderr, "-e and -t conflict\n");
}

int main(int argc, char *argv[]) {
    uint64_t number = 0;
    const char *directory = NULL;
    const char *startcursor = NULL;
    const char *endcursor = NULL;
    uint64_t begin = 0;
    uint64_t end = 0;
    char c;

    while ((c = getopt (argc, argv, "b:e:d:n:f:t:h")) != -1) {
	switch (c) {
	    case 'b':
		begin = convert_argument(optarg);
		begin = begin*1000*1000;
		break;
	    case 'e':
		end = convert_argument(optarg);
		end = end*1000*1000;
		break;
	    case 'd':
		directory = optarg;
		break;
	    case 'n':
		number = convert_argument(optarg);
		break;
	    case 'f':
		startcursor = optarg;
		break;
	    case 't':
		endcursor = optarg;
		break;
	    case 'h':
		usage(argv[0]);
		exit(0);
		break;
	    case '?':
	    default:
		usage(argv[0]);
		exit(1);
	}
    }

    if (number && (begin || startcursor)) {
	usage(argv[0]);
	exit(1);
    }

    if (begin && startcursor) {
	usage(argv[0]);
	exit(1);
    }

    if (end && endcursor) {
	usage(argv[0]);
	exit(1);
    }

    if (argc > optind) {
	usage(argv[0]);
	exit(1);
    }

    // to prevent calling it everytime we generate a timestamp
    tzset();

    int r;
    sd_journal *j;
    if (directory == NULL) {
	r = sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);
    } else {
	r = sd_journal_open_directory(&j, directory, 0);
    }

    if (r < 0) {
	fprintf(stderr, "Failed to open journal: %s\n", strerror(-r));
	return 1;
    }

    // if we want to print the last x entries, seek to cursor or end,
    // then x entries back, print the cursor and finally print the
    // entries until end or cursor
    if (number) {
	if (end) {
	    r = sd_journal_seek_realtime_usec(j, end);
	} else if (endcursor != NULL) {
	    r = sd_journal_seek_cursor(j, endcursor);
	    number++;
	} else {
	    r = sd_journal_seek_tail(j);
	}

	if (r < 0) {
	    fprintf(stderr, "Failed to seek to end/cursor: %s\n", strerror(-r));
	    exit(1);
	}

	// seek back number entries and print cursor
	r = sd_journal_previous_skip(j, number + 1);
	if (r < 0) {
	    fprintf(stderr, "Failed to seek back: %s\n", strerror(-r));
	    exit(1);
	}
    } else {
	if (begin) {
	    r = sd_journal_seek_realtime_usec(j, begin);
	} else if (startcursor) {
	    r = sd_journal_seek_cursor(j, startcursor);
	} else {
	    r = sd_journal_seek_head(j);
	}

	if (r < 0) {
	    fprintf(stderr, "Failed to seek to begin/cursor: %s\n", strerror(-r));
	    exit(1);
	}

	// if we have a start cursor, we want to skip the first entry
	if (startcursor) {
	    r = sd_journal_next(j);
	    if (r < 0) {
		fprintf(stderr, "Failed to seek to begin/cursor: %s\n", strerror(-r));
		exit(1);
	    }
	    print_first_cursor(j);
	}
    }


    while ((r = sd_journal_next(j)) > 0 && (end == 0 || get_timestamp(j) < end)) {
	print_first_cursor(j);
	if (endcursor != NULL && sd_journal_test_cursor(j, endcursor)) {
	    break;
	}
	print_line(j);
    }

    // print optional reboot
    print_reboot(j);

    // print final cursor
    print_cursor(j);

    // print remaining buffer
    write(1, buf, offset);
    sd_journal_close(j);

    return 0;
}
