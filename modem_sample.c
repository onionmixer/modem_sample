/*
 * modem_sample.c
 * Sample implementation of modem communication functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

// Function prototypes
int modem_init(const char* device_path);
int modem_connect(int fd, const char* phone_number);
int modem_send_command(int fd, const char* command, char* response, size_t response_len);
int modem_disconnect(int fd);
void modem_cleanup(int fd);

// Initialize modem connection
int modem_init(const char* device_path) {
    int fd;
    struct termios tty;
    
    // Open serial port
    fd = open(device_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("Error opening serial port");
        return -1;
    }
    
    // Configure terminal settings
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error getting terminal attributes");
        close(fd);
        return -1;
    }
    
    // Set baud rate to 9600
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    
    // Configure for raw communication
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ISIG;
    
    tty.c_iflag &= ~IXON;
    tty.c_iflag &= ~IXOFF;
    tty.c_iflag &= ~IGNBRK;
    tty.c_iflag &= ~ISTRIP;
    tty.c_oflag &= ~OPOST;
    
    // Set timeout for read operations
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    
    // Apply settings
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error setting terminal attributes");
        close(fd);
        return -1;
    }
    
    return fd;
}

// Connect to a phone number
int modem_connect(int fd, const char* phone_number) {
    char command[256];
    char response[256];
    int result;
    
    // Send AT command to reset modem
    result = modem_send_command(fd, "AT", response, sizeof(response));
    if (result != 0) {
        fprintf(stderr, "Failed to reset modem\n");
        return -1;
    }
    
    // Set modem to answer mode
    result = modem_send_command(fd, "ATM0", response, sizeof(response));
    if (result != 0) {
        fprintf(stderr, "Failed to set modem to answer mode\n");
        return -1;
    }
    
    // Dial the phone number
    snprintf(command, sizeof(command), "ATDT%s", phone_number);
    result = modem_send_command(fd, command, response, sizeof(response));
    if (result != 0) {
        fprintf(stderr, "Failed to dial phone number %s\n", phone_number);
        return -1;
    }
    
    printf("Dialing %s...\n", phone_number);
    
    // Wait for connection
    usleep(500000);
    
    return 0;
}

// Send command and receive response
int modem_send_command(int fd, const char* command, char* response, size_t response_len) {
    char buffer[256];
    ssize_t n;
    int retries = 3;
    const char* terminator = "\r";
    
    // Send command
    write(fd, command, strlen(command));
    write(fd, terminator, strlen(terminator));
    
    // Wait for response
    usleep(100000);
    
    // Read response
    memset(buffer, 0, sizeof(buffer));
    n = read(fd, buffer, sizeof(buffer) - 1);
    
    if (n > 0) {
        // Copy response to provided buffer
        strncpy(response, buffer, response_len - 1);
        response[response_len - 1] = '\0';
        
        // If response received, check if it's OK
        if (strstr(buffer, "OK")) {
            return 0; // Success
        } else if (strstr(buffer, "ERROR")) {
            return -1; // Error
        } else {
            return 0; // Assume success if we got a response
        }
    }
    
    return -1; // No response
}

// Disconnect modem
int modem_disconnect(int fd) {
    char response[256];
    
    // Send AT command to hang up
    modem_send_command(fd, "ATH", response, sizeof(response));
    
    return 0;
}

// Cleanup modem connection
void modem_cleanup(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}