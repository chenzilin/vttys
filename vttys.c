#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#include <termio.h>

static char slave1[64];
static char master1[64];
static char slave2[64];
static char master2[64];

static char buffer[1024];

static int g_argc = 1;
static char argv1[64];
static char argv2[64];

int ptym_open(char *pts_name, char *pts_name_s , int pts_namesz)
{
    int     fdm;
    char    *ptr;

    strncpy(pts_name, "/dev/ptmx", pts_namesz);
    pts_name[pts_namesz - 1] = '\0';


    if ((fdm = posix_openpt(O_RDWR | O_NONBLOCK)) < 0) return -1;

    if (grantpt(fdm) < 0) {
        close(fdm);
        return -2;
    }

    if (unlockpt(fdm) < 0) {
        close(fdm);
        return -3;
    }

    if ((ptr = ptsname(fdm)) == NULL) {
        close(fdm);
        return -4;
    }

    strncpy(pts_name_s, ptr, pts_namesz);
    pts_name[pts_namesz - 1] = '\0';

    return(fdm);
}

int conf_ser(int serialDev)
{

    int rc;
    struct termios params;

    // Get terminal atributes
    rc = tcgetattr(serialDev, &params);

    // Modify terminal attributes
    cfmakeraw(&params);

    rc = cfsetispeed(&params, B9600);
    rc = cfsetospeed(&params, B9600);

    // CREAD - Enable port to read data
    // CLOCAL - Ignore modem control lines
    params.c_cflag |= (B9600 |CS8 | CLOCAL | CREAD);

    // Make Read Blocking
    //fcntl(serialDev, F_SETFL, 0);

    // Set serial attributes
    rc = tcsetattr(serialDev, TCSANOW, &params);

    // Flush serial device of both non-transmitted
    // output data and non-read input data....
    tcflush(serialDev, TCIOFLUSH);

    return EXIT_SUCCESS;
}

void copydata(int fdfrom, int fdto, int direction)
{
    ssize_t br, bw;
    char *pbuf = buffer;
    br = read(fdfrom, buffer, 1024);

    if (br < 0) {
        if (errno == EAGAIN || errno == EIO) {
            br = 0;
        }
        else {
            perror("read");
            exit(1);
        }
    }
    if (br > 0) {

#ifdef DEBUG
        if (direction < 1) {
            if (g_argc >= 3) {
                printf("\n%s ==> %s:\n", argv1, argv2);
            }
            else {
                printf("\n%s ==> %s:\n", slave1, slave2);
            }
        }
        else {
            if (g_argc >= 3) {
                printf("\n%s ==> %s:\n", argv2, argv1);
            }
            else {
                printf("\n%s ==> %s:\n", slave2, slave1);
            }
        }

        int i = 0;
        while (i < br) {
            fprintf(stderr, "%x", buffer[i++]);
        }
#endif
        do
        {
            do
            {
                bw = write(fdto, pbuf, br);
                if (bw > 0) {
                    pbuf += bw;
                    br -= bw;
                }
            } while (br > 0 && bw > 0);
        } while (bw < 0 && errno == EAGAIN);
        if (bw <= 0) {
            // kernel buffer may be full, but we can recover
            fprintf(stderr, "Write error, br=%d bw=%d\n", (int) br, (int) bw);
            usleep(500000);
            // discard input
            while (read(fdfrom, buffer, 1024) > 0) ;
        }
    }
    else {
        usleep(100000);
    }
}

int main(int argc, char* argv[])
{
    int fd1;
    int fd2;
    fd_set rfds;

    fd1=ptym_open(master1, slave1, 64);
    fd2=ptym_open(master2, slave2, 64);

    if (argc >= 3) {
        unlink(argv[1]);
        unlink(argv[2]);
        if (symlink(slave1, argv[1]) < 0) {
            fprintf(stderr, "Cannot create: %s\n", argv[1]);
            return 1;
        }
        if (symlink(slave2, argv[2]) < 0) {
            fprintf(stderr, "Cannot create: %s\n", argv[2]);
            return 1;
        }

        g_argc = argc;
        strcpy(argv1, argv[1]);
        strcpy(argv2, argv[2]);
        printf("\n%s <=> %s\n", argv1, argv2);
    }
    else {
        printf("\n%s <=> %s\n", slave1, slave2);
    }

    conf_ser(fd1);
    conf_ser(fd2);

    while(1) {

        FD_ZERO(&rfds);
        FD_SET(fd1, &rfds);
        FD_SET(fd2, &rfds);

        if (-1 == select(fd2 + 1, &rfds, NULL, NULL, NULL)) {
            perror("select");
            return 1;
        }

        if (FD_ISSET(fd1, &rfds)) {
            copydata(fd1, fd2, 0);
        }

        if (FD_ISSET(fd2, &rfds)) {
            copydata(fd2, fd1, 1);
        }
    }

    close(fd1);
    close(fd2);

    return EXIT_SUCCESS;
}
