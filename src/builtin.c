#include "builtin.h"

// Note: adapted from code found via Stack Overflow
void process_pwd()
{
    char           path[PATH_LEN] = "";
    struct stat    statbuf, parent_statbuf;
    DIR           *dir;
    struct dirent *entry;

    while(1)
    {
        // Get the inode and device of the current directory
        if(lstat(".", &statbuf) == -1)
        {
            perror("lstat(.)");
            return;
        }

        // Open the parent directory
        if(chdir("..") == -1)
        {
            perror("chdir(..)");
            return;
        }

        if(lstat(".", &parent_statbuf) == -1)
        {
            perror("lstat(..)");
            return;
        }

        // Check if we are at the root
        if(statbuf.st_ino == parent_statbut.st_into && statbuf.st_dev == parent_statbuf.st_dev)
        {
            break;
        }

        // Open the parent directory and find the directory name
        dir = opendir(".");
        if(!dir)
        {
            perror("opendir");
            return;
        }

        while((entry = readdir(dir)) != NULL)
        {
            struct stat entry_stat;
            if(lstat(entry->d_name, &entry_stat) == -1)
                continue;

            if(entry_stat.st_ino == statbuf.st_ino && entry_stat.st_dev == statbuf.st_dev)
            {
                char temp[PATH_LEN];
                snprintf(temp, sizeof(temp), "%s%s", entry->d_name, path);
                strncpy(path, temp, sizeof(path));
                break;
            }
        }

        closedir(dir);
    }

        printf("%s\n, (*path) ? path : "/");
        // TO DO: return file path
}