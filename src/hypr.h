#ifndef AQUARIUM_HYPR_H
#define AQUARIUM_HYPR_H

/* Open Hyprland's event stream (.socket2.sock), non-blocking.
 * Returns a readable fd, or -1 when Hyprland is not around. */
int hypr_events_open(void);

/* Consume everything pending on the event fd.
 * Returns 1 if an event that could change occlusion went past, 0 otherwise,
 * -1 if the connection died. */
int hypr_events_drain(int fd);

/* Ask whether the focused monitor's active workspace holds a fullscreen
 * window. Returns 1 yes, 0 no, -1 if the query failed. */
int hypr_has_fullscreen(void);

#endif
