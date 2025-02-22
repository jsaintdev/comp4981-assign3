#ifndef CLIENT_H
#define CLIENT_H

#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_INPUT 1024
#define CMD_NOT_FOUND 127

static volatile sig_atomic_t exit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void setup_signal_handler(void);
static void sigint_handler(int signum);

#endif    // CLIENT_H
