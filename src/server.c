#include "server.h"
#include "setup.h"

int main(int argc, char *argv[])
{
    static struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT,    SETUP,            setup_server      },
        {SETUP,            WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     RECEIVE_CMD,      receive_command   },
        {RECEIVE_CMD,      PARSE_CMD,        parse_command     },
        {PARSE_CMD,        CHECK_CMD_TYPE,   check_command_type},
        {CHECK_CMD_TYPE,   INVALID_CMD,      invalid_command   },
        {CHECK_CMD_TYPE,   EXECUTE_BUILT_IN, execute_built_in  },
        {CHECK_CMD_TYPE,   SEARCH_FOR_CMD,   search_for_command},
        {SEARCH_FOR_CMD,   EXECUTE_CMD,      execute_command   },
        {INVALID_CMD,      SEND_OUTPUT,      send_output       },
        {EXECUTE_BUILT_IN, SEND_OUTPUT,      send_output       },
        {EXECUTE_CMD,      SEND_OUTPUT,      send_output       },
        {SEND_OUTPUT,      WAIT_FOR_CMD,     wait_for_command  },
        {SETUP,            ERROR,            state_error       },
        {WAIT_FOR_CMD,     CLEANUP,          cleanup           },
        {WAIT_FOR_CMD,     ERROR,            state_error       },
        {RECEIVE_CMD,      ERROR,            state_error       },
        {PARSE_CMD,        ERROR,            state_error       },
        {CHECK_CMD_TYPE,   ERROR,            state_error       },
        {SEARCH_FOR_CMD,   ERROR,            state_error       },
        {INVALID_CMD,      ERROR,            state_error       },
        {EXECUTE_BUILT_IN, ERROR,            state_error       },
        {EXECUTE_CMD,      ERROR,            state_error       },
        {SEND_OUTPUT,      ERROR,            state_error       },
        {ERROR,            CLEANUP,          cleanup           },
        {CLEANUP,          P101_FSM_EXIT,    NULL              }
    };
    struct p101_error    *error;
    struct p101_env      *env;
    struct p101_fsm_info *fsm;
    p101_fsm_state_t      from_state;
    p101_fsm_state_t      to_state;
    struct p101_error    *fsm_error;
    struct p101_env      *fsm_env;

    char *address;
    char *port_str;

    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;
    int                     exit_code;

    address  = NULL;
    port_str = NULL;

    setup_signal_handler();

    // Start the server program
    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);

    // Set up FSM
    error = p101_error_create(false);
    if(error == NULL)
    {
        exit_code = EXIT_FAILURE;
        goto done;
    }
    env = p101_env_create(error, true, NULL);
    if(p101_error_has_error(error))
    {
        exit_code = EXIT_FAILURE;
        goto free_error;
    }

    fsm_error = p101_error_create(false);
    if(fsm_error == NULL)
    {
        exit_code = EXIT_FAILURE;
        goto done;
    }
    fsm_env   = p101_env_create(error, true, NULL);
    if(p101_error_has_error(fsm_error))
    {
        exit_code = EXIT_FAILURE;
        goto free_error;
    }

    fsm = p101_fsm_info_create(env, error, "application-fsm", fsm_env, fsm_error, NULL);
        p101_fsm_run(fsm, &from_state, &to_state, &context, transitions, sizeof(transitions));
    p101_fsm_info_destroy(env, &fsm);

    // Set up Signal Handler


    // Server Loop
    //    while(!(exit_flag))
    //    {
    //        int                     client_sockfd;
    //        struct sockaddr_storage client_addr;
    //        socklen_t               client_addr_len;
    //        pid_t                   pid;
    //
    //        client_addr_len = sizeof(client_addr);
    //        client_sockfd   = socket_accept_connection(sockfd, &client_addr, &client_addr_len);
    //
    //        if(client_sockfd == -1)
    //        {
    //            if(exit_flag)
    //            {
    //                break;
    //            }
    //            continue;
    //        }
    //
    //        // Fork a new child process for each new connection
    //        pid = fork();
    //        if(pid == -1)
    //        {
    //            perror("Error creating child process");
    //            close(client_sockfd);
    //            continue;
    //        }
    //
    //        if(pid == 0)
    //        {
    //            // Child Process
    //            close(sockfd);
    //
    //            // Receive, process, and send data back
    //
    //            // Shut down child process
    //            shutdown_socket(client_sockfd, SHUT_RDWR);
    //            socket_close(client_sockfd);
    //            exit(EXIT_SUCCESS);
    //        }
    //        else
    //        {
    //            // Parent Process
    //            close(client_sockfd);
    //            waitpid(-1, NULL, WNOHANG);
    //        }
    //    }
    //
    //    // Graceful Termination
    //    shutdown_socket(sockfd, SHUT_RDWR);
    //    socket_close(sockfd);


    free(fsm_env);

free_fsm_error:
    p101_error_reset(fsm_error);
    p101_free(env, fsm_error);

free_env:
    p101_free(env, env);

free_error:
    p101_error_reset(error);
    free(error);

done:
    return exit_code;
}

static p101_fsm_state_t setup_server(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_bind(sockfd, &addr, port);
    start_listening(sockfd, SOMAXCONN);

    return WAIT_FOR_CMD;
}

static p101_fsm_state_t wait_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return RECEIVE_CMD;
}

static p101_fsm_state_t receive_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return PARSE_CMD;
}

static p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return CHECK_CMD_TYPE;
}

static p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg)
{
    p101_fsm_state_t next_state;

    P101_TRACE(env);

    next_state = INVALID_CMD;
    // next_state = EXECUTE_BUILT_IN;
    // next_state = SEARCH_FOR_CMD;

    return next_state;
}

static p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return send_output;
}

static p101_fsm_state_t execute_built_in(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return send_output;
}

static p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return execute_command;
}

static p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return send_output;
}

static p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return wait_for_command;
}

static p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return cleanup;
}

static p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);
    return P101_FSM_EXIT;
}

static void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

static void shutdown_socket(int sockfd, int how)
{
    if(shutdown(sockfd, how) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}
