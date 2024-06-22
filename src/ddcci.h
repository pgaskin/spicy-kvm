#pragma once
#include <stdint.h>

enum ddcci_error {
    ddcci_success,
    ddcci_err_invalid_argument,
    ddcci_err_device_gone,
    ddcci_err_checksum,
    ddcci_err_bad_reply,
    ddcci_err_no_reply,
    ddcci_err_unsupported_vcp,
    ddcci_err_short_read,
    ddcci_err_bad_i2c_src_addr,
    // negative values are errnos
};

struct ddcci {
    int fd;
    int tfd;
};

int ddcci_find_i2c(const char *card, int *i2c);

int ddcci_open(struct ddcci *ddc, int i2c);
int ddcci_vcp_set(struct ddcci *ddc, uint8_t vcp, uint16_t val);
int ddcci_close(struct ddcci *ddc);

const char *ddcci_strerror(int rc);
