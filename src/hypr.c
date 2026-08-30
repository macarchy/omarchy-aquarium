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

int hypr_has_fullscreen(void) {
	int fd = hypr_connect(".socket.sock");
	if (fd < 0) return -1;

	static const char cmd[] = "j/activeworkspace";
	if (write(fd, cmd, sizeof(cmd) - 1) < 0) {
		close(fd);
		return -1;
	}

	char buf[8192];
	size_t off = 0;
	for (;;) {
		ssize_t n = read(fd, buf + off, sizeof(buf) - 1 - off);
		if (n <= 0) break;
		off += (size_t)n;
		if (off >= sizeof(buf) - 1) break;
	}
	close(fd);
	buf[off] = '\0';

	if (off == 0) return -1;
	if (strstr(buf, "\"hasfullscreen\": true") ||
	    strstr(buf, "\"hasfullscreen\":true"))
		return 1;
	return 0;
}
