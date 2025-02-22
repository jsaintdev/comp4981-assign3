#ifndef BUILTIN_H
#define BUILTIN_H

#include "server.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATH_LEN 1024

void process_cd(client_info *client);
void process_pwd(client_info *client);
void process_echo(client_info *client);
void process_type(client_info *client);

#endif    // BUILTIN_H
