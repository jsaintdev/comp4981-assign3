#include "server.h"
#include "builtin.h"
#include "setup.h"

int main(int argc, char *argv[])
{
    static struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT,    WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     PARSE_CMD,        parse_command     },
        {WAIT_FOR_CMD,     CLEANUP,          cleanup           },
        {PARSE_CMD,        CHECK_CMD_TYPE,   check_command_type},
        {CHECK_CMD_TYPE,   INVALID_CMD,      invalid_command   },
        {CHECK_CMD_TYPE,   EXECUTE_BUILT_IN, execute_built_in  },
        {CHECK_CMD_TYPE,   SEARCH_FOR_CMD,   search_for_command},
        {CHECK_CMD_TYPE,   CLEANUP,          cleanup           },
        {SEARCH_FOR_CMD,   EXECUTE_CMD,      execute_command   },
        {INVALID_CMD,      SEND_OUTPUT,      send_output       },
        {EXECUTE_BUILT_IN, SEND_OUTPUT,      send_output       },
        {EXECUTE_CMD,      SEND_OUTPUT,      send_output       },
        {SEND_OUTPUT,      WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     ERROR,            state_error       },
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
    server_data             server_state;

    address   = NULL;
    port_str  = NULL;
    exit_code = EXIT_SUCCESS;

    // Start the server program
    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);

    // Set up server
    convert_address(address, &addr);
    sockfd                     = socket_create(addr.ss_family, SOCK_STREAM, 0);
    server_state.server_socket = sockfd;
    socket_bind(sockfd, &addr, port);
    start_listening(sockfd, SOMAXCONN);

    // Set up signal handler
    setup_signal_handler();

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
        goto free_env;
    }
    fsm_env = p101_env_create(error, true, NULL);
    if(p101_error_has_error(fsm_error))
    {
        exit_code = EXIT_FAILURE;
        goto free_fsm_error;
    }

    fsm = p101_fsm_info_create(env, error, "application-fsm", fsm_env, fsm_error, NULL);
    p101_fsm_run(fsm, &from_state, &to_state, &server_state, transitions, sizeof(transitions));

    // Cleanup
    p101_fsm_info_destroy(env, &fsm);
    free(fsm_env);

free_fsm_error:
    p101_error_reset(fsm_error);
    free(fsm_error);

free_env:
    free(env);

free_error:
    p101_error_reset(error);
    free(error);

done:
    return exit_code;
}

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t wait_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    struct sockaddr_storage client_addr;
    struct timeval          timeout;
    server_data            *server_state;
    fd_set                  read_fds;
    socklen_t               client_len;
    ssize_t                 bytes_received;
    int                     activity;
    int                     nfds;
    int                     i;

    P101_TRACE(env);
    server_state = (server_data *)arg;
    client_len   = sizeof(client_addr);

    // **Reset FD_SET and add server socket**
    FD_ZERO(&read_fds);
    FD_SET(server_state->server_socket, &read_fds);

    // **Calculate max file descriptor (nfds)**
    nfds = server_state->server_socket;
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        int client_socket = server_state->clients[i].client_socket;
        if(client_socket > 0)
        {
            FD_SET(client_socket, &read_fds);
            nfds = (nfds > client_socket) ? nfds : client_socket;
        }
    }
    nfds += 1;

    // **Set timeout for select**
    timeout.tv_sec  = TIMEOUT;
    timeout.tv_usec = 0;

    activity = select(nfds, &read_fds, NULL, NULL, &timeout);

    if(activity < 0 && errno != EINTR)
    {
        perror("[ERROR] Select error");
        return ERROR;
    }
    if(activity == 0)
    {
        fflush(stdout);
        return WAIT_FOR_CMD;
    }

    // **Check for new client connections**
    if(FD_ISSET(server_state->server_socket, &read_fds))
    {
        int new_socket = socket_accept_connection(server_state->server_socket, &client_addr, &client_len);
        if(new_socket < 0)
        {
            perror("Accept error");
            return ERROR;
        }

        for(i = 0; i < MAX_CLIENTS; i++)
        {
            if(server_state->clients[i].client_socket == 0)
            {
                server_state->clients[i].client_socket = new_socket;
                memset(server_state->clients[i].msg, 0, MAX_MSG_LENGTH);
                break;
            }
        }
    }

    // **Check for input from existing clients**
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        int client_socket = server_state->clients[i].client_socket;

        if(client_socket > 0 && FD_ISSET(client_socket, &read_fds))
        {
            char buffer[MAX_MSG_LENGTH];

            bytes_received = recv(client_socket, buffer, MAX_MSG_LENGTH - 1, MSG_DONTWAIT);

            if(bytes_received < 0 && errno != EWOULDBLOCK && errno == EAGAIN)
            {
                printf("Waiting for data from client %d...\n", client_socket);
                continue;
            }

            if(bytes_received < 0)
            {
                perror("[ERROR] recv() failed");
                close(client_socket);
                FD_CLR(client_socket, &read_fds);
                server_state->clients[i].client_socket = 0;
                continue;
            }

            if(bytes_received == 0)
            {
                printf("Client %d disconnected\n", client_socket);
                close(client_socket);
                FD_CLR(client_socket, &read_fds);
                server_state->clients[i].client_socket = 0;
            }
            else
            {
                buffer[bytes_received] = '\0';
                strncpy(server_state->clients[i].msg, buffer, MAX_MSG_LENGTH);
                server_state->active_client = i;
                printf("Received message from client %d: %s\n", client_socket, buffer);
                return PARSE_CMD;
            }
        }
    }

    return WAIT_FOR_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    int          i;
    int          j;
    int          k;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    i = 0;    // for original message
    j = 0;    // for command buffer
    k = 0;    // for argument buffer

    // Extract the command
    while(client->msg[i] != ' ' && client->msg[i] != '\0' && j < MAX_CMD_LENGTH - 1)
    {
        client->cmd[j++] = client->msg[i++];
    }
    client->cmd[j] = '\0';

    // Move past space(s) to get to the arguments
    while(client->msg[i] == ' ')
    {
        i++;
    }

    // Extract any arguments
    if(client->msg[i] != '\0')
    {
        while(client->msg[i] != '\0' && k < MAX_ARGS_LENGTH - 1)
        {
            client->args[k++] = client->msg[i++];
        }
        client->args[k] = '\0';
    }
    else
    {
        client->args[0] = '\0';
    }

    printf("Parsed command: %s\n", client->cmd);
    printf("Parsed argument(s): %s\n", client->args);

    return CHECK_CMD_TYPE;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data       *server_state;
    int                client_index;
    const client_info *client;
    p101_fsm_state_t   next_state;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    if(strcmp(client->cmd, "exit") == 0)
    {
        printf("Command exit received. Shutting down server...\n");
        next_state = CLEANUP;
    }
    else if(strcmp(client->cmd, "cd") == 0 || strcmp(client->cmd, "pwd") == 0 || strcmp(client->cmd, "echo") == 0 || strcmp(client->cmd, "type") == 0)
    {
        printf("Built-in command %s received\n", client->cmd);
        next_state = EXECUTE_BUILT_IN;
    }
    else if(strcmp(client->cmd, "ls") == 0)
    {
        printf("External command %s received\n", client->cmd);
        next_state = SEARCH_FOR_CMD;
    }
    else
    {
        printf("Invald command %s received\n", client->cmd);
        next_state = INVALID_CMD;
    }

    return next_state;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    snprintf(client->output, MAX_MSG_LENGTH, "Error: Invalid command\n");

    return SEND_OUTPUT;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t execute_built_in(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    // Clear output buffer
    memset(client->output, 0, MAX_MSG_LENGTH);

    if(strcmp(client->cmd, "cd") == 0)
    {
        process_cd(client);
    }
    else if(strcmp(client->cmd, "pwd") == 0)
    {
        process_pwd(client);
    }
    else if(strcmp(client->cmd, "echo") == 0)
    {
        process_echo(client);
    }
    else if(strcmp(client->cmd, "exit") == 0)
    {
        process_exit();
    }
    else
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unrecognized built-in command\n");
    }

    return SEND_OUTPUT;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    char         command_path[MAX_PATH_LENGTH];

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    // Try to locate the command in the system's PATH
    if(find_executable(client->cmd, command_path, sizeof(command_path)) != 0)
    {
        // Command not found, set error message
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Command not found\n");
        return INVALID_CMD;
    }

    // Command found, store it and transition to execution
    snprintf(client->cmd_path, MAX_MSG_LENGTH, "%s", command_path);
    return EXECUTE_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    int          pipe_fds[2];
    pid_t        pid;
    char        *saveptr;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    if(client->cmd_path[0] == '\0')
    {
        perror("Executable not found");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Executable not found\n");
        return SEND_OUTPUT;
    }

    // Create a pipe
    if(pipe2(pipe_fds, O_CLOEXEC) == -1)
    {
        perror("Pipe creation failed");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to execute due to pipe creation failure\n");
        return SEND_OUTPUT;
    }

    // Fork a new child process
    pid = fork();
    if(pid < 0)
    {
        perror("Fork failed");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to execute due to fork failure\n");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return SEND_OUTPUT;
    }

    // Child process
    if(pid == 0)
    {
        char *argv[(MAX_ARGS_LENGTH / 2) + 2];
        int   argc;

        // Close read end
        close(pipe_fds[0]);

        // Redirect stdout and stderr
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);

        // Close write end
        close(pipe_fds[1]);

        // Prepare file path for execv
        argc         = 0;
        argv[argc++] = client->cmd_path;
        arg          = strtok_r(client->args, " ", &saveptr);

        while(arg && argc < MAX_ARGS_LENGTH / 2)
        {
            argv[argc++] = (char *)arg;
            arg          = strtok_r(NULL, " ", &saveptr);
        }
        argv[argc] = NULL;

        // Execute command
        execv(argv[0], argv);

        perror("Exec failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        char    buffer[MAX_MSG_LENGTH];
        ssize_t bytes_read;

        // Close write end
        close(pipe_fds[1]);

        // Prepare output from the child
        memset(buffer, 0, MAX_MSG_LENGTH);
        bytes_read = read(pipe_fds[0], buffer, MAX_MSG_LENGTH - 1);

        close(pipe_fds[0]);

        if(bytes_read > 0)
        {
            strncpy(client->output, buffer, MAX_MSG_LENGTH);
        }
        else
        {
            snprintf(client->output, MAX_MSG_LENGTH, "Error: no output from command\n");
        }

        // Wait for child to finish
        waitpid(pid, NULL, 0);
    }

    return SEND_OUTPUT;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    size_t       msg_length;
    ssize_t      total_written;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;

    // Validate client index before accessing the array
    if(client_index < 0 || client_index >= MAX_CLIENTS)
    {
        fprintf(stderr, "Invalid client index: %d\n", client_index);
        return ERROR;
    }

    client = &server_state->clients[client_index];

    printf("Sending output to client %d: %s\n", client->client_socket, client->output);

    msg_length    = strlen(client->output);
    total_written = 0;

    // Loop until the entire message is written
    while((size_t)total_written < msg_length)
    {
        ssize_t bytes_written;

        size_t bytes_to_write = msg_length - (size_t)total_written;
        bytes_written         = write(client->client_socket, client->output + total_written, bytes_to_write);

        if(bytes_written == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }

            if(errno == EAGAIN)
            {
                // Non-blocking mode: try again later
                fprintf(stderr, "Client socket %d is not ready for writing\n", client->client_socket);
                return WAIT_FOR_CMD;
            }
            perror("Error sending output to client");
            return ERROR;
        }

        if(bytes_written == 0)
        {
            // Client may have closed the connection
            fprintf(stderr, "Client socket %d closed the connection\n", client->client_socket);
            return ERROR;
        }

        total_written += bytes_written;
    }

    // Clear output buffer
    server_state->active_client = -1;
    memset(client->output, 0, MAX_MSG_LENGTH);
    memset(client->msg, 0, MAX_MSG_LENGTH);

    return WAIT_FOR_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    printf("A critical server error occurred\n");

    return CLEANUP;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          i;

    P101_TRACE(env);

    server_state = (server_data *)arg;

    printf("Cleaning up server resources...\n");

    // Close all active client sockets
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        if(server_state->clients[i].client_socket > 0)
        {
            close(server_state->clients[i].client_socket);
            server_state->clients[i].client_socket = 0;
        }
    }

    // Close the server socket
    if(server_state->server_socket > 0)
    {
        shutdown_socket(server_state->server_socket, SHUT_RDWR);
        socket_close(server_state->server_socket);
        server_state->server_socket = 0;
    }

    printf("Cleanup complete. Server shutting down.\n");

    return P101_FSM_EXIT;
}

#pragma GCC diagnostic pop

void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
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
            printf("accept() failed");
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

void shutdown_socket(int sockfd, int how)
{
    if(shutdown(sockfd, how) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

void process_exit(void)
{
    exit_flag = 1;
}

int find_executable(const char *cmd, char *full_path, size_t size)
{
    char       *path;
    const char *dir;
    char        candidate[MAX_MSG_LENGTH];
    char       *saveptr;

    // Get the system PATH
    path = getenv("PATH");
    if(!path)
    {
        return -1;
    }

    // Tokenize and search directories in PATH
    dir = strtok_r(path, ":", &saveptr);
    while(dir)
    {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, cmd);
        if(access(candidate, X_OK) == 0)
        {
            strncpy(full_path, candidate, size);
            return 0;    // Found
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    return -1;    // Not found
}

// Sets up a signal handler so the program can terminate gracefully
void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
    sigaction(SIGINT, &sa, NULL);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// Handles a SIGINT signal by setting a flag to signal termination
void sigint_handler(int signum)
{
    exit_flag = EXIT_CODE;
    printf("SIGINT received. Exiting...\n");
}

#pragma GCC diagnostic pop
