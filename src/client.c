#include "client.h"
#include "setup.h"

int main(int argc, char *argv[])
{
    char input[MAX_INPUT];      // Buffer to store user input
    char response[MAX_INPUT0;	// Buffer for server response
    int  status = 0;          // Variable to store the last

    char                   *address;
    char                   *port_str;
    in_port_t               port;
    int                     sockfd;
    struct sockaddr_storage addr;

    address  = NULL;
    port_str = NULL;

    // Set up network socket
    parse_arguments(argc, argv, &address, &port_str);
    handle_arguments(argv[0], address, port_str, &port);
    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_connect(sockfd, &addr, port);

    while(1)
    {
        size_t len;
        // Display the shell prompt
        printf("shellkitty$ ");
        fflush(stdout);

        // Read input from the user
        len = read(STDIN_FILENO, input, MAX_INPUT - 1);
        if(len <= 0)
        {
            perror("Read error");
            break;
        }

        // Remove trailing newline character
        if(input[len - 1] == '\n')
        {
            input[len - 1] = '\0';
            len--;
        }

        // Substitute "$?" in the input with the last status
        for(char *p = input; (p = strstr(p, "$?"));)
        {
            char   temp[MAX_INPUT];
            size_t prefix_len;
            snprintf(temp, sizeof(temp), "%d", status);
            prefix_len = (size_t)(p - input);    // Text before "$?"

            snprintf(temp, sizeof(temp), "%.*s%d%s", (int)prefix_len, input, status, p + 2);
            strlcpy(input, temp, sizeof(input));
        }

        // **Send user input to server**
        if(write(sockfd, input, len) == -1)
        {
            perror("Error sending command to server");
            break;
        }

        // **Receive and print the response from the server**
        ssize_t bytes_read = read(sockfd, response, sizeof(response) - 1);

        if(bytes_read <= 0)
        {
            printf("Server disconnected. Exiting...\n");
            break;
        }

        response[bytes_read] = '\0';    // Null-terminate response
        printf("%s", response);

        // Update status based on server response
        if(strcmp(response, "Error: Command not found\n") == 0)
        {
            status = CMD_NOT_FOUND;
        }
        else
        {
            status = 0;    // Assume success if no error
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}

// Sets up a signal handler so the program can terminate gracefully
static void setup_signal_handler(void)
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
static void sigint_handler(int signum)
{
    exit_flag = EXIT_CODE;
    printf("SIGINT received. Exiting...\n");
}

#pragma GCC diagnostic pop
