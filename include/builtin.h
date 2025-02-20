#ifndef BUILTIN_H
#define BUILTIN_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATH_LEN 1024

// exit
void process_pwd();
// echo

#endif    // BUILTIN_H
