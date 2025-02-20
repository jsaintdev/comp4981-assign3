#ifndef SETUP_H
#define SETUP_H

#include <netinet/in.h>
#include <stdio.h>

_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void           parse_arguments(int argc, char *argv[], char **ip_address, char **port);
static void           handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port);
static in_port_t      parse_in_port_t(const char *binary_name, const char *port_str);
static void           convert_address(const char *address, struct sockaddr_storage *addr);
static int            socket_create(int domain, int type, int protocol);
static void           socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void             setup_signal_handler(void);
static void             sigint_handler(int signum);

#endif //SETUP_H
