#ifndef SETUP_H
#define SETUP_H

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define EXIT_CODE 1

void           parse_arguments(int argc, char *argv[], char **ip_address, char **port);
void           handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port);
void           convert_address(const char *address, struct sockaddr_storage *addr);
int            socket_create(int domain, int type, int protocol);
void           socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);

#endif    // SETUP_H
