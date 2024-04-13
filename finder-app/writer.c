#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>


int close_file(int fd) {
    int res = close(fd);
    if (res == -1) {
        syslog(LOG_ERR, "Main: close: %s\n", strerror(errno));
        EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    // Check for correct number of arguments
    if (argc < 3) {
        syslog(LOG_ERR, "Main: %s\n", "Too few arguments, aborting.");
        return EXIT_FAILURE;
    }

    // If file does not exist, create it. If file exists, remove all old content.
    const char *filename = argv[1];
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Main: open: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // Write data to the file. For regular files on disk we should be guaranteed to write the
    // entire data unless an error occurs. So there should be no need to handle partial writes.
    const char *data = argv[2];
    size_t len = strlen(data);
    int res = write(fd, data, len);
    if (res == -1) {
        syslog(LOG_ERR, "Main: write: %s\n", strerror(errno));
        close_file(fd);
        return EXIT_FAILURE;
    }
    syslog(LOG_DEBUG, "Writing %s to %s\n", data, filename);

    res = close_file(fd);
    return res;
}