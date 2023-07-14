#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_BYTES 1024

int main(int argc, char *argv[])
{
    int  sockfd     = -1;
    int  bytes_read =  0;
    char recv_buff[MAX_BYTES];
    struct sockaddr_in serv_addr;
    struct sockaddr_in my_addr;
    struct sockaddr_in peer_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in );
   
    if (argc < 4) {
          fprintf(stderr, "Usage: %s <server IP> <server port> <filepath>\n", argv[0]);
          exit(1);
    }   
    // Open the specified file for reading.
    FILE *file = fopen(argv[3], "r");
    if (file == NULL) {
          perror("fopen");
          exit(1);
    }
    // Create a TCP connection to the specified server port on the specified server IP
    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
      printf("\n Error : Could not create socket. %s\n", strerror(errno));
      exit(1);
    }
    memset(recv_buff, 0,sizeof(recv_buff));
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2])); 
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
          fprintf(stderr, "Invalid server IP address: %s. %s\n", argv[1], strerror(errno));
          exit(1);
    }
    // connect socket to the target address
    if(connect(sockfd,(struct sockaddr*) &serv_addr,sizeof(serv_addr)) < 0){
      printf("\n Error : Connect Failed. %s \n", strerror(errno));
      exit(1);
    }
  
    // Transfer the contents of the file to the server over the TCP connection and 
    // receive the count of printable characters computed by the server.

    // (a) The client sends the server file_size (32-bit unsigned integer in network byte order),
    // the number of bytes that will be transferred (i.e. the file size).
    fseek(file, 0L, SEEK_END);
    long int file_size = ftell(file);
    rewind(file);
    unsigned int file_size_nbo = htonl(file_size);
    printf("file_size: %ld\n", file_size);
    printf("file_size_nbo: %d\n", file_size_nbo);
     // (b) The client sends the server file_size bytes (the file’s content)
    if (send(sockfd, &file_size_nbo, sizeof(file_size_nbo), 0) < 0) {
          perror("send2");
          exit(1);
    }
    if (ferror(file)) {
        perror("fread");
        exit(1);
    }
    fclose(file);

   
    while (bytes_read = fread( recv_buff, 1, MAX_BYTES, file)) {
        ///////
          printf("bytes_read: %d\n", bytes_read);
          //puts( recv_buff );
        ///////
        if (write(sockfd,  recv_buff, bytes_read) < 0) {
              perror("send3");
              exit(1);
        }
    }
    //(c) The server sends the client printable_count_chars (32-bit unsigned integer in network byte order), 
    // the number of printable characters.
    unsigned int printable_count_chars;
    if (recv(sockfd, &printable_count_chars, sizeof(printable_count_chars), 0) < 0) {
        perror("recv");
        exit(1);
    }
    // Print the number of printable characters obtained to stdout.
    printf("# of printable characters: %u\n", printable_count_chars);

    close(sockfd);
    exit(0);
  }
 
  