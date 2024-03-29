#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define N 1024

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    if (argc != 4) {
        fprintf(stderr, "\"client\" process expected 4 arguments but received %d, Exiting.\n", argc);
        return EXIT_FAILURE;
    }
    int port = (int) strtol(argv[3], NULL, 10);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) perror("socket() error");
    struct sockaddr_in servaddr;
    bzero((char*) &servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(argv[2]);

    int ret = connect(sockfd, (struct sockaddr*) &servaddr, sizeof(servaddr));
    if (ret < 0) perror("connect() error");

    char filename[80];
    int length = 0;
    int buffer[1100];
    buffer[length] = (int) strtol(argv[1], NULL, 10);

    int size = 4, i;
    char *lenbuf = (char*) malloc(size * sizeof(char));
    while(1) {
        length = 1;
        fprintf(stdout, "Enter file name or \"End\" to exit: ");
        scanf("%s", filename);

        if (strcmp(filename, "End") == 0) {
            shutdown(sockfd, SHUT_RDWR);
            return 0;
        }
        buffer[length] = (int) strlen(filename);
        length++;
        for (i = 0; i < buffer[1]; i++) {
            buffer[length] = (unsigned char) filename[i];
            length++;
        }
        FILE* fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "File \"%s\" not found\n", filename);
        } else {
            getline(&lenbuf, (size_t *) &size, fp);
            buffer[length] = (int) strtol(lenbuf, NULL, 10);
            length++;
            for (i = 0; i < buffer[length - 1]; i++) {
                fscanf(fp, "%d", &buffer[length + i]);
            }
            length += buffer[length - 1];
            send(sockfd, buffer, length*sizeof(int), 0);
        }
    }
}
