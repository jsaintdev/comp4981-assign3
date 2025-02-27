#ifndef SERVER_H
#define SERVER_H

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_convert/integer.h>
#include <p101_fsm/fsm.h>
#include <p101_posix/p101_string.h>
#include <p101_posix/p101_unistd.h>
#include <p101_unix/p101_getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

static volatile sig_atomic_t exit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

#define TIMEOUT 10
#define MAX_CLIENTS 10
#define MAX_MSG_LENGTH 256
#define MAX_CMD_LENGTH 32
#define MAX_ARGS_LENGTH 128
#define MAX_PATH_LENGTH 256

typedef struct
{
    int                client_socket;
    struct sockaddr_in client_address;
    pid_t              process_id;
    char               cmd[MAX_CMD_LENGTH];
    char               args[MAX_ARGS_LENGTH];
    char               cmd_path[MAX_PATH_LENGTH];
    char               msg[MAX_MSG_LENGTH];
    char               output[MAX_MSG_LENGTH];
} client_info;

typedef struct
{
    int         server_socket;
    client_info clients[MAX_CLIENTS];
    fd_set      active_fds;
    int         max_fd;
    int         active_client;
} server_data;

enum application_states
{
    WAIT_FOR_CMD = P101_FSM_USER_START,
    PARSE_CMD,
    CHECK_CMD_TYPE,
    INVALID_CMD,
    EXECUTE_BUILT_IN,
    SEARCH_FOR_CMD,
    EXECUTE_CMD,
    SEND_OUTPUT,
    CLEANUP,
    ERROR
};

void start_listening(int server_fd, int backlog);
int  socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
void shutdown_socket(int sockfd, int how);
void socket_close(int sockfd);
void process_exit(void);
int  find_executable(const char *cmd, char *full_path, size_t size);
void setup_signal_handler(void);
void sigint_handler(int signum);

p101_fsm_state_t wait_for_command(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t execute_built_in(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg);
p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg);

#endif    // SERVER_H
