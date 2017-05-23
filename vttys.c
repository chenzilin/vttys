#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termio.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>

static char slave1[64];
static char slave2[64];

static char buffer[1024];

static int g_argc = 1;
static char argv1[64];
static char argv2[64];

typedef struct {
    int direction;
    int leftFD;
    int rightFD;
    int isRunning;
} thread_para;

// 传参用结构体
thread_para thread_para1;
thread_para thread_para2;

pthread_t thread1;
pthread_t thread2;

static void signal_handler(int signo)
{
    if (signo == SIGINT){
        printf("main receive signal SIGINT\n");
        thread_para1.isRunning = 0;
        thread_para2.isRunning = 0;
    }
    if (signo == SIGTERM){
        printf("main receive signal: SIGTERM\n");
    }
}

// 打开连接并获得句柄
int getFd(char * slave_name)
{
    char * tmp;
    /*
     * 以读写方式打开|如果pathname指向终端设备，
     * 不将此设备分配作为进程的控制终端,
     * 第三个相当重要没有数据时要求立即返回使用非堵塞模式
     */
    int onlyFd= posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(onlyFd == -1
            || grantpt(onlyFd) == -1             // 更改不同组别的读写特权
            || unlockpt(onlyFd) == -1            // 在读写的同时允许其他进程进行访问
            || (tmp = ptsname(onlyFd)) == NULL ) // 得到伪从串口名字
        return -1;
    strncpy(slave_name, tmp, 64);
    slave_name[63] = '\0';
    return (onlyFd);
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
    // fcntl(serialDev, F_SETFL, 0);

    // Set serial attributes
    rc = tcsetattr(serialDev, TCSANOW, &params);

    // Flush serial device of both non-transmitted
    // output data and non-read input data....
    tcflush(serialDev, TCIOFLUSH);

    return EXIT_SUCCESS;
}


void * copydata(int fdfrom, int fdto,int direction)
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

void * deal_tty( void * arg)
{
    thread_para * recPara;
    recPara = (thread_para *)arg;
    fd_set rfds;
    int fd1 = recPara->leftFD;
    int fd2 = recPara->rightFD;
    struct timespec timeout = {5, 0};
    struct timespec timein = {5, 0};
    while(recPara->isRunning){
        FD_ZERO(&rfds);
        FD_SET(fd1, &rfds);
        FD_SET(fd2, &rfds);                                         // 在不连通的情况下，每五秒轮巡一次，文件状态不改变什么也不做
        if(pselect(fd2 + 1, &rfds, NULL, NULL, &timeout, NULL) > 0){// 如果用select函数时间会慢慢被扣掉最后会变成非堵塞状态CPU使用率百分之两百
            if (FD_ISSET(fd1, &rfds)) {
                copydata(fd1, fd2, 0);
            }
            if (FD_ISSET(fd2, &rfds)) {
                copydata(fd2, fd1, 1);
            }
            while(recPara->isRunning) {
                /*
                 * 必须放置在while循环内，每一次执行之前对文件状态进行初始化！！！
                 * 设置了三秒的超时等待防止在因为文件状态不改变卡死，发送报文的周期怎么样也比三秒小吧。
                 */
                FD_ZERO(&rfds);

                FD_SET(fd1, &rfds);
                FD_SET(fd2, &rfds);
                /*
                 * select用来观察在结构中的文件是否有数据需要读
                 * 第一个参数只是设置一个数值大小
                 * 第二个参数观察的是读属性
                 */
                int mark = pselect(fd2 + 1, &rfds, NULL, NULL, &timein, NULL);
                if ( mark == -1 || mark == 0) {  // -1是错误，0是超时返回，正常是返回这个文件结构里面发生改变文件的个数
                    break;                       // 如果五秒之内没有收到数据退出内层循环，此时如果外层isRunning为false直接便会结束线程
                }
                if (FD_ISSET(fd1, &rfds)) {
                    copydata(fd1, fd2, 0);
                }
                if (FD_ISSET(fd2, &rfds)) {
                    copydata(fd2, fd1, 1);
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{

    int fd1;
    int fd2;
    fd1 = getFd(slave1);
    fd2 = getFd(slave2);

    if (signal(SIGINT, signal_handler) == SIG_ERR ||
            signal(SIGTERM, signal_handler) == SIG_ERR) {
        printf("main process register signal handler fail\n");
        return 0;
    }

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

    thread_para1.direction = 0;
    thread_para2.direction = 1;
    thread_para1.leftFD = thread_para2.leftFD = fd1;
    thread_para1.rightFD = thread_para2.rightFD = fd2;
    thread_para1.isRunning = thread_para2.isRunning = 1;

    pthread_create(&thread1, NULL, deal_tty, &thread_para1);
    pthread_create(&thread2, NULL, deal_tty, &thread_para2);

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    close(fd1);
    close(fd2);

    return EXIT_SUCCESS;
}
