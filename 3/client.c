#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/*
The client creates a TCP connection to the server and sends it the contents
of a user-supplied file. The client then reads back the count of printable characters from the
server, prints it, and exits.
*/

int main(int argc, char *argv[]) {
    int sock;
    int bytes_read;
    FILE *file;
    char *server_ip;
    char *file_path;
    char buffer[1048576]; // Allocations of up to 1 MB are OK.
    uint16_t server_port; // Port number can be represented with a 16-bit unsigned integer. 
    uint32_t file_size; // The size of the file can be represented with a 32-bit unsigned integer.
    uint32_t file_size_n;
    uint32_t printable_count; // The count of printable characters can be represented with a 32-bit unsigned integer.
    uint32_t printable_count_n;
    struct sockaddr_in server_addr;

    // Validate command line arguments
    if (argc != 4) {
        fprintf(stderr, "Error: Invalid number of command line arguments.\n");
        exit(1);
    }

    server_ip = argv[1];
    server_port = atoi(argv[2]);
    file_path = argv[3];

    // Open the specified file for reading
    file = fopen(file_path, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(1);
    }

    // Create a TCP connection to the specified server port on the specified server IP.
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        exit(1);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    // Converting a string containing an IP address to binary representation.
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Error converting IP address to binary representation");
        exit(1);
    }
    // Connect to the server
    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(1);
    }

    // Get the size of the file 
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    rewind(file);
    // Send the server the number of bytes that will be transferred (i.e., the file size).
    file_size_n = htonl((uint32_t) file_size); // Convert from host byte order to network byte order.
    if (write(sock, &file_size_n, sizeof(file_size_n)) < 0) { // Send the file size
        perror("Error sending file size");
        exit(1);
    }
    // Sends the server the contents of the file.
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (write(sock, buffer, bytes_read) < 0) {
            perror("Error sending file contents");
            exit(1);
        }
    }

    // Receive the count of printable characters from the server
    if (read(sock, &printable_count_n, sizeof(printable_count_n)) < 0){
        perror("Error receiving printable count");
        exit(1);
    }
    printable_count = ntohl(printable_count_n); // Convert from network byte order to host byte order.

    // Print the number of printable characters
    printf("# of printable characters: %u\n", printable_count);
    
    // Close the file and socket
    fclose(file);
    close(sock);

    // Exit with exit code 0
    exit(0);
}
