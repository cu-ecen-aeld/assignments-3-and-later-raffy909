#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <libgen.h> // For dirname() : https://linux.die.net/man/3/dirname

// https://stackoverflow.com/a/2336245
static void _mkdir(const char *dir) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}

int main(int argc, char const *argv[])
{
    const char *path;
    const char *string_to_write;
    char *dir;

    openlog(NULL, 0, LOG_USER);

    if (argc < 3) {
        syslog(LOG_ERR, "usage: %s <path file to write> <string to write>\n", argv[0]);
        return EXIT_FAILURE;
    }

    path = argv[1];
    string_to_write = argv[2];

    dir = (char *)malloc(sizeof(char) * (strlen(path) + 1));
    strcpy(dir, path);
    
    dir = dirname((char *)dir);

    // https://stackoverflow.com/questions/3828192/checking-if-a-directory-exists-in-unix-system-call
    struct stat sb;
    if (!(stat(dir, &sb) == 0 && S_ISDIR(sb.st_mode))) {
        syslog(LOG_DEBUG, "Directory %s does not exist\n", dir);
        _mkdir(dir);
        syslog(LOG_DEBUG, "Directory %s created\n", dir);
    }

    free(dir);

    FILE *file = fopen(path, "a+"); 
    if (file != NULL) { 
        if(fprintf(file, "%s\n", string_to_write) < 0) {
            syslog(LOG_ERR, "Error writing file: %s \n", strerror(errno));
            return EXIT_FAILURE;
        } 
        fclose(file);
        syslog(LOG_DEBUG, "Succesfully written %s to %s\n", string_to_write, path);
    } else { 
        syslog(LOG_ERR, "Error opening file: %s \n", strerror(errno));
        return EXIT_FAILURE;
    } 

    return EXIT_SUCCESS;
}