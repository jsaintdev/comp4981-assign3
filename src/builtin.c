#include "builtin.h"

void process_cd(client_info *client)
{
    const char *path = client->args;

    // Default set to home
    if(path == NULL || *path == '\0')
    {
        path = getenv("HOME");
    }

    // Execute chdir
    if(chdir(path) != 0)
    {
        perror("No such file or directory");
        snprintf(client->output, MAX_MSG_LENGTH, "Error using [cd]: No such file or directory\n");
        return;
    }

    // Success message
    printf("Changing directory\n");
    snprintf(client->output, MAX_MSG_LENGTH, "Changed directory to %s\n", path);
}

void process_pwd(client_info *client)
{
    if(getcwd(client->output, MAX_MSG_LENGTH) != NULL)
    {
        snprintf(client->output + strlen(client->output), MAX_MSG_LENGTH - strlen(client->output), "\n");
    }
    else
    {
        perror("Error retrieving current directory");
        snprintf(client->output, MAX_MSG_LENGTH, "Error using [pwd]: unable to retrieve current directory\n");
    }
}

void process_echo(client_info *client)
{
    if(*client->args == '\0')
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error using [echo]: No message provided\n");
    }
    else
    {
        snprintf(client->output, MAX_MSG_LENGTH, "%s\n", client->args);
    }
}

// static void process_type(client_info *client)
//{
//
// }
