#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // Validate command line arguments
    if (argc != 4) {
        fprintf(stderr, "Error: Invalid number of command line arguments.\n");
        exit(1);
    }
    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    char *file_path = argv[3];

    // Open the specified file for reading
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(1);
    }

    // Create a TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        exit(1);
    }

    // Connect to the server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Error converting IP address to binary representation");
        exit(1);
    }
    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(1);
    }

    // Send file size and contents to the server
    // Get the size of the file 
    fseek(file, 0, SEEK_END);
    int file_size = ftell(file);
    rewind(file);
    // Send the file size
    uint32_t file_size_n = htonl((uint32_t) file_size);
    if (write(sock, &file_size_n, sizeof(file_size_n)) < 0) {
        perror("Error sending file size");
        exit(1);
    }
    // Send the file contents
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (write(sock, buffer, bytes_read) < 0) {
            perror("Error sending file contents");
            exit(1);
        }
    }

    // Receive the count of printable characters from the server
    uint32_t printable_count_n;
    if (read(sock, &printable_count_n, sizeof(printable_count_n)) < 0)
    {
        perror("Error receiving printable count");
        exit(1);
    }
    uint32_t printable_count = ntohl(printable_count_n);

    // Print the number of printable characters
    printf("# of printable characters: %u\n", printable_count);
    //printf("# of printable characters: %u\n", printable_count_n);
    // Close the file and socket
    fclose(file);
    close(sock);

    // Exit with exit code 0
    exit(0);
}
