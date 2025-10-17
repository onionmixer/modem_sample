/*****************************************************************************
 * Serial Port Handler Module
 * Based on MBSE BBS mbcico/openport.c and mbcico/ttyio.c
 *****************************************************************************/

#include "modem_sample.h"
#include <sys/stat.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static struct termios saved_tios;
static struct termios current_tios;
static int port_opened = 0;
static char lock_file[PATH_MAX] = {0};

/*
 * Convert baudrate integer to speed_t
 * Reference: mbcico/openport.c transpeed()
 */
static speed_t get_baudrate(int speed)
{
    switch (speed) {
        case 300:    return B300;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:
            print_error("Unsupported baudrate %d, using 9600", speed);
            return B9600;
    }
}

/*
 * Open and configure serial port
 * Reference: mbcico/openport.c openport() and tty_raw()
 */
int open_serial_port(const char *device, int baudrate)
{
    int fd;
    speed_t baud;

    print_message("Opening serial port: %s at %d baud", device, baudrate);

    /* Lock port first to prevent conflicts */
    if (lock_port(device) != SUCCESS) {
        return ERROR_PORT;
    }

    /* Open the serial port */
    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        print_error("Failed to open %s: %s", device, strerror(errno));
        return ERROR_PORT;
    }

    /* Get current terminal attributes and save them */
    if (tcgetattr(fd, &saved_tios) < 0) {
        print_error("tcgetattr failed: %s", strerror(errno));
        close(fd);
        return ERROR_PORT;
    }

    /* Configure port for raw 8N1 mode
     * Reference: mbcico/openport.c tty_raw()
     */
    current_tios = saved_tios;

    /* Input flags - disable all input processing */
    current_tios.c_iflag = 0;

    /* Output flags - enable output processing with CR to CR-LF conversion */
    current_tios.c_oflag = OPOST | ONLCR;

    /* Control flags - 8N1, enable receiver, hangup on close, local mode */
    current_tios.c_cflag &= ~(CSTOPB | PARENB | PARODD);
    current_tios.c_cflag |= CS8 | CREAD | HUPCL | CLOCAL;

    /* Local flags - raw mode */
    current_tios.c_lflag = 0;

    /* Control characters */
    current_tios.c_cc[VMIN] = 1;   /* Read at least 1 character */
    current_tios.c_cc[VTIME] = 0;  /* No timeout */

    /* Set baudrate */
    baud = get_baudrate(baudrate);
    cfsetispeed(&current_tios, baud);
    cfsetospeed(&current_tios, baud);

    /* Apply settings */
    if (tcsetattr(fd, TCSADRAIN, &current_tios) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        close(fd);
        return ERROR_PORT;
    }

    /* Set to blocking mode */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        flags &= ~O_NONBLOCK;
        fcntl(fd, F_SETFL, flags);
    }

    port_opened = 1;
    print_message("Serial port opened successfully");

    return fd;
}

/*
 * Close serial port and restore settings
 * Reference: mbcico/openport.c closeport()
 */
void close_serial_port(int fd)
{
    if (fd < 0 || !port_opened)
        return;

    print_message("Closing serial port");

    /* Flush buffers */
    tcflush(fd, TCIOFLUSH);

    /* Restore original terminal settings */
    tcsetattr(fd, TCSAFLUSH, &saved_tios);

    close(fd);
    port_opened = 0;

    /* Unlock port */
    unlock_port();
}

/*
 * Write data to serial port
 * Reference: mbcico/ttyio.c tty_write()
 */
int serial_write(int fd, const char *data, int len)
{
    int result;

    if (fd < 0 || !data || len <= 0)
        return ERROR_GENERAL;

    result = write(fd, data, len);

    if (result != len) {
        if (errno == EPIPE || errno == ECONNRESET) {
            print_error("Serial port hangup during write");
            return ERROR_PORT;
        } else {
            print_error("Write error: %s", strerror(errno));
            return ERROR_GENERAL;
        }
    }

    /* Wait for data to be transmitted */
    tcdrain(fd);

    return result;
}

/*
 * Read data from serial port with timeout
 * Reference: mbcico/ttyio.c tty_read()
 */
int serial_read(int fd, char *buffer, int size, int timeout)
{
    fd_set readfds, exceptfds;
    struct timeval tv;
    int rc;

    if (fd < 0 || !buffer || size <= 0)
        return ERROR_GENERAL;

    /* Setup select() */
    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
    FD_SET(fd, &readfds);
    FD_SET(fd, &exceptfds);

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    rc = select(fd + 1, &readfds, NULL, &exceptfds, &tv);

    if (rc < 0) {
        print_error("select() failed: %s", strerror(errno));
        return ERROR_GENERAL;
    } else if (rc == 0) {
        /* Timeout */
        return ERROR_TIMEOUT;
    }

    /* Check for exceptions */
    if (FD_ISSET(fd, &exceptfds)) {
        print_error("Exception on serial port");
        return ERROR_PORT;
    }

    /* Read data */
    if (FD_ISSET(fd, &readfds)) {
        rc = read(fd, buffer, size);

        if (rc <= 0) {
            if (errno == EPIPE || errno == ECONNRESET) {
                print_error("Serial port hangup during read");
                return ERROR_PORT;
            } else {
                print_error("Read error: %s", strerror(errno));
                return ERROR_GENERAL;
            }
        }

        return rc;
    }

    return 0;
}

/*
 * Read a line from serial port (until \n or \r)
 * Useful for reading modem responses
 *
 * Improved version with internal buffering to handle fragmented data:
 * - Uses static buffer to accumulate data across multiple reads
 * - Reads data in larger chunks (up to 128 bytes at once)
 * - Returns complete lines only when line terminator is found
 * - Preserves incomplete data in buffer for next call
 */
int serial_read_line(int fd, char *buffer, int size, int timeout)
{
    static char read_buffer[2048];     /* Internal buffer for accumulating data */
    static size_t buf_pos = 0;        /* Current position in read_buffer */
    static size_t buf_len = 0;        /* Amount of data in read_buffer */

    char chunk[128];                  /* Temporary buffer for reading chunks */
    time_t start_time, current_time;
    int remaining_timeout;
    int rc;
    size_t i;

    if (fd < 0 || !buffer || size <= 0)
        return ERROR_GENERAL;

    start_time = time(NULL);
    buffer[0] = '\0';

    while (1) {
        /* First, check if we have a complete line in the buffer */
        for (i = buf_pos; i < buf_len; i++) {
            char c = read_buffer[i];

            /* Check for line terminators */
            if (c == '\n' || c == '\r') {
                /* Found line terminator - copy line to output buffer */
                size_t copy_len = i - buf_pos;
                if (copy_len > (size_t)(size - 1)) {
                    copy_len = (size_t)(size - 1);
                }

                if (copy_len > 0) {
                    memcpy(buffer, &read_buffer[buf_pos], copy_len);
                }
                buffer[copy_len] = '\0';

                /* Skip the line terminator(s) */
                buf_pos = i + 1;

                /* Skip additional CR/LF if present */
                while (buf_pos < buf_len &&
                       (read_buffer[buf_pos] == '\r' || read_buffer[buf_pos] == '\n')) {
                    buf_pos++;
                }

                /* If we consumed all buffered data, reset buffer */
                if (buf_pos >= buf_len) {
                    buf_pos = 0;
                    buf_len = 0;
                }

                return (int)copy_len;
            }
        }

        /* No complete line found - need to read more data */
        current_time = time(NULL);
        remaining_timeout = timeout - (current_time - start_time);

        if (remaining_timeout <= 0) {
            /* Timeout - return any partial data we have */
            if (buf_len > buf_pos) {
                size_t copy_len = buf_len - buf_pos;
                if (copy_len > (size_t)(size - 1)) {
                    copy_len = (size_t)(size - 1);
                }
                memcpy(buffer, &read_buffer[buf_pos], copy_len);
                buffer[copy_len] = '\0';
                buf_pos = 0;
                buf_len = 0;
                return ERROR_TIMEOUT;
            }
            return ERROR_TIMEOUT;
        }

        /* Compact buffer if needed (move remaining data to beginning) */
        if (buf_pos > 0) {
            if (buf_len > buf_pos) {
                memmove(read_buffer, &read_buffer[buf_pos], buf_len - buf_pos);
                buf_len -= buf_pos;
            } else {
                buf_len = 0;
            }
            buf_pos = 0;
        }

        /* Check if buffer is full without finding line terminator */
        if (buf_len >= sizeof(read_buffer) - 1) {
            /* Buffer overflow - return what we have and reset */
            size_t copy_len = (buf_len > (size_t)(size - 1)) ? (size_t)(size - 1) : buf_len;
            memcpy(buffer, read_buffer, copy_len);
            buffer[copy_len] = '\0';
            buf_pos = 0;
            buf_len = 0;
            return (int)copy_len;
        }

        /* Read more data into temporary chunk buffer */
        rc = serial_read(fd, chunk, sizeof(chunk) - 1,
                        (remaining_timeout > 1) ? 1 : remaining_timeout);

        if (rc < 0) {
            /* Error during read */
            if (rc != ERROR_TIMEOUT) {
                /* Real error - return any buffered data or the error */
                if (buf_len > buf_pos) {
                    size_t copy_len = buf_len - buf_pos;
                    if (copy_len > (size_t)(size - 1)) {
                        copy_len = (size_t)(size - 1);
                    }
                    memcpy(buffer, &read_buffer[buf_pos], copy_len);
                    buffer[copy_len] = '\0';
                    buf_pos = 0;
                    buf_len = 0;
                    return rc;
                }
                return rc;
            }
            /* Timeout on this read - continue loop to check overall timeout */
            continue;
        } else if (rc > 0) {
            /* Got some data - append to buffer */
            size_t space_left = sizeof(read_buffer) - buf_len;
            size_t copy_len = ((size_t)rc > space_left) ? space_left : (size_t)rc;

            if (copy_len > 0) {
                memcpy(&read_buffer[buf_len], chunk, copy_len);
                buf_len += copy_len;
            }

            /* Continue loop to check if we now have a complete line */
        }
    }

    /* Should never reach here */
    return ERROR_GENERAL;
}

/*
 * Flush input buffer
 * Reference: mbcico/ttyio.c tty_flushin()
 */
void serial_flush_input(int fd)
{
    if (fd >= 0) {
        tcflush(fd, TCIFLUSH);
    }
}

/*
 * Flush output buffer
 * Reference: mbcico/ttyio.c tty_flushout()
 */
void serial_flush_output(int fd)
{
    if (fd >= 0) {
        tcflush(fd, TCOFLUSH);
    }
}

/*
 * Check if data is available for reading
 * Reference: mbcico/ttyio.c tty_check()
 */
int serial_check_available(int fd)
{
    fd_set readfds;
    struct timeval tv;

    if (fd < 0)
        return 0;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    return select(fd + 1, &readfds, NULL, NULL, &tv) > 0;
}

/*
 * Enable carrier detect (DCD) monitoring
 * Reference: mbcico/openport.c nolocalport() and process_sample.txt:64-94
 */
int enable_carrier_detect(int fd)
{
    struct termios tios;
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Enabling carrier detect (DCD monitoring)...");

    /* Get current settings */
    if ((rc = tcgetattr(fd, &tios)) < 0) {
        print_error("tcgetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Disable CLOCAL - enable carrier detect */
    tios.c_cflag &= ~CLOCAL;

    /* Enable RTS/CTS hardware flow control */
    tios.c_cflag |= CRTSCTS;

    /* Apply settings */
    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    print_message("Carrier detect enabled - DCD signal will be monitored");
    return SUCCESS;
}

/*
 * Perform DTR drop hangup
 * Reference: mbcico/openport.c tty_local() and process_sample.txt:206-217
 */
int dtr_drop_hangup(int fd)
{
    struct termios tios;
    speed_t saved_ispeed, saved_ospeed;
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Performing DTR drop hangup...");

    /* Get current settings */
    if ((rc = tcgetattr(fd, &tios)) < 0) {
        print_message("Warning: tcgetattr failed: %s (continuing anyway)", strerror(errno));
        return SUCCESS;  /* Not fatal - carrier may already be lost */
    }

    /* Save current speeds */
    saved_ispeed = cfgetispeed(&tios);
    saved_ospeed = cfgetospeed(&tios);

    /* Set speed to 0 to drop DTR */
    cfsetispeed(&tios, B0);
    cfsetospeed(&tios, B0);

    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_message("Warning: tcsetattr (DTR drop) failed: %s (continuing anyway)", strerror(errno));
        return SUCCESS;  /* Not fatal */
    }

    print_message("DTR dropped - waiting 1 second...");

    /* Wait for modem to recognize hangup */
    sleep(1);

    /* Restore original speeds (may fail if carrier is lost) */
    cfsetispeed(&tios, saved_ispeed);
    cfsetospeed(&tios, saved_ospeed);

    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        /* This is expected if the carrier is already lost */
        print_message("Note: DTR restore skipped (carrier already dropped)");
        return SUCCESS;  /* Not an error - connection is already closed */
    }

    print_message("DTR drop hangup completed");
    return SUCCESS;
}

/*
 * Lock serial port using UUCP-style lock files
 * Reference: mbcico/ulock.c and mbtask/ports.c check_ports()
 */
int lock_port(const char *device)
{
    FILE *fp;
    pid_t pid;
    const char *devname;

    /* Extract device name (ttyUSB0 from /dev/ttyUSB0) */
    devname = strrchr(device, '/');
    if (devname)
        devname++;
    else
        devname = device;

    /* Create lock file path */
    snprintf(lock_file, sizeof(lock_file), "/var/lock/LCK..%s", devname);

    /* Check if lock file exists */
    if (access(lock_file, F_OK) == 0) {
        /* Read PID from lock file */
        fp = fopen(lock_file, "r");
        if (fp) {
            if (fscanf(fp, "%d", &pid) == 1) {
                /* Check if process is still running */
                if (kill(pid, 0) == 0) {
                    fclose(fp);
                    print_error("Port locked by process %d", pid);
                    return ERROR_PORT;
                } else {
                    /* Stale lock - remove it */
                    print_message("Removing stale lock file (PID %d not running)", pid);
                    unlink(lock_file);
                }
            }
            fclose(fp);
        }
    }

    /* Create lock file */
    fp = fopen(lock_file, "w");
    if (!fp) {
        /* If we can't create lock file (no permissions), just warn and continue */
        print_message("Warning: Cannot create lock file %s: %s", lock_file, strerror(errno));
        print_message("Continuing without port locking...");
        lock_file[0] = '\0';
        return SUCCESS;
    }

    fprintf(fp, "%10d\n", getpid());
    fclose(fp);

    print_message("Port locked: %s", lock_file);
    return SUCCESS;
}

/*
 * Unlock serial port
 */
void unlock_port(void)
{
    if (lock_file[0] != '\0') {
        if (unlink(lock_file) == 0) {
            print_message("Port unlocked: %s", lock_file);
        }
        lock_file[0] = '\0';
    }
}

/*
 * Check carrier (DCD) status
 * Reference: MBSE mbcico/openport.c and ioctl TIOCMGET
 * Returns: 1 if carrier present, 0 if not, -1 on error
 */
int check_carrier_status(int fd)
{
    int status;

    if (fd < 0)
        return ERROR_GENERAL;

    if (ioctl(fd, TIOCMGET, &status) < 0) {
        print_error("ioctl TIOCMGET failed: %s", strerror(errno));
        return ERROR_GENERAL;
    }

    return (status & TIOCM_CAR) ? 1 : 0;
}

/*
 * Verify carrier before transmission
 * Reference: MBSE pattern of checking carrier before write operations
 */
int verify_carrier_before_send(int fd)
{
    int carrier;

    carrier = check_carrier_status(fd);

    if (carrier < 0) {
        print_error("Failed to check carrier status");
        return ERROR_PORT;
    }

    if (carrier == 0) {
        print_error("Carrier lost - cannot transmit");
        return ERROR_HANGUP;
    }

    return SUCCESS;
}

/*
 * Robust serial write with retry logic
 * Reference: MBSE mbcico/ttyio.c tty_write() patterns with error handling
 * Implements:
 * - Partial write handling (result != size)
 * - Retry on EAGAIN/EWOULDBLOCK
 * - Carrier status checking
 * - EPIPE/ECONNRESET detection
 */
int robust_serial_write(int fd, const char *data, int len)
{
    int sent = 0;
    int retry = 0;
    int rc;

    if (fd < 0 || !data || len <= 0)
        return ERROR_GENERAL;

    /* Check carrier before starting transmission */
    if (verify_carrier_before_send(fd) != SUCCESS) {
        return ERROR_HANGUP;
    }

    while (sent < len && retry < config.max_write_retry) {
        rc = write(fd, data + sent, len - sent);

        if (rc < 0) {
            /* Check for hangup conditions (from MBSE ttyio.c:256-258) */
            if (errno == EPIPE || errno == ECONNRESET) {
                print_error("Connection hangup during write (errno=%d)", errno);
                return ERROR_HANGUP;
            }

            /* Retry on temporary errors */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                print_message("Write would block, retry %d/%d", retry + 1, config.max_write_retry);
                usleep(config.retry_delay_us);
                retry++;
                continue;
            }

            /* Other errors are fatal */
            print_error("Write error: %s", strerror(errno));
            return ERROR_GENERAL;
        }

        /* Successful write */
        sent += rc;
        retry = 0;  /* Reset retry counter on success */

        /* Check for partial write */
        if (rc < (len - sent)) {
            print_message("Partial write: sent %d of %d bytes, continuing...", sent, len);
        }
    }

    if (sent < len) {
        print_error("Failed to send all data after %d retries: sent %d of %d bytes",
                    config.max_write_retry, sent, len);
        return ERROR_GENERAL;
    }

    /* Wait for data to be transmitted (from our current serial_write) */
    tcdrain(fd);

    return sent;
}

/*
 * Buffered send for large data transfers
 * Reference: Pattern for chunked transmission to avoid buffer overflows
 * Sends data in chunks with delays between chunks
 */
int buffered_serial_send(int fd, const char *data, int len)
{
    int sent = 0;
    int chunk_size;
    int rc;

    if (fd < 0 || !data || len <= 0)
        return ERROR_GENERAL;

    print_message("Buffered send: %d bytes in chunks of %d", len, config.tx_chunk_size);

    while (sent < len) {
        /* Calculate chunk size */
        chunk_size = (len - sent > config.tx_chunk_size) ? config.tx_chunk_size : (len - sent);

        /* Send chunk using robust write */
        rc = robust_serial_write(fd, data + sent, chunk_size);

        if (rc < 0) {
            print_error("Chunk transmission failed at offset %d", sent);
            return rc;
        }

        sent += rc;

        /* Delay between chunks to avoid overwhelming receiver */
        if (sent < len) {
            usleep(config.tx_chunk_delay_us);
        }

        /* Progress indicator for large transfers */
        if (len > config.tx_chunk_size * 4) {  /* Only for large transfers */
            print_message("Progress: %d/%d bytes (%.1f%%)",
                         sent, len, (100.0 * sent) / len);
        }
    }

    print_message("Buffered send completed: %d bytes", sent);
    return sent;
}

/*
 * Log transmission data (hex dump)
 * Useful for debugging and monitoring data transmission
 */
void log_transmission(const char *label, const char *data, int len)
{
    int i;
    int display_len = (len > 32) ? 32 : len;

    printf("[TX-LOG] %s: %d bytes: ", label, len);

    for (i = 0; i < display_len; i++) {
        printf("%02X ", (unsigned char)data[i]);
        if ((i + 1) % 16 == 0 && i < display_len - 1)
            printf("\n         ");
    }

    if (len > 32) {
        printf("... (%d more bytes)", len - 32);
    }

    printf("\n");
    fflush(stdout);
}

/*
 * Wait for client ready signal
 * Implements handshake pattern - wait for specific string from client
 * Reference: Pattern for synchronization before data transmission
 */
int wait_for_client_ready(int fd, const char *ready_string, int timeout)
{
    char buf[128];
    time_t start = time(NULL);
    int rc;

    if (!ready_string) {
        print_error("wait_for_client_ready: ready_string is NULL");
        return ERROR_GENERAL;
    }

    print_message("Waiting for client ready signal: '%s' (timeout: %ds)",
                  ready_string, timeout);

    while (time(NULL) - start < timeout) {
        rc = serial_read_line(fd, buf, sizeof(buf), 1);

        if (rc > 0) {
            print_message("Received from client: %s", buf);

            if (strstr(buf, ready_string) != NULL) {
                print_message("Client ready signal detected!");
                return SUCCESS;
            }
        } else if (rc == ERROR_HANGUP) {
            print_error("Connection lost while waiting for ready signal");
            return ERROR_HANGUP;
        }

        /* Check for interruption */
        if (interrupted) {
            print_message("Interrupted while waiting for client");
            return ERROR_GENERAL;
        }
    }

    print_error("Timeout waiting for client ready signal");
    return ERROR_TIMEOUT;
}

/*
 * Dynamically adjust serial port speed
 * Used after CONNECT to match the actual modem connection speed
 */
int adjust_serial_speed(int fd, int new_baudrate)
{
    struct termios tios;
    speed_t new_speed;
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Adjusting serial port speed to %d bps", new_baudrate);

    /* Get current settings */
    if ((rc = tcgetattr(fd, &tios)) < 0) {
        print_error("tcgetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Convert baudrate to speed_t */
    new_speed = get_baudrate(new_baudrate);

    /* Set new speed */
    cfsetispeed(&tios, new_speed);
    cfsetospeed(&tios, new_speed);

    /* Flush buffers before changing speed */
    tcflush(fd, TCIOFLUSH);

    /* Apply new settings */
    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Wait a moment for speed change to take effect */
    usleep(100000); /* 100ms */

    print_message("Serial port speed adjusted to %d bps successfully", new_baudrate);
    return SUCCESS;
}
