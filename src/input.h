#pragma once
#include <stdbool.h>
#include <linux/input-event-codes.h>

struct input_opts {
    bool grab_key[KEY_MAX];
};

bool input_init(const struct input_opts *opts);
bool input_is_grabbed(void);

