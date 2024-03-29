#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>

#define N 1024
#define PORT 17710

void sigint_handler(int signum);

int parentw, parentr, exit_signal = 0;
const int neg_one = -1;

// args: M, A, Q, D
int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    if (argc != 5) {
        fprintf(stderr, "\"admin\" process expected 5 arguments but received %d, Exiting.\n", argc);
        return EXIT_FAILURE;
    }
    int fds1[2], fds2[2];

	pipe(fds1);
	pipe(fds2);

	int childr = fds1[0];
	parentw = fds1[1];
	parentr = fds2[0];
	int childw = fds2[1];

	char childrs[10], childws[10];
	sprintf(childrs, "%d", childr);
	sprintf(childws, "%d", childw);
	char* args[] = {"./cal-new.exe", childrs, childws, argv[1], argv[2], argv[3], NULL};
	int pid = fork();
	if (pid == 0) {
		execvp(args[0], args);
	} else {
        fd_set mainfds, readfds;
        int maxfd, listenfd, clientfd;
        FD_ZERO(&mainfds);
        FD_ZERO(&readfds);

        listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd < 0) perror("socket() error");
        struct sockaddr_in addr;
        bzero((char*) &addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(PORT);
        int ret = bind(listenfd, (struct sockaddr*) &addr, sizeof(addr));
        if (ret < 0)  {
            perror("bind() error");
            exit(EXIT_FAILURE);
        }

        printf("listening at %s:%d\n", inet_ntoa(addr.sin_addr), PORT);

        int status = listen(listenfd, SOMAXCONN);
        if (status < 0) perror("status() error");
        FD_SET(listenfd, &mainfds);
        maxfd = listenfd;

        signal(SIGINT, sigint_handler);

        int i, j;   // loop vars
        int fd, CID, length, filenamelen, filenamebuf[80], numbuf[N];
        char filename[80];
        while (exit_signal == 0) {
            readfds = mainfds;
            if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
                if (errno != EINTR) perror("select() error %d");
            } else {
                for (fd = 0; fd <= maxfd; fd++) {
                    if (FD_ISSET(fd, &readfds)) {
                        if (fd == listenfd) {
                            struct sockaddr_in clientaddr;
                            socklen_t addrlen = sizeof(clientaddr);
                            clientfd = accept(listenfd, (struct sockaddr *) &clientaddr, &addrlen);
                            if (clientfd < 0) {
                                perror("accept() error");
                            } else {
                                FD_SET(clientfd, &mainfds);
                                if (clientfd > maxfd) maxfd = clientfd;
                            }

                        } else {
                            bzero(filename, sizeof(char) * 80);
                            if (recv(fd, &CID, sizeof(int), 0) > 0) {
                                recv(fd, &filenamelen, sizeof(int), 0);
                                recv(fd, filenamebuf, sizeof(int) * filenamelen, 0);
                                for (i = 0; i < filenamelen; i++) filename[i] = (char) filenamebuf[i];
                                recv(fd, &length, sizeof(length), 0);
                                recv(fd, &numbuf, sizeof(int) * length, 0);

                                write(parentw, &length, sizeof(int));
                                write(parentw, &CID, sizeof(int));
                                write(parentw, &filenamelen, sizeof(int));
                                write(parentw, filename, sizeof(char) * filenamelen);
                                write(parentw, &numbuf, sizeof(int) * length);
                            } else {
                                shutdown(fd, SHUT_RDWR);
                                FD_CLR(fd, &mainfds);
                            }
                        }
                    }
                }
            }
        }
        write(parentw, &neg_one, sizeof(int));
        wait(NULL);
        exit(EXIT_SUCCESS);
	}
}

void sigint_handler(int signum) {
    exit_signal = 1;
}
