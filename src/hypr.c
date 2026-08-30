// Just enough Hyprland IPC to know when a fullscreen window is covering the
// desktop. Wayland has no occlusion signal for layer surfaces, and Hyprland
// keeps sending frame callbacks to a background layer that nobody can see, so
// without this the aquarium renders at full rate behind a fullscreen window.

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hypr.h"

static int hypr_connect(const char *which) {
	const char *rt = getenv("XDG_RUNTIME_DIR");
	const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (!rt || !sig) return -1;

	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	int n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/hypr/%s/%s",
	                 rt, sig, which);
	if (n < 0 || (size_t)n >= sizeof(addr.sun_path)) return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int hypr_events_open(void) {
	int fd = hypr_connect(".socket2.sock");
	if (fd < 0) return -1;
	int fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	return fd;
}

int hypr_events_drain(int fd) {
	char buf[8192];
	int interesting = 0;
	int closed = 0;
	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		if (n == 0) { closed = 1; break; }
		if (n < 0) break;                       /* EAGAIN: drained */
		buf[n] = '\0';
		/* Anything that can put a fullscreen window in front of us, or take
		 * one away. Cheap to over-trigger: the query behind it is local. */
		if (strstr(buf, "fullscreen>>") || strstr(buf, "workspace>>") ||
		    strstr(buf, "focusedmon") || strstr(buf, "openwindow>>") ||
		    strstr(buf, "closewindow>>") || strstr(buf, "monitor"))
			interesting = 1;
		if ((size_t)n < sizeof(buf) - 1) break;
	}
	if (closed) return -1;
	return interesting;
}

/* One request on the command socket, response into buf. Returns the length,
 * or -1 (buf is always NUL-terminated on success). */
static ssize_t hypr_query(const char *cmd, char *buf, size_t len) {
	int fd = hypr_connect(".socket.sock");
	if (fd < 0) return -1;

	if (write(fd, cmd, strlen(cmd)) < 0) {
		close(fd);
		return -1;
	}

	size_t off = 0;
	for (;;) {
		ssize_t n = read(fd, buf + off, len - 1 - off);
		if (n <= 0) break;
		off += (size_t)n;
		if (off >= len - 1) break;
	}
	close(fd);
	buf[off] = '\0';
	return off > 0 ? (ssize_t)off : -1;
}

int hypr_has_fullscreen(void) {
	char buf[8192];
	if (hypr_query("j/activeworkspace", buf, sizeof(buf)) < 0) return -1;
	if (strstr(buf, "\"hasfullscreen\": true") ||
	    strstr(buf, "\"hasfullscreen\":true"))
		return 1;
	return 0;
}

int hypr_cursorpos(int *x, int *y) {
	char buf[256];
	if (hypr_query("j/cursorpos", buf, sizeof(buf)) < 0) return -1;
	const char *px = strstr(buf, "\"x\":");
	const char *py = strstr(buf, "\"y\":");
	if (!px || !py) return -1;
	*x = atoi(px + 4);
	*y = atoi(py + 4);
	return 0;
}

/* The logical position of a named monitor in the global layout — what turns
 * a global cursor position into an output-local one. Single-monitor setups
 * get (0,0) either way; this matters when a second display hangs off USB-C. */
int hypr_monitor_origin(const char *name, int *x, int *y) {
	char buf[16384];
	if (hypr_query("j/monitors", buf, sizeof(buf)) < 0) return -1;

	char key[80];
	snprintf(key, sizeof(key), "\"name\": \"%s\"", name);
	const char *m = strstr(buf, key);
	if (!m) {
		snprintf(key, sizeof(key), "\"name\":\"%s\"", name);
		m = strstr(buf, key);
	}
	if (!m) return -1;
	const char *px = strstr(m, "\"x\":");
	const char *py = strstr(m, "\"y\":");
	if (!px || !py) return -1;
	*x = atoi(px + 4);
	*y = atoi(py + 4);
	return 0;
}
