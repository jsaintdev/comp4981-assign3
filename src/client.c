#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 1024
#define CMD_NOT_FOUND 127

int main(void)
{
    char input[MAX_INPUT];    // Buffer to store user input
    int  status = 0;          // Variable to store the last
    // "exit status"
    while(1)
    {
        size_t len;
        // Display the shell prompt
        printf("shellkitty$ ");
        fflush(stdout);
        // Read input from the user
        if(fgets(input, MAX_INPUT, stdin) == NULL)
        {
            perror("fgets");
            break;
        }
        // Remove trailing newline character
        len = strlen(input);
        if(len > 0 && input[len - 1] == '\n')
        {
            input[len - 1] = '\0';
        }
        // Handle the "exit" command
        if(strcmp(input, "exit") == 0)
        {
            printf("Exiting shell...\n");
            break;
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
        // **Placeholder for Command Execution**
        // At this point, the shell would parse the input and
        // determine whether to execute an external command,
        // a built-in command, or handle a syntax error.
        // For example:
        // - Tokenize the input into a command and arguments.
        // - If it's a built-in, handle it directly
        // (e.g., "cd", "echo").
        // - Otherwise, attempt to execute it using system
        // calls like fork() and exec().
        // Code for these features could go here.
        // Simulate an exit status for demonstration

        if(strcmp(input, "simulate_success") == 0)
        {
            status = 0;    // Success
            printf("Simulating success: $? = %d\n", status);
        }
        else if(strcmp(input, "simulate_failure") == 0)
        {
            status = 1;    // Failure
            printf("Simulating failure: $? = %d\n", status);
        }
        else
        {
            // Default behaviour for unrecognized input
            printf("Unrecognized input: \"%s\"\n", input);
            status = CMD_NOT_FOUND;    // Indicate command not found
        }
        // Print the last status for debugging (optional)
        // printf("Debug: Last status = %d\n", status);
    }
    return EXIT_SUCCESS;
}
