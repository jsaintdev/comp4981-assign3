#include "server.h"
#include "builtin.h"
#include "setup.h"

static p101_fsm_state_t wait_for_command(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t execute_built_in(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg);
static p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg);

static void start_listening(int server_fd, int backlog);
static int  socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);
static void shutdown_socket(int sockfd, int how);
static void socket_close(int sockfd);
static void process_exit(void);
static int  find_executable(const char *cmd, char *full_path, size_t size);

int main(int argc, char *argv[])
{
    static struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT,    WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     WAIT_FOR_CMD,     wait_for_command  },
        {WAIT_FOR_CMD,     PARSE_CMD,        parse_command     },
        {WAIT_FOR_CMD,     CLEANUP,          cleanup           },
        {PARSE_CMD,        CHECK_CMD_TYPE,   check_command_type},
        {CHECK_CMD_TYPE,   EXECUTE_BUILT_IN, execute_built_in  },
        {CHECK_CMD_TYPE,   SEARCH_FOR_CMD,   search_for_command},
        {CHECK_CMD_TYPE,   CLEANUP,          cleanup           },
        {SEARCH_FOR_CMD,   EXECUTE_CMD,      execute_command   },
        {SEARCH_FOR_CMD,   INVALID_CMD,      invalid_command   },
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Waits for input from connected clients or new connection attempts using select().
    Accepts new connections or reads messages from active clients into their buffer.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    WAIT_FOR_CMD: Continue waiting for input
    PARSE_CMD: A message was received and should be parsed
    CLEANUP: Shutdown was requested
    ERROR: A socket or select error occurred
*/
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
    int                     new_socket;

    P101_TRACE(env);
    server_state = (server_data *)arg;
    client_len   = sizeof(client_addr);
    new_socket   = -1;

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
        int assigned = 0;

        // Exit if exit_flag is set
        if(exit_flag)
        {
            return CLEANUP;
        }

        new_socket = socket_accept_connection(server_state->server_socket, &client_addr, &client_len);
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
                assigned = 1;
                break;
            }
        }

        if(!assigned)
        {
            fprintf(stderr, "Max clients reached, rejecting new connection.\n");
            close(new_socket);
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
                printf("[input] from client %d: %s\n", client_socket, buffer);
                return PARSE_CMD;
            }
        }
    }

    (void)new_socket;
    return WAIT_FOR_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Parses a client's message into a command and its arguments.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    CHECK_CMD_TYPE: Command successfully parsed
*/
static p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg)
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

    // printf("Parsed command: %s\n", client->cmd);
    // printf("Parsed argument(s): %s\n", client->args);

    return CHECK_CMD_TYPE;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Determines the type of command received and selects the next state.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    CLEANUP: If "exit" command is received
    EXECUTE_BUILT_IN: If the command is a recognized built-in
    SEARCH_FOR_CMD: If the command may be external
*/
static p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg)
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
        printf("[exit] Shutting down server...\n");
        next_state = CLEANUP;
    }
    else if(strcmp(client->cmd, "cd") == 0 || strcmp(client->cmd, "pwd") == 0 || strcmp(client->cmd, "echo") == 0 || strcmp(client->cmd, "type") == 0 || strcmp(client->cmd, "meow") == 0)
    {
        printf("[type] %s is built-in\n", client->cmd);
        next_state = EXECUTE_BUILT_IN;
    }
    else
    {
        // printf("[type] %s is external\n", client->cmd);
        next_state = SEARCH_FOR_CMD;
    }

    return next_state;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Handles an unrecognized command by setting an error message for the client.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    SEND_OUTPUT: Transition to send the error message to the client
*/
static p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg)
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

/*
    Executes a built-in command like cd, pwd, echo, type, or meow.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    SEND_OUTPUT: Transition to send the result to the client
*/
static p101_fsm_state_t execute_built_in(const struct p101_env *env, struct p101_error *err, void *arg)
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
    else if(strcmp(client->cmd, "type") == 0)
    {
        process_type(client);
    }
    else if(strcmp(client->cmd, "meow") == 0)
    {
        process_meow(client);
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

/*
    Searches for an external command in the system's PATH and sets the full path.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    EXECUTE_CMD: If the command is found
    INVALID_CMD: If the command is not found
*/
static p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
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
    printf("[type] %s is external at %s\n", client->cmd, command_path);
    snprintf(client->cmd_path, MAX_MSG_LENGTH, "%s", command_path);
    return EXECUTE_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Executes an external command and captures its output via a pipe.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    SEND_OUTPUT: After execution, with output or error message
*/
static p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg)
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
#if defined(__linux__)
    // Linux-specific: use pipe2 with O_CLOEXEC
    if(pipe2(pipe_fds, O_CLOEXEC) == -1)
    {
        perror("pipe2 failed");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to create pipe\n");
        return SEND_OUTPUT;
    }
#else
    // macOS and other POSIX systems
    if(pipe(pipe_fds) == -1)
    {
        perror("pipe failed");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to create pipe\n");
        return SEND_OUTPUT;
    }

    // Set close-on-exec manually
    if(fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
    {
        perror("fcntl failed");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to set pipe flags\n");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return SEND_OUTPUT;
    }
#endif

    // Set FD_CLOEXEC flag manually
    if(fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) == -1)
    {
        perror("Failed to set FD_CLOEXEC");
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unable to set pipe flags\n");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return SEND_OUTPUT;
    }

    // For cat, check if there are args
    if(client->args[0] == '\0' && strcmp(client->cmd, "cat") == 0)
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: 'cat' requires input or filename\n");
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

/*
    Sends the generated output back to the active client.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    WAIT_FOR_CMD: After successfully sending the response
    ERROR: If an error occurs during transmission
*/
static p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg)
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

    printf("[output] to client %d: %s\n", client->client_socket, client->output);

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

/*
    Handles critical errors during FSM execution and transitions to cleanup.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    CLEANUP: Always transitions to cleanup after reporting an error
*/
static p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    printf("A critical server error occurred\n");

    return CLEANUP;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
    Releases resources and closes all client and server sockets before shutdown.

    @param
    env: The program context
    err: Used for error reporting
    arg: The program configuration details

    @return
    P101_FSM_EXIT: Indicates the server is shutting down
*/
static p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          i;

    P101_TRACE(env);

    server_state = (server_data *)arg;

    // printf("Cleaning up server resources...\n");

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

/*
    Starts listening for incoming connections on the specified socket.

    @param
    server_fd: The server socket file descriptor
    backlog: The maximum length of the pending connections queue
*/
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

/*
    Accepts an incoming connection on the server socket and retrieves client information.

    @param
    server_fd: The server socket file descriptor
    client_addr: Pointer to a sockaddr_storage struct to store client address info
    client_addr_len: Pointer to a variable containing the size of client_addr

    @return
    A new socket file descriptor for the client, or -1 on failure
*/
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
            printf("accept() failed");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}

/*
    Shuts down the specified socket for reading, writing, or both.

    @param
    sockfd: The socket file descriptor
    how: Specifies how to shut down (e.g., SHUT_RD, SHUT_WR, SHUT_RDWR)
*/
static void shutdown_socket(int sockfd, int how)
{
    if(shutdown(sockfd, how) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

/*
    Closes the specified socket and exits on failure.

    @param
    sockfd: The socket file descriptor to close
*/
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

/*
    Signals the server to begin shutdown by setting the global exit_flag.
*/
static void process_exit(void)
{
    exit_flag = 1;
}

/*
    Searches for an executable command in the system's PATH.

    @param
    cmd: Name of the command to search for
    full_path: Buffer to store the resolved full path
    size: Size of the full_path buffer

    @return
    0 if the executable is found, -1 otherwise
*/
static int find_executable(const char *cmd, char *full_path, size_t size)
{
    const char *path;
    const char *dir;
    char        candidate[MAX_MSG_LENGTH];
    char       *saveptr;
    char       *path_copy;

    // Get the system PATH
    path = getenv("PATH");
    if(!path || *path == '\0')
    {
        path = "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    }

    // Create modifiable copy
    path_copy = strdup(path);

    // Tokenize and search directories in PATH
    dir = strtok_r(path_copy, ":", &saveptr);
    while(dir != NULL)
    {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, cmd);

        if(access(candidate, X_OK) == 0)
        {
            strncpy(full_path, candidate, size);
            free(path_copy);
            return 0;
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
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

/*
    Sets up a signal handler for SIGINT to allow graceful termination.

    @param
    signum: Signal number to handle (unused)
*/
void sigint_handler(int signum)
{
    exit_flag = EXIT_CODE;
    printf("SIGINT received. Cleaning up...\n");
}

#pragma GCC diagnostic pop
