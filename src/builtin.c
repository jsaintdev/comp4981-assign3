#include "builtin.h"

void process_cd(client_info *client)
{
    const char *path = client->args;

    // Default set to home
    if(path == NULL || *path == '\0')
    {
        path = getenv("HOME");
        if(path == NULL)
        {
            path = "/";
        }
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

void process_type(client_info *client)
{
    const char *arg = client->args;

    // Check for missing argument
    if(arg == NULL || *arg == '\0')
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error using [type]: No command provided\n");
        return;
    }

    // Built-in commands
    const char *builtins[] = {"cd", "pwd", "echo", "exit", "type"};
    for(int i = 0; i < 5; i++)
    {
        if(strcmp(arg, builtins[i]) == 0)
        {
            snprintf(client->output, MAX_MSG_LENGTH, "%s is a shell builtin\n", arg);
            return;
        }
    }

    // Check if command is in PATH
    char *path = getenv("PATH");
    if(path == NULL)
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: PATH not set\n");
        return;
    }

    char *path_copy = strdup(path); // copy PATH since strtok modifies it
    if(path_copy == NULL)
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Memory allocation failed\n");
        return;
    }

    char *saveptr;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while(dir != NULL)
    {
        char full_path[PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
        if(access(full_path, X_OK) == 0)
        {
            snprintf(client->output, MAX_MSG_LENGTH, "%s is %s\n", arg, full_path);
            free(path_copy);
            return;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    snprintf(client->output, MAX_MSG_LENGTH, "%s not found\n", arg);
    free(path_copy);
}

