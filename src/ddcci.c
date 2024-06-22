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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "ddcci.h"

#define ddcci_errno -errno

#define IOCTL_I2C_SLAVE 0x0703
#define I2C_ADDR_DDC_CI 0x37
#define I2C_ADDR_HOST   0x51

static const char *cut_prefix(const char *s, const char *pfx) {
    while (*pfx) {
        if (*pfx++ != *s++) {
            return NULL;
        }
    }
    return s;
}

int ddcci_find_i2c(const char *card, int *i2c) {
    char fn[PATH_MAX];
    DIR *dp;
    struct dirent *de;

    if (snprintf(fn, sizeof(fn), "/sys/class/drm/%s/", card) == -1) {
        return ddcci_errno;
    }
    if ((dp = opendir(fn)) == NULL) {
        return ddcci_errno;
    }
    for (errno = 0; (de = readdir(dp)); errno = 0) {
        const char *s = cut_prefix(de->d_name, "i2c-");
        if (s) {
            *i2c = atoi(s); // TODO: proper error checking
            return ddcci_success;
        }
    }
    closedir(dp);

    if (snprintf(fn, sizeof(fn), "/sys/class/drm/%s/ddc/i2c-dev", card) == -1) {
        return ddcci_errno;
    }
    if ((dp = opendir(fn)) == NULL) {
        if (errno == ENOENT) {
            errno = ENODEV;
        }
        return ddcci_errno;
    }
    for (errno = 0; (de = readdir(dp)); errno = 0) {
        const char *s = cut_prefix(de->d_name, "i2c-");
        if (s) {
            *i2c = atoi(s); // TODO: proper error checking
            return ddcci_success;
        }
    }
    closedir(dp);

    errno = ENODEV;
    return ddcci_errno;
}

static int ddcci_wait(struct ddcci *ddc) {
    uint64_t x;
    while (1) {
        if (read(ddc->tfd, &x, sizeof(x)) == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return ddcci_errno;
        }
        return ddcci_success;
    }
}

static int ddcci_wait_set(struct ddcci *ddc, int ms) {
    struct itimerspec ts = (struct itimerspec) {
        .it_value.tv_nsec = ((uint64_t)(ms) * 1000000) % 1000000000,
        .it_value.tv_sec = ((uint64_t)(ms) * 1000000) / 1000000000,
        .it_interval.tv_nsec = 100000,
    };
    if (timerfd_settime(ddc->tfd, 0, &ts, NULL) == -1) {
        return ddcci_errno;
    }
    return ddcci_success;
}

int ddcci_open(struct ddcci *ddc, int i2c) {
    ddc->fd = -1;
    ddc->tfd = -1;

    char fn[64];
    if (snprintf(fn, sizeof(fn), "/dev/i2c-%d", i2c) == -1) {
        return ddcci_err_invalid_argument;
    }

    if ((ddc->fd = open(fn, O_RDWR, 0)) == -1) {
        ddcci_close(ddc);
        return ddcci_errno;
    }

    if (ioctl(ddc->fd, IOCTL_I2C_SLAVE, I2C_ADDR_DDC_CI) == -1) {
        ddcci_close(ddc);
        return ddcci_errno;
    }

    if ((ddc->tfd = timerfd_create(CLOCK_MONOTONIC, 0)) == -1) {
        ddcci_close(ddc);
        return ddcci_errno;
    }

    int rc;
    if ((rc = ddcci_wait_set(ddc, 1))) {
        ddcci_close(ddc);
        return ddcci_errno;
    }

    ddcci_wait(ddc);

    return ddcci_success;
}

static int ddcci_tx(struct ddcci *ddc, const uint8_t *cmd, size_t cmd_len, int wait_ms) {
    // https://glenwing.github.io/docs/VESA-DDCCI-1.1.pdf
    int rc;
    uint8_t buf[256]; // much larger than required
    size_t buf_len = 0;
    if (cmd_len > 0xFF) {
        return ddcci_err_invalid_argument;
    }

    // rate-limit commands
    if ((rc = ddcci_wait(ddc))) {
        return rc;
    }

    // header (excluding slave address)
    buf[buf_len++] = I2C_ADDR_HOST;
    buf[buf_len++] = 0x80 | (uint8_t)(cmd_len);

    // command
    memcpy(buf + buf_len, cmd, cmd_len);
    buf_len += cmd_len;

    // checksum
    buf[buf_len++] = I2C_ADDR_DDC_CI << 1;
    for (size_t i = 0, ck = buf_len-1; i < ck; i++) {
        buf[ck] ^= buf[i];
    }

    // send
    if (write(ddc->fd, buf, buf_len) == -1) {
        return ddcci_errno;
    }

    // next delay
    if ((rc = ddcci_wait_set(ddc, wait_ms))) {
        return rc;
    }

    return ddcci_success;
}

/*
static int ddcci_rx(struct ddcci *ddc, uint8_t *buf, size_t *buf_len) {
    // https://glenwing.github.io/docs/VESA-DDCCI-1.1.pdf
    int rc;

    // rate-limit commands
    if ((rc = ddcci_wait(ddc))) {
        return rc;
    }

    // read header
    uint8_t hdr[2]; // much larger than required
    if ((rc = read(ddc->fd, hdr, sizeof(hdr))) != sizeof(hdr)) {
        return rc < 0 ? ddcci_errno : ddcci_err_short_read;
    }

    // check source
    uint8_t hdrAddr = hdr[0] >> 1;
    uint8_t pktLen = hdr[1] &~ 0x80;
    if (hdrAddr == 0) {
        return ddcci_err_no_reply;
    }
    if (hdrAddr != I2C_ADDR_DDC_CI) {
        return ddcci_err_bad_i2c_src_addr;
    }
    if ((hdr[1] & 0x80) == 0) {
        return ddcci_err_bad_reply;
    }

    // read payload
    uint8_t pkt[256]; // much larger than required
    if ((rc = read(ddc->fd, pkt, pktLen+1)) != pktLen+1) {
        return rc < 0 ? ddcci_errno : ddcci_err_short_read;
    }

    // checksum
    pkt[pktLen] ^= (I2C_ADDR_HOST - 1);
    for (size_t i = 0; i < sizeof(hdr); i++) {
        pkt[pktLen] ^= hdr[i];
    }
    for (size_t i = 0; i < pktLen; i++) {
        pkt[pktLen] ^= pkt[i];
    }
    if (pkt[pktLen] != 0) {
        return ddcci_err_checksum;
    }

    // copy
    if (pktLen > *buf_len) {
        return ddcci_err_invalid_argument;
    }
    *buf_len = pktLen;
    memcpy(buf, pkt, pktLen);

    return ddcci_success;
}

int ddcci_vcp_get(struct ddcci *ddc, uint8_t vcp, uint16_t *val_out, uint16_t *max_out) {
    return ddcci_err_invalid_argument; // TODO
}
*/

int ddcci_vcp_set(struct ddcci *ddc, uint8_t vcp, uint16_t val) {
	// https://glenwing.github.io/docs/VESA-DDCCI-1.1.pdf page 20
    uint8_t cmd[4] = {0x03, vcp, (val >> 8) & 0xFF, val & 0xFF};
    return ddcci_tx(ddc, cmd, sizeof(cmd), 50);
}

int ddcci_close(struct ddcci *ddc) {
    if (ddc->fd != -1) close(ddc->fd);
    if (ddc->tfd != -1) close(ddc->tfd);

    ddc->fd = 0;
    ddc->tfd = 0;

    return ddcci_success;
}

const char *ddcci_strerror(int rc) {
    if (rc < 0) {
        return strerror(-rc);
    }
    switch (rc) {
    case ddcci_success:
        return "ddc: success";
    case ddcci_err_invalid_argument:
        return "ddc: invalid argument";
    case ddcci_err_device_gone:
        return "ddc: device gone";
    case ddcci_err_checksum:
        return "ddc: invalid checksum";
    case ddcci_err_bad_reply:
        return "ddc: bad reply";
    case ddcci_err_no_reply:
        return "ddc: no reply";
    case ddcci_err_unsupported_vcp:
        return "ddc: unsupported vcp";
    case ddcci_err_short_read:
        return "ddc: short read";
    case ddcci_err_bad_i2c_src_addr:
        return "ddc: bad i2c read source address";
    }
    return "unknown error";
}
