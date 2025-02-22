#include "client.h"
#include "setup.h"

int main(int argc, char *argv[])
{
    char input[MAX_INPUT];       // Buffer to store user input
    char response[MAX_INPUT];    // Buffer for server response

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

    printf("[DEBUG] Attempting to connect to server...\n");
    socket_connect(sockfd, &addr, port);
    printf("[DEBUG] Successfully connected to server.\n");

    setup_signal_handler();

    while(!(exit_flag))
    {
        ssize_t len;
        ssize_t bytes_read;
        ssize_t bytes_written;

        // Display the shell prompt
        printf("shellkitty$ ");
        fflush(stdout);

        // Read input from the user
        len = read(STDIN_FILENO, input, MAX_INPUT - 1);
        if(len == 0)
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

        printf("[DEBUG] Sending command: '%s' (Length: %zd bytes)\n", input, len);

        // **Send user input to server**

        bytes_written = write(sockfd, input, (size_t)len);
        if(bytes_written == -1)
        {
            perror("Error sending command to server");
            break;
        }
        printf("[DEBUG] Successfully sent %zd bytes to server.\n", bytes_written);

        // **Receive and print the response from the server**
        bytes_read = read(sockfd, response, sizeof(response) - 1);

        if(bytes_read == 0)
        {
            printf("Server disconnected. Exiting...\n");
            break;
        }
        if(bytes_read < 0)
        {
            printf("Read failed");
            break;
        }

        response[bytes_read] = '\0';    // Null-terminate response
        printf("%s", response);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}

// Establishes a connection between the client and the server
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        const char *msg;

        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
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
