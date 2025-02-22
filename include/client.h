#ifndef CLIENT_H
#define CLIENT_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 1024
#define CMD_NOT_FOUND 127

static volatile sig_atomic_t exit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void setup_signal_handler(void);
static void sigint_handler(int signum);

#endif    // CLIENT_H
