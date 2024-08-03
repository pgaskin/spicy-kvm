/**
 * Copyright © 2024 Patrick Gaskin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <pthread.h>
#include <purespice.h>
#include <spice/enums.h>
#include <systemd/sd-device.h>
#include <systemd/sd-event.h>

#include "input.h"

static const uint32_t linux_to_ps2[KEY_MAX] = {
    // https://github.com/gnif/LookingGlass/blob/master/client/src/kb.c
   [KEY_RESERVED]         /* = USB   0 */ = 0x000000,
   [KEY_ESC]              /* = USB  41 */ = 0x000001,
   [KEY_1]                /* = USB  30 */ = 0x000002,
   [KEY_2]                /* = USB  31 */ = 0x000003,
   [KEY_3]                /* = USB  32 */ = 0x000004,
   [KEY_4]                /* = USB  33 */ = 0x000005,
   [KEY_5]                /* = USB  34 */ = 0x000006,
   [KEY_6]                /* = USB  35 */ = 0x000007,
   [KEY_7]                /* = USB  36 */ = 0x000008,
   [KEY_8]                /* = USB  37 */ = 0x000009,
   [KEY_9]                /* = USB  38 */ = 0x00000A,
   [KEY_0]                /* = USB  39 */ = 0x00000B,
   [KEY_MINUS]            /* = USB  45 */ = 0x00000C,
   [KEY_EQUAL]            /* = USB  46 */ = 0x00000D,
   [KEY_BACKSPACE]        /* = USB  42 */ = 0x00000E,
   [KEY_TAB]              /* = USB  43 */ = 0x00000F,
   [KEY_Q]                /* = USB  20 */ = 0x000010,
   [KEY_W]                /* = USB  26 */ = 0x000011,
   [KEY_E]                /* = USB   8 */ = 0x000012,
   [KEY_R]                /* = USB  21 */ = 0x000013,
   [KEY_T]                /* = USB  23 */ = 0x000014,
   [KEY_Y]                /* = USB  28 */ = 0x000015,
   [KEY_U]                /* = USB  24 */ = 0x000016,
   [KEY_I]                /* = USB  12 */ = 0x000017,
   [KEY_O]                /* = USB  18 */ = 0x000018,
   [KEY_P]                /* = USB  19 */ = 0x000019,
   [KEY_LEFTBRACE]        /* = USB  47 */ = 0x00001A,
   [KEY_RIGHTBRACE]       /* = USB  48 */ = 0x00001B,
   [KEY_ENTER]            /* = USB  40 */ = 0x00001C,
   [KEY_LEFTCTRL]         /* = USB 224 */ = 0x00001D,
   [KEY_A]                /* = USB   4 */ = 0x00001E,
   [KEY_S]                /* = USB  22 */ = 0x00001F,
   [KEY_D]                /* = USB   7 */ = 0x000020,
   [KEY_F]                /* = USB   9 */ = 0x000021,
   [KEY_G]                /* = USB  10 */ = 0x000022,
   [KEY_H]                /* = USB  11 */ = 0x000023,
   [KEY_J]                /* = USB  13 */ = 0x000024,
   [KEY_K]                /* = USB  14 */ = 0x000025,
   [KEY_L]                /* = USB  15 */ = 0x000026,
   [KEY_SEMICOLON]        /* = USB  51 */ = 0x000027,
   [KEY_APOSTROPHE]       /* = USB  52 */ = 0x000028,
   [KEY_GRAVE]            /* = USB  53 */ = 0x000029,
   [KEY_LEFTSHIFT]        /* = USB 225 */ = 0x00002A,
   [KEY_BACKSLASH]        /* = USB  49 */ = 0x00002B,
   [KEY_Z]                /* = USB  29 */ = 0x00002C,
   [KEY_X]                /* = USB  27 */ = 0x00002D,
   [KEY_C]                /* = USB   6 */ = 0x00002E,
   [KEY_V]                /* = USB  25 */ = 0x00002F,
   [KEY_B]                /* = USB   5 */ = 0x000030,
   [KEY_N]                /* = USB  17 */ = 0x000031,
   [KEY_M]                /* = USB  16 */ = 0x000032,
   [KEY_COMMA]            /* = USB  54 */ = 0x000033,
   [KEY_DOT]              /* = USB  55 */ = 0x000034,
   [KEY_SLASH]            /* = USB  56 */ = 0x000035,
   [KEY_RIGHTSHIFT]       /* = USB 229 */ = 0x000036,
   [KEY_KPASTERISK]       /* = USB  85 */ = 0x000037,
   [KEY_LEFTALT]          /* = USB 226 */ = 0x000038,
   [KEY_SPACE]            /* = USB  44 */ = 0x000039,
   [KEY_CAPSLOCK]         /* = USB  57 */ = 0x00003A,
   [KEY_F1]               /* = USB  58 */ = 0x00003B,
   [KEY_F2]               /* = USB  59 */ = 0x00003C,
   [KEY_F3]               /* = USB  60 */ = 0x00003D,
   [KEY_F4]               /* = USB  61 */ = 0x00003E,
   [KEY_F5]               /* = USB  62 */ = 0x00003F,
   [KEY_F6]               /* = USB  63 */ = 0x000040,
   [KEY_F7]               /* = USB  64 */ = 0x000041,
   [KEY_F8]               /* = USB  65 */ = 0x000042,
   [KEY_F9]               /* = USB  66 */ = 0x000043,
   [KEY_F10]              /* = USB  67 */ = 0x000044,
   [KEY_NUMLOCK]          /* = USB  83 */ = 0x000045,
   [KEY_SCROLLLOCK]       /* = USB  71 */ = 0x000046,
   [KEY_KP7]              /* = USB  95 */ = 0x000047,
   [KEY_KP8]              /* = USB  96 */ = 0x000048,
   [KEY_KP9]              /* = USB  97 */ = 0x000049,
   [KEY_KPMINUS]          /* = USB  86 */ = 0x00004A,
   [KEY_KP4]              /* = USB  92 */ = 0x00004B,
   [KEY_KP5]              /* = USB  93 */ = 0x00004C,
   [KEY_KP6]              /* = USB  94 */ = 0x00004D,
   [KEY_KPPLUS]           /* = USB  87 */ = 0x00004E,
   [KEY_KP1]              /* = USB  89 */ = 0x00004F,
   [KEY_KP2]              /* = USB  90 */ = 0x000050,
   [KEY_KP3]              /* = USB  91 */ = 0x000051,
   [KEY_KP0]              /* = USB  98 */ = 0x000052,
   [KEY_KPDOT]            /* = USB  99 */ = 0x000053,
   [KEY_102ND]            /* = USB 100 */ = 0x000056,
   [KEY_F11]              /* = USB  68 */ = 0x000057,
   [KEY_F12]              /* = USB  69 */ = 0x000058,
   [KEY_RO]               /* = USB 135 */ = 0x000073,
   [KEY_HENKAN]           /* = USB 138 */ = 0x000079,
   [KEY_KATAKANAHIRAGANA] /* = USB 136 */ = 0x000070,
   [KEY_MUHENKAN]         /* = USB 139 */ = 0x00007B,
   [KEY_KPENTER]          /* = USB  88 */ = 0x00E01C,
   [KEY_RIGHTCTRL]        /* = USB 228 */ = 0x00E01D,
   [KEY_KPSLASH]          /* = USB  84 */ = 0x00E035,
   [KEY_SYSRQ]            /* = USB  70 */ = 0x00E037,
   [KEY_RIGHTALT]         /* = USB 230 */ = 0x00E038,
   [KEY_HOME]             /* = USB  74 */ = 0x00E047,
   [KEY_UP]               /* = USB  82 */ = 0x00E048,
   [KEY_PAGEUP]           /* = USB  75 */ = 0x00E049,
   [KEY_LEFT]             /* = USB  80 */ = 0x00E04B,
   [KEY_RIGHT]            /* = USB  79 */ = 0x00E04D,
   [KEY_END]              /* = USB  77 */ = 0x00E04F,
   [KEY_DOWN]             /* = USB  81 */ = 0x00E050,
   [KEY_PAGEDOWN]         /* = USB  78 */ = 0x00E051,
   [KEY_INSERT]           /* = USB  73 */ = 0x00E052,
   [KEY_DELETE]           /* = USB  76 */ = 0x00E053,
   [KEY_KPEQUAL]          /* = USB 103 */ = 0x000059,
   [KEY_PAUSE]            /* = USB  72 */ = 0x00E046,
   [KEY_KPCOMMA]          /* = USB 133 */ = 0x00007E,
   [KEY_HANGEUL]          /* = USB 144 */ = 0x0000F2,
   [KEY_HANJA]            /* = USB 145 */ = 0x0000F1,
   [KEY_YEN]              /* = USB 137 */ = 0x00007D,
   [KEY_LEFTMETA]         /* = USB 227 */ = 0x00E05B,
   [KEY_RIGHTMETA]        /* = USB 231 */ = 0x00E05C,
   [KEY_COMPOSE]          /* = USB 101 */ = 0x00E05D,
   [KEY_F13]              /* = USB 104 */ = 0x00005D,
   [KEY_F14]              /* = USB 105 */ = 0x00005E,
   [KEY_F15]              /* = USB 106 */ = 0x00005F,
   [KEY_PRINT]            /* = USB  70 */ = 0x00E037,
   [KEY_MUTE]             /* = USB 127 */ = 0x00E020,
   [KEY_VOLUMEUP]         /* = USB 128 */ = 0x00E030,
   [KEY_VOLUMEDOWN]       /* = USB 129 */ = 0x00E02E,
   [KEY_NEXTSONG]         /* = USB 235 */ = 0x00E019,
   [KEY_PLAYPAUSE]        /* = USB 232 */ = 0x00E022,
   [KEY_PREVIOUSSONG]     /* = USB 234 */ = 0x00E010,
   [KEY_STOPCD]           /* = USB 233 */ = 0x00E024,
};

static const SpiceMouseButton linux_to_spice[KEY_MAX] = {
    [BTN_LEFT]    = SPICE_MOUSE_BUTTON_LEFT,
    [BTN_MIDDLE]  = SPICE_MOUSE_BUTTON_MIDDLE,
    [BTN_RIGHT]   = SPICE_MOUSE_BUTTON_RIGHT,
    [BTN_SIDE]    = SPICE_MOUSE_BUTTON_SIDE,
    [BTN_EXTRA]   = SPICE_MOUSE_BUTTON_EXTRA,
};

#define MAX_INPUT_DEVICES 64

static struct {
    sd_event *sd_event;
    sd_device_monitor *sd_device_monitor;
    sd_device_enumerator *sd_device_enumerator;

    struct libevdev *libevdev[MAX_INPUT_DEVICES];

    bool grab_key[KEY_MAX];
    ssize_t grabbed_keyboard;
    ssize_t grabbed_mouse;

    struct timeval grab_key_at;
    bool temp_ungrabbed_mouse;
} input = {
    .grab_key = {
        [KEY_RIGHTCTRL] = true,
    },
    .grabbed_keyboard = -1,
    .grabbed_mouse = -1,
};

// TODO: proper logging

static void input_ungrab(void) {
    int rc;
    if (input.grabbed_keyboard != -1) {
        if (input.libevdev[input.grabbed_keyboard]) {
            const char *name = libevdev_get_name(input.libevdev[input.grabbed_keyboard]) ?: "(no name)";
            printf("input: ungrabbing keyboard %s\n", name);
            if ((rc = libevdev_grab(input.libevdev[input.grabbed_keyboard], LIBEVDEV_UNGRAB)) < 0) {
                fprintf(stderr, "input: warning: failed to un-grab device %s\n", name);
            }
        }
        if (input.grabbed_mouse == input.grabbed_keyboard) {
            input.grabbed_mouse = -1;
        }
        input.grabbed_keyboard = -1;
    }
    if (input.grabbed_mouse != -1) {
        if (input.libevdev[input.grabbed_mouse]) {
            const char *name = libevdev_get_name(input.libevdev[input.grabbed_mouse]) ?: "(no name)";
            printf("input: ungrabbing mouse %s\n", name);
            if ((rc = libevdev_grab(input.libevdev[input.grabbed_mouse], LIBEVDEV_UNGRAB)) < 0) {
                fprintf(stderr, "input: warning: failed to un-grab device %s\n", name);
            }
        }
        input.grabbed_mouse = -1;
    }
}

static uint64_t tv_ms_diff(const struct timeval *a, const struct timeval *b) {
    uint64_t msa = (a->tv_sec * (uint64_t)(1000LL)) + (a->tv_usec / 1000);
    uint64_t msb = (b->tv_sec * (uint64_t)(1000LL)) + (b->tv_usec / 1000);
    return msb > msa ? msb - msa : msa - msb;
}

static void *input_device_thread(void *data) {
    int rc;
    int idx = *(int*)(&data);
    const char *name = libevdev_get_name(input.libevdev[idx]) ?: "(no name)";

    // set the thread name for easier debugging
    prctl(PR_SET_NAME, name); // can be seen in pstree -t

    // loop variables
    // note: if we send a mouse motion event out of range, spice will freeze up!?! (I noticed this when I accidentally forgot to initialize the state struct and the mouse offset was ridiculously high)
    struct {
        bool ok;
        int dx;
        int dy;
        int wheel;
    } pending_rel = {0};
    struct {
        bool ignore;
        bool x, y, touch; // have we seen initial values?
        int cx, cy;
        int dx, dy;
    } pending_rel_fake = {0};
    struct input_event ev;
    int sync = -1;

loop:
    // read an event
    if ((rc = libevdev_next_event(input.libevdev[idx], (sync == -1 ? LIBEVDEV_READ_FLAG_NORMAL : LIBEVDEV_READ_FLAG_SYNC)|LIBEVDEV_READ_FLAG_BLOCKING, &ev)) < 0) {
        if (rc == -EAGAIN) {
            if (sync != -1) {
                printf("input: synced %s in %d events\n", name, sync);
                sync = -1;
            }
            goto loop;
        }
        if (rc == -ENODEV) {
            printf("input: lost device %s\n", name);
        } else {
            printf("input: failed to read event from input device %s\n", name);
        }
        goto end;
    }

    // check if the buffer was overrun, resync input device state if it is
    if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        if (sync == -1) {
            printf("input: device dropped events, syncing %s\n", name);
        }
        sync++;
    }

    // check for the grab key
    if (ev.type == EV_KEY && input.grab_key[ev.code]) {
        // store the key down time
        if (ev.value == 1) {
            gettimeofday(&input.grab_key_at, NULL);

            // if this is from the grabbed keyboard, ungrab the mouse while the key is held
            if (input.grabbed_keyboard == idx && input.grabbed_mouse != -1 && input.libevdev[input.grabbed_mouse]) {
                const char *name = libevdev_get_name(input.libevdev[input.grabbed_mouse]) ?: "(no name)";
                printf("input: temporarily ungrabbing mouse %s while grab key is held\n", name);
                if ((rc = libevdev_grab(input.libevdev[input.grabbed_mouse], LIBEVDEV_UNGRAB)) < 0) {
                    fprintf(stderr, "input: warning: failed to un-grab device %s\n", name);
                }
                input.temp_ungrabbed_mouse = true;
            }
        }
        // on key up (so the key doesn't get stuck down)
        if (ev.value == 0) {
            struct timeval now;
            gettimeofday(&now, NULL);

            const char *name = libevdev_get_name(input.libevdev[idx]) ?: "(no name)";

            // if it was a short key press (or we don't know)
            if (input.grab_key_at.tv_sec == 0 || tv_ms_diff(&now, &input.grab_key_at) < 250) {
                fprintf(stdout, "input: handling grab key release from device %s\n", name);

                bool ungrab = input.grabbed_keyboard == idx;
                if (input.grabbed_keyboard != -1) {
                    fprintf(stdout, "input: ungrabbing everything\n");
                    input_ungrab();
                }
                if (!ungrab) {
                    if ((rc = libevdev_grab(input.libevdev[idx], LIBEVDEV_GRAB)) < 0) {
                        fprintf(stderr, "input: warning: failed to grab device %s\n", name);
                    } else {
                        fprintf(stdout, "input: grabbed device %s\n", name);
                        if (input.grabbed_keyboard != -1 || input.grabbed_mouse != -1) {
                            fprintf(stdout, "input: ungrabbing old inputs\n");
                            input_ungrab();
                        }
                        input.grabbed_keyboard = idx;
                    }
                }
            } else {
                fprintf(stdout, "input: ignoring grab key release from device %s\n", name);

                // re-grab if we temporarily ungrabbed the mouse
                if (input.temp_ungrabbed_mouse && input.grabbed_keyboard == idx && input.grabbed_mouse != -1 && input.libevdev[input.grabbed_mouse]) {
                    const char *name = libevdev_get_name(input.libevdev[input.grabbed_mouse]) ?: "(no name)";
                    printf("input: re-grabbing mouse %s since grab key was released after a long press\n", name);
                    if ((rc = libevdev_grab(input.libevdev[input.grabbed_mouse], LIBEVDEV_GRAB)) < 0) {
                        fprintf(stderr, "input: warning: failed to re-grab device %s\n", name);
                    }
                }
            }

            // clear the stored down time
            input.grab_key_at = (struct timeval){
                .tv_sec = 0,
                .tv_usec = 0,
            };
            input.temp_ungrabbed_mouse = false;
        }
        goto loop; // don't send the grab key to the spice server
    }

    // if we have a grabbed keyboard, but not a grabbed pointing device, check if it's a pointing device we can grab
    if (input.grabbed_keyboard != -1 && input.grabbed_mouse == -1) {
        if ((ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) || (ev.type == EV_ABS && (ev.code == ABS_X || ev.code == ABS_Y))) {
            const char *name = libevdev_get_name(input.libevdev[idx]) ?: "(no name)";
            if (idx == input.grabbed_keyboard) {
                fprintf(stdout, "input: got mouse movement from same devices as keyboard %s, assuming it's also a mouse\n", name);
                input.grabbed_mouse = input.grabbed_keyboard;
            } else {
                fprintf(stdout, "input: got mouse movement from %s, grabbing\n", name);
                if ((rc = libevdev_grab(input.libevdev[idx], LIBEVDEV_GRAB)) < 0) {
                    fprintf(stderr, "input: warning: failed to grab device %s\n", name);
                } else {
                    fprintf(stdout, "input: grabbed device %s\n", name);
                    input.grabbed_mouse = idx;
                }
            }
        }
        // fallthrough, send the mouse event
    }

    // if the device isn't grabbed, don't forward the event
    if (input.grabbed_keyboard != idx && input.grabbed_mouse != idx) {
        goto loop;
    }

    // if we're temporarily un-grabbing the mouse, don't handle events from the mouse
    if (input.temp_ungrabbed_mouse && input.grabbed_mouse == idx) {
        goto loop;
    }

    // handle key events
    if (ev.type == EV_KEY) {
        // send keys immediately rather than waiting for a report (spice sends each up/down as a single message anyways, and latency is noticeably better this way)
        // doing it this way also eliminates an odd bug where keys feel sticky due to missing events when many keys are pressed quickly
        if (linux_to_spice[ev.code]) {
            if (ev.value == 1) {
                if (!purespice_mousePress(linux_to_spice[ev.code])) {
                    fprintf(stderr, "input: warning: failed to send packet\n");
                }
            }
            if (ev.value == 0) {
                if (!purespice_mouseRelease(linux_to_spice[ev.code])) {
                    fprintf(stderr, "input: warning: failed to send packet\n");
                }
            }
        }
        if (linux_to_ps2[ev.code]) {
            if (ev.value == 1) {
                if (!purespice_keyDown(linux_to_ps2[ev.code])) {
                    fprintf(stderr, "input: warning: failed to send packet\n");
                }
            }
            if (ev.value == 0) {
                if (!purespice_keyUp(linux_to_ps2[ev.code])) {
                    fprintf(stderr, "input: warning: failed to send packet\n");
                }
            }
        }
        if (ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                pending_rel_fake.touch = true;
            }
            if (ev.value == 0) {
                pending_rel_fake.touch = pending_rel_fake.x = pending_rel_fake.y = false;
                pending_rel_fake.dx = pending_rel_fake.dy = 0;
            }
        }
    }

    // handle pointer reports
    if (ev.type == EV_REL) {
        pending_rel.ok = true;
        if (ev.code == REL_X) {
            pending_rel.dx += ev.value;
        }
        if (ev.code == REL_Y) {
            pending_rel.dy += ev.value;
        }
        if (ev.code == REL_WHEEL) {
            pending_rel.wheel += ev.value;
        }
        pending_rel_fake.ignore = true;
    }
    if (ev.type == EV_ABS) {
        if (ev.code == ABS_X) {
            if (pending_rel_fake.x) {
                pending_rel_fake.dx += ev.value - pending_rel_fake.cx;
            } else {
                pending_rel_fake.x = true;
                pending_rel_fake.dx = 0;
            }
            pending_rel_fake.cx = ev.value;
        }
        if (ev.code == ABS_Y) {
            if (pending_rel_fake.y) {
                pending_rel_fake.dy += ev.value - pending_rel_fake.cy;
            } else {
                pending_rel_fake.y = true;
                pending_rel_fake.dy = 0;
            }
            pending_rel_fake.cy = ev.value;
        }
    }
    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
        if (pending_rel.ok) {
            if (pending_rel.dx || pending_rel.dy) {
                if (!purespice_mouseMotion(pending_rel.dx, pending_rel.dy)) {
                    fprintf(stderr, "input: warning: failed to send packet\n");
                }
            }
            if (pending_rel.wheel < 0) {
                while (pending_rel.wheel++) {
                    if (!purespice_mousePress(SPICE_MOUSE_BUTTON_DOWN)) {
                        fprintf(stderr, "input: warning: failed to send packet\n");
                    }
                    if (!purespice_mouseRelease(SPICE_MOUSE_BUTTON_DOWN)) {
                        fprintf(stderr, "input: warning: failed to send packet\n");
                    }
                }
            } else if (pending_rel.wheel > 0) {
                while (pending_rel.wheel--) {
                    if (!purespice_mousePress(SPICE_MOUSE_BUTTON_UP)) {
                        fprintf(stderr, "input: warning: failed to send packet\n");
                    }
                    if (!purespice_mouseRelease(SPICE_MOUSE_BUTTON_UP)) {
                        fprintf(stderr, "input: warning: failed to send packet\n");
                    }
                }
            }
            pending_rel.ok = false;
            pending_rel.dx = pending_rel.dy = pending_rel.wheel = 0;
        }
        if (!pending_rel_fake.ignore) {
            if (pending_rel_fake.touch) {
                if (pending_rel_fake.dx || pending_rel_fake.dy) {
                    if (!purespice_mouseMotion(pending_rel_fake.dx, pending_rel_fake.dy)) {
                        fprintf(stderr, "input: warning: failed to send packet\n");
                    }
                }
                pending_rel_fake.dx = pending_rel_fake.dy = 0;
            }
        }
    }

    // continue reading events
    goto loop;

    // cleanup
end:
    printf("input: no longer tracking %s\n", libevdev_get_name(input.libevdev[idx]) ?: "(no name)");

    // close the input device (this also ungrabs it if it is grabbed)
    close(libevdev_get_fd(input.libevdev[idx]));
    libevdev_free(input.libevdev[idx]);
    input.libevdev[idx] = NULL;

    // ungrab everything if it was a grabbed device
    bool ungrab = false;
    if (input.grabbed_keyboard == idx) {
        input.grabbed_keyboard = -1;
        ungrab = true;
    }
    if (input.grabbed_mouse == idx) {
        input.grabbed_mouse = -1;
        ungrab = true;
    }
    if (ungrab) {
        fprintf(stdout, "input: untracked input was grabbed, so un-grabbing everything\n");
        input_ungrab();
    }

    // exit the thread
    return 0;
}

static int input_add_device(sd_device *device) {
    int rc;

    const char *devname;
    if ((rc = sd_device_get_devname(device, &devname)) < 0) {
        return rc;
    }
    if (strncmp(devname, "/dev/input/event", sizeof("/dev/input/event")-1)) {
        return 0;
    }
    // TODO: log
    printf("input: probing %s\n", devname);

    int idx = -1;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; i++) {
        if (!input.libevdev[i]) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        // TODO: log not enough room
        return -1;
    }

    int fd = open(devname, O_RDONLY, 0);
    if (fd < 0) {
        rc = -errno;
        // TODO: log errno
        return rc;
    }

    if ((rc = libevdev_new_from_fd(fd, &input.libevdev[idx])) < 0) {
        // TODO: log
        return rc;
    }

    const char *name = libevdev_get_name(input.libevdev[idx]);
    bool has_rel_x = libevdev_has_event_code(input.libevdev[idx], EV_REL, REL_X);
    bool has_rel_y = libevdev_has_event_code(input.libevdev[idx], EV_REL, REL_Y);
    bool has_abs_x = libevdev_has_event_code(input.libevdev[idx], EV_ABS, REL_X);
    bool has_abs_y = libevdev_has_event_code(input.libevdev[idx], EV_ABS, REL_Y);
    bool has_btn_touch = libevdev_has_event_code(input.libevdev[idx], EV_KEY, BTN_TOUCH);
    bool has_key = libevdev_has_event_type(input.libevdev[idx], EV_KEY);
    bool has_grab_key = false;

    for (int code = 0; code < KEY_MAX; code++) {
        if (input.grab_key[code] && libevdev_has_event_code(input.libevdev[idx], EV_KEY, code)) {
            has_grab_key = true;
            continue;
        }
    }

    bool is_supported_kbd = has_key && has_grab_key; // e.g., keyboard
    bool is_supported_pointer_rel = has_rel_x && has_rel_y; // e.g., mouse, trackball, trackpoint
    bool is_supported_pointer_fake_rel = !is_supported_pointer_rel && has_abs_x && has_abs_y && has_btn_touch; // e.g, drawing tablet, trackpad

    if (!is_supported_kbd && !is_supported_pointer_rel && !is_supported_pointer_fake_rel) {
        printf("input: ignoring %s\n", name ?: "(no name)");
        close(fd);
        libevdev_free(input.libevdev[idx]);
        input.libevdev[idx] = NULL;
        return 0;
    }

    printf("input: tracking %d %s (as keyboard=%s pointer=%s)\n",
        idx,
        name ?: "(no name)",
        is_supported_kbd ? "yes" : "no",
        (is_supported_pointer_rel|is_supported_pointer_fake_rel) ? (is_supported_pointer_fake_rel ? "fake_relative" : "relative") : "no");

    pthread_t input_thread;
    if ((rc = pthread_create(&input_thread, NULL, input_device_thread, (void*)(int64_t)idx))) {
        return -rc;
    }
    return 0;
}

static void *input_udev_thread(void *data) {
    int rc;
    prctl(PR_SET_NAME, "udev");
    if ((rc = sd_event_loop(input.sd_event)) < 1) {
        // TODO: log fatal error
    }
    return NULL;
}

static int input_udev_handler(sd_device_monitor *m, sd_device *device, void *userdata) {
    // note: returning an error from this will cause systemd to disable the handler
    int rc;
    sd_device_action_t action;
    if ((rc = sd_device_get_action(device, &action)) < 0) {
        return 0;
    }
    if (action == SD_DEVICE_ADD) {
        input_add_device(device);
    }
    return 0;
}

bool input_init(const struct input_opts *opts) {
    int rc;
    pthread_t udev_thread;
    if (opts) {
        for (int i = 0; i < KEY_MAX; i++) {
            input.grab_key[i] = opts->grab_key[i];
        }
    }
    if ((rc = sd_event_new(&input.sd_event)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_enumerator_new(&input.sd_device_enumerator) < 0)) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_enumerator_add_match_subsystem(input.sd_device_enumerator, "input", true) < 0)) {
        return false; // rc is -errno
    }
    for (sd_device *device = sd_device_enumerator_get_device_first(input.sd_device_enumerator); device; device = sd_device_enumerator_get_device_next(input.sd_device_enumerator)) {
        input_add_device(device);
    }
    if ((rc = sd_device_monitor_new(&input.sd_device_monitor)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_monitor_attach_event(input.sd_device_monitor, input.sd_event)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_monitor_set_description(input.sd_device_monitor, "spicy-kvm input")) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_monitor_filter_add_match_subsystem_devtype(input.sd_device_monitor, "input", NULL)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_monitor_filter_update(input.sd_device_monitor)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = sd_device_monitor_start(input.sd_device_monitor, input_udev_handler, NULL)) < 0) {
        return false; // rc is -errno
    }
    if ((rc = pthread_create(&udev_thread, NULL, input_udev_thread, NULL))) {
        return false; // rc is errno
    }
    return true;
}

bool input_is_grabbed(void) {
    return input.grabbed_keyboard != -1;
}
