#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>

/*
The server accepts TCP connections from clients. A client that connects
sends the server a stream of N bytes (N is determined by the client and is not a global constant).
The server counts the number of printable characters in the stream (a printable character is a
byte b whose value is 32 ≤ b ≤ 126). Once the stream ends, the server sends the count back to
the client over the same connection. In addition, the server maintains a data structure in which it
counts the number of times each printable character was observed in all the connections. When
the server receives a SIGINT, it prints these counts and exits.
*/

// Global data structure to count the number of times each printable character was observed
// in all client connections. The counts are 32-bits unsigned integers.
uint32_t pcc_total[95];

// Flag to indicate if the server received a SIGINT signal
volatile sig_atomic_t sigint_received = 0;

// Handler for the SIGINT signal
void sigint_handler(int sig) {
    sigint_received = 1;
}

int main(int argc, char *argv[]) {
    int sock;
    int client_err_flag = 0;  // Flag to indicate a TCP errors or unexpected connection terminate from the client
    int client_sock;
    int bytes_read;
    uint32_t pcc_count[95]; // Local data structure to count the number of times each printable character was observed in a client connection
    char buffer[1048576]; // Allocations of up to 1 MB are OK.
    uint16_t server_port; // Port number can be represented with a 16-bit unsigned integer.
    uint32_t file_size; // The size of the file can be represented with a 32-bit unsigned integer.
    uint32_t file_size_n; //  Size of the file as a 32-bit unsigned integer network byte order
    uint32_t printable_count; // The count of printable characters can be represented with a 32-bit unsigned integer.
    uint32_t printable_count_n; // The count of printable characters as a 32-bit unsigned integer network byte order
    struct sigaction sa;
    struct sockaddr_in server_addr;
    
    // Validate command line arguments
    if (argc != 2) {
        fprintf(stderr, "Error: Invalid number of command line arguments.\n");
        exit(1);
    }
    server_port = atoi(argv[1]);

    // Initialize pcc_total data structure
    memset(pcc_total, 0, sizeof(pcc_total));

    // Register the SIGINT signal handler
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    // Create a TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        exit(1);
    }

    // Enable reusing the port quickly after the server terminates
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Error setting socket option SO_REUSEADDR");
        exit(1);
    }

    // Bind the socket to the specified port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections on all network interfaces
    server_addr.sin_port = htons((uint16_t) server_port); // Convert from host byte order to network byte order (server)
    if (bind(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(1);
    }

    // Listen for incoming TCP connections on the specified server port, using a queue of size 10.
    if (listen(sock, 10) < 0) {
        perror("Error listening for connections");
        exit(1);
    }

    // Enter the main loop
    while (!sigint_received) {
        client_err_flag = 0;
        // Initialize pcc_count data structure
        memset(pcc_count, 0, sizeof(pcc_total));
        // Accept a new TCP connection
        client_sock = accept(sock, NULL, NULL);
        if (client_sock < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal, check if it was a SIGINT
                continue;
            }
            perror("Error accepting connection");
            exit(1);
        }
       
        // Read the file size from the client
        if (read(client_sock, &file_size_n, sizeof(file_size_n)) < 0) {
            perror("Error receiving file size");
            if(errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE || errno == EOF)
                client_err_flag = 1;
            else
                exit(1);
        }
        file_size = ntohl(file_size_n); // Convert from network byte order to host byte order

        // Read and process the file content's stream of bytes from the client
        printable_count = 0;
        while (file_size > 0) {
            bytes_read = read(client_sock, buffer, sizeof(buffer));
            if (bytes_read < 0) {
                perror("Error receiving file contents");
                if(errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE || errno == EOF)
                    client_err_flag = 1;
                else
                    exit(1);
            }
            // Compute count of printable characters and updates the pcc_count data structure.
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] >= 32 && buffer[i] <= 126) {
                    pcc_count[buffer[i] - 32]++;
                    printable_count++;
                }
            }
            file_size -= bytes_read;
        }
        // Send the result of printable count to the client over the TCP connection.
        printable_count_n = htonl(printable_count); // Convert from host byte order to network byte order.
        if (write(client_sock, &printable_count_n, sizeof(printable_count_n)) < 0) {
            perror("Error sending printable count");
            if(errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE || errno == EOF)
                client_err_flag = 1;
            else
                exit(1);
        }
       
        // After sending the result to the client, updates the pcc_total global data structure. 
        // if a TCP error or unexpected connection terminate from the client occurred,
        // the server should not update the pcc_total data structure.
        // (not handling overflow of the pcc_total counters).
        for (int i = 0; i < 95 && !client_err_flag; i++) {
            pcc_total[i] += pcc_count[i];
        }

        // Close the client connection
        close(client_sock);

        // Ready to accept a new connection
    }
    
    // Print the statistics of printable characters observed
    for (int i = 0; i < 95; i++) {
        printf("char '%c' : %u times\n", i + 32, pcc_total[i]);
    }

    // Close the server socket and exit
    close(sock);
    exit(0);
}
