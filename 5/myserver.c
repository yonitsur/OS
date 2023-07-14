#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>

#define MAX_CONNECTIONS 10
#define BUFSIZE 1024

// Initialize a data structure pcc_total that will count how many times each printable character
// was observed in all client connections. The counts are 32-bits unsigned integers.
unsigned int pcc_total[95];

int listenfd;

void handle_sigint(int signo) {
    // For every printable character, print the number of times it was 
    // observed (possibly 0) to standard output
    for (int i = 32; i <= 126; i++) {
        printf("char '%c' : %u times\n", i, pcc_total[i - 32]);
    }
    close(listenfd);
    exit(0);
}

int main(int argc, char *argv[])
{
    int count;
    int connfd    = -1;
    int  bytes_read =  0;
    char recv_buff[1024];
    struct sockaddr_in serv_addr;
    struct sockaddr_in my_addr;
    struct sockaddr_in peer_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in );
    
   
    // Check for correct number of command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    // Set up server address structure
    memset(&serv_addr, 0, addrsize);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    listenfd = socket( AF_INET, SOCK_STREAM, 0 );
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }
    // Allow immediate reuse of the port
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval,sizeof(optval));
    // Bind the socket to the local address
    if (bind(listenfd, (struct sockaddr *) &serv_addr, addrsize) < 0) {
        perror("bind");
        return 1;
    }
    // Listen to incoming TCP connections on the specified server port. 
    // Use a listen() queue of size 10.
    if (listen(listenfd, MAX_CONNECTIONS) < 0) {
        perror("listen");
        return 1;
    }
     // Set up signal handling
    signal(SIGINT, handle_sigint);
    int p =0;
    while( 1 )
    {
        connfd = accept( listenfd,(struct sockaddr*) &peer_addr,&addrsize);
        if( connfd < 0 ){
            printf("\n Error : Accept Failed. %s \n", strerror(errno));
            return 1;
        }
        unsigned int pcc_count[256];
        memset(pcc_count, 0, sizeof(pcc_count));
        // Read the number of bytes the client will send
        if (recv(connfd, &count, sizeof(count), 0) < 0) {
            perror("recv");
            return 1;
        }
        count = ntohl(count);
        printf("count: %d\n", count);
        while (count > 0) {
            bytes_read = read(connfd,recv_buff,sizeof(recv_buff) - 1);
            /////////////////
              //printf("bytes_read: %d\n", bytes_read);
            //////////////////
            if( bytes_read <= 0 )
                break;
            recv_buff[bytes_read] = '\0';
            /////////////
              //puts(recv_buff);
            /////////////
            count -= bytes_read;
        }
         printf("test here\n");
        int printable_chars_cnt = 0;
        // Count the printable characters and update pcc_total
        for (int i = 0; i < bytes_read; i++) {
            if (recv_buff[i] >= 32 && recv_buff[i] <= 126) {
                pcc_total[recv_buff[i] - 32]++;
                printable_chars_cnt++;
            }
        }
        printf("printable_chars_cnt: %d\n", printable_chars_cnt);
        // Send the count of printable characters to the client
        if (send(connfd, &printable_chars_cnt, sizeof(printable_chars_cnt), 0) < 0) {
            perror("send1");
            return 1;
        }
        
        close(connfd);

    }
}
