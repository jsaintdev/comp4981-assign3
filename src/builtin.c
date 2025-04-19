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
    const char *path;
    char       *path_copy;
    const char *dir;
    char       *saveptr;
    const char *arg;
    const char *builtins[] = {"cd", "pwd", "echo", "exit", "type", "meow"};

    arg = client->args;

    // Check for missing argument
    if(arg == NULL || *arg == '\0')
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error using [type]: No command provided\n");
        return;
    }

    // Built-in commands
    for(int i = 0; i < BASE_FIVE; i++)
    {
        if(strcmp(arg, builtins[i]) == 0)
        {
            snprintf(client->output, MAX_MSG_LENGTH, "%s is a shellkitty builtin\n", arg);
            return;
        }
    }

    // Check if command is in PATH
    path = getenv("PATH");
    if(path == NULL)
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: PATH not set\n");
        return;
    }

    path_copy = strdup(path);    // copy PATH since strtok modifies it
    if(path_copy == NULL)
    {
        snprintf(client->output, MAX_MSG_LENGTH, "Error: Memory allocation failed\n");
        return;
    }

    dir = strtok_r(path_copy, ":", &saveptr);
    while(dir != NULL)
    {
        char full_path[PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);
        if(access(full_path, X_OK) == 0)
        {
            // Clear buffer
            client->output[0] = '\0';

            // Append in chunks
            strncat(client->output, arg, MAX_MSG_LENGTH - strlen(client->output) - 1);
            strncat(client->output, " is ", MAX_MSG_LENGTH - strlen(client->output) - 1);
            strncat(client->output, full_path, MAX_MSG_LENGTH - strlen(client->output) - 1);
            strncat(client->output, "\n", MAX_MSG_LENGTH - strlen(client->output) - 1);

            free(path_copy);
            return;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    snprintf(client->output, MAX_MSG_LENGTH, "%s not found\n", arg);
    free(path_copy);
}

void process_meow(client_info *client)
{
    // Output string buffer
    char buffer[MAX_MSG_LENGTH] = "meow";
    int  count                  = 1;

    // Seed rand() once per session if needed
    static int seeded = 0;
    if(!seeded)
    {
        unsigned int seed;
        if(getrandom(&seed, sizeof(seed), 0) != sizeof(seed))
        {
            seed = MEANING_OF_LIFE;
        }
        srand(seed);

        seeded = 1;
    }

    while(count < BASE_FIVE)    // Up to 4 follow-ups (total 5 lines max)
    {
        if(rand() % 2 == 0)
        {
            strncat(buffer, " meow", MAX_MSG_LENGTH - strlen(buffer) - 1);
        }
        else
        {
            strncat(buffer, " purr", MAX_MSG_LENGTH - strlen(buffer) - 1);
        }

        // 50% chance to stop
        if(rand() % 2 == 0)
        {
            break;
        }

        count++;
    }

    strncat(buffer, "\n", MAX_MSG_LENGTH - strlen(buffer) - 1);
    snprintf(client->output, MAX_MSG_LENGTH, "%s", buffer);
}
