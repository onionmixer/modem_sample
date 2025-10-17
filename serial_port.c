/*
 * serial_port.c
 * Serial port communication functions for modem connection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>

// Function to configure serial port
int configure_serial_port(const char* device_path, int baud_rate) {
    int fd;
    struct termios tty;
    
    // Open serial port
    fd = open(device_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("Error opening serial port");
        return -1;
    }
    
    // Get current terminal attributes
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error getting terminal attributes");
        close(fd);
        return -1;
    }
    
    // Change baud rate
    cfsetospeed(&tty, baud_rate);
    cfsetispeed(&tty, baud_rate);
    
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
    
    // Set timeout for read
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // 1 second timeout
    
    // Apply settings
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error setting terminal attributes");
        close(fd);
        return -1;
    }
    
    return fd;
}

// Function to send data to serial port
int serial_send(int fd, const char* data, size_t length) {
    ssize_t n;
    
    n = write(fd, data, length);
    if (n < 0) {
        perror("Error writing to serial port");
        return -1;
    }
    
    return n;
}

// Function to receive data from serial port
int serial_receive(int fd, char* buffer, size_t buffer_size, int timeout_seconds) {
    fd_set readfds;
    struct timeval timeout;
    ssize_t n;
    int result;
    
    // Set up the timeout
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;
    
    // Set up file descriptor set
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    // Wait for data to be available
    result = select(fd + 1, &readfds, NULL, NULL, &timeout);
    
    if (result < 0) {
        perror("Error in select");
        return -1;
    } else if (result == 0) {
        // Timeout
        return 0;
    }
    
    if (FD_ISSET(fd, &readfds)) {
        n = read(fd, buffer, buffer_size - 1);
        if (n < 0) {
            perror("Error reading from serial port");
            return -1;
        }
        
        buffer[n] = '\0';
        return n;
    }
    
    return -1;
}

// Function to send AT command and receive response
int send_at_command(int fd, const char* command, char* response, size_t response_size, int timeout) {
    char terminator[2] = "\r";
    char full_command[256];
    int result;
    
    // Format command with CR terminator
    snprintf(full_command, sizeof(full_command), "%s%s", command, terminator);
    
    // Send command
    result = serial_send(fd, full_command, strlen(full_command));
    if (result < 0) {
        return -1;
    }
    
    // Read response
    result = serial_receive(fd, response, response_size, timeout);
    if (result < 0) {
        return -1;
    }
    
    return result;
}

// Function to check if modem is responding
int modem_test_connection(int fd) {
    char response[256];
    
    // Send basic AT command
    if (send_at_command(fd, "AT", response, sizeof(response), 3) > 0) {
        // Check if we got a proper response
        if (strstr(response, "OK") != NULL || strstr(response, "ERROR") != NULL) {
            return 1; // Modem is responding
        }
    }
    
    return 0; // Modem not responding
}

// Function to wait for specific response from modem
int wait_for_response(int fd, const char* expected_response, char* response, size_t response_size, int timeout_seconds) {
    char buffer[256];
    int total_read = 0;
    int remaining_timeout = timeout_seconds;
    int result;
    
    while (remaining_timeout > 0) {
        // Read data
        result = serial_receive(fd, buffer, sizeof(buffer) - 1, 1);
        
        if (result > 0) {
            buffer[result] = '\0';
            strcat(response, buffer);
            total_read += result;
            
            // Check if we found the expected response
            if (strstr(response, expected_response) != NULL) {
                return 0; // Success
            }
        } else if (result < 0) {
            return -1; // Error
        }
        
        remaining_timeout--;
    }
    
    return -1; // Timeout
}