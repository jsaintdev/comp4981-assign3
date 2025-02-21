#include "server.h"
#include "builtin.h"
#include "setup.h"

int main(int argc, char *argv[])
{
    static struct p101_fsm_transition transitions[] = {
        {P101_FSM_INIT,    WAIT_FOR_CMD,     wait_for_command  },
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
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
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
    p101_free(env, fsm_error);

free_env:
    p101_free(env, env);

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

static p101_fsm_state_t wait_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    struct sockaddr_in client_addr;
    server_data       *server_state;
    fd_set             read_fds;
    socklen_t          client_len;
    ssize_t            bytes_received;
    int                activity;
    int                new_socket;
    int                client_socket;
    int                i;
    char               buffer[MAX_MSG_LENGTH];

    P101_TRACE(env);

    server_state = (server_data *)arg;
    read_fds     = server_state->active_fds;
    client_len   = sizeof(client_addr);

    printf("Server waiting for command...\n");

    // Monitor sockets for activity
    activity = select(server_state->max_fd + 1, &read_fds, NULL, NULL, NULL);

    if(activity < 0 && errno != EINTR)
    {
        perror("Select error");
        return ERROR;
    }

    // Check for new socket connection
    if(FD_ISSET(server_state->server_socket, &read_fds))
    {
        new_socket = accept(server_state->server_socket, (struct sockaddr *)&client_addr, &client_len);
        if(new_socket < 0)
        {
            perror("Accept error");
            return ERROR;
        }
        printf("Accepted new client at socket %d\n", new_socket);

        // Add new client to struct
        for(i = 0; i < MAX_CLIENTS; i++)
        {
            if(server_state->clients[i].client_socket == 0)
            {
                server_state->clients[i].client_socket  = new_socket;
                server_state->clients[i].client_address = client_addr;
                memset(server_state->clients[i].msg, 0, MAX_MSG_LENGTH);
                FD_SET(new_socket, &server_state->active_fds);
                if(new_socket > server_state->max_fd)
                {
                    server_state->max_fd = new_socket;
                }
                break;
            }
        }
    }

    // Check for input from existing clients
    for(i = 0; i < MAX_CLIENTS; i++)
    {
        client_socket = server_state->clients[i].client_socket;

        if(client_socket > 0 && FD_ISSET(client_socket, &read_fds))
        {
            bytes_received = recv(client_socket, buffer, MAX_MSG_LENGTH - 1, 0);

            if(bytes_received <= 0)
            {
                // Client disconnected
                printf("Client %d disconnected\n", client_socket);
                close(client_socket);
                FD_CLR(client_socket, &server_state->active_fds);
                server_state->clients[i].client_socket = 0;
            }
            else
            {
                // Process message
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

static p101_fsm_state_t parse_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    int          i, j, k;

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
        i++;

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

static p101_fsm_state_t check_command_type(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data     *server_state;
    int              client_index;
    client_info     *client;
    p101_fsm_state_t next_state;

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

static p101_fsm_state_t invalid_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    snprintf(client->output, MAX_MSG_LENGTH, "Error: Invalid command '%s'\n", client->cmd);

    return SEND_OUTPUT;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

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
    else
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Unrecognized built-in command '%s'\n", client->cmd);
    }

    return SEND_OUTPUT;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static p101_fsm_state_t search_for_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    // Try to locate the command in the system's PATH
    if(find_executable(client->cmd, command_path, sizeof(client->cmd_path)) != 0)
    {
        // Command not found, set error message
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Command '%s' not found\n", client->cmd);
        return INVALID_CMD;
    }

    // Command found, store it and transition to execution
    snprintf(client->output, MAX_MSG_LENGTH, "%s", command_path);
    return EXECUTE_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static p101_fsm_state_t execute_command(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    int          pipe_fds[2];
    pid_t        pid;
    char         buffer[MAX_MSG_LENGTH];
    ssize_t      bytes_read;
    char        *argv[MAX_ARGS_LENGTH / 2 + 2];
    int          argc;

    P101_TRACE(env);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    if(client->cmd_path[0] == '\0')
    {
        perror("Executable not found");
        snprint(client->output, MAX_MSG_LENGTH, "Error: Executable not found\n");
        return SEND_OUTPUT;
    }

    // Create a pipe
    if(pipe(pipe_fds) == -1)
        ;
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
        char *arg    = strtok(client->args, " ");

        while(arg && argc < MAX_ARGS_LENGTH / 2)
        {
            argv[argc++] = arg;
            arg          = strtok(NULL, " ");
        }
        argv[argc] = NULL;

        // Execute command
        execv(argv[0], argv);

        perror("Exec failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        // Parent
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

static p101_fsm_state_t send_output(const struct p101_env *env, struct p101_error *err, void *arg)
{
    server_data *server_state;
    int          client_index;
    client_info *client;
    size_t       msg_length;
    ssize_t      bytes_written;
    ssize_t      total_written;

    P101_TRACE(env);
    printf("Sending output to client %d: %s\n", client->client_socket, client->output);

    server_state = (server_data *)arg;
    client_index = server_state->active_client;
    client       = &server_state->clients[client_index];

    msg_length    = strlen(client->output);
    bytes_written = 0;
    total_written = 0;

    // Loop until the whole message is written
    while(total_written < msg_length)
    {
        bytes_written = write(client->client_socket, client->output + total_written, msg_length - total_written);

        if(bytes_written == -1)
        {
            perror("Error sending output to client\n");
            return ERROR;
        }

        total_written += bytes_written;
    }

    // Clear output buffer
    memset(client->output, 0, MAX_MSG_LENGTH);

    return WAIT_FOR_CMD;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static p101_fsm_state_t state_error(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);

    return cleanup;
}

#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static p101_fsm_state_t cleanup(const struct p101_env *env, struct p101_error *err, void *arg)
{
    P101_TRACE(env);
    return P101_FSM_EXIT;
}

#pragma GCC diagnostic pop

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

static void process_exit(void)
{
    exit_flag = 1;
}

static int find_executable(const char *cmd, char *full_path, size_t size)
{
    char *path, *dir;
    char  candidate[MAX_MSG_LENGTH];

    // Get the system PATH
    path = getenv("PATH");
    if(!path)
    {
        return -1;
    }

    // Tokenize and search directories in PATH
    dir = strtok(path, ":");
    while(dir)
    {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, cmd);
        if(access(candidate, X_OK) == 0)
        {
            strncpy(full_path, candidate, size);
            return 0;    // Found
        }
        dir = strtok(NULL, ":");
    }

    return -1;    // Not found
}
