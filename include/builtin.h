#ifndef BUILTIN_H
#define BUILTIN_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
    #include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define PATH_LEN 1024

static void process_cd(const char *args_'
static void process_pwd();
static void process_echo(const char *args);
static void process_type(const char *args);

#endif    // BUILTIN_H
