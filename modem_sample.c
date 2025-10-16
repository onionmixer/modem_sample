/*****************************************************************************
 * Modem Sample Program - Main Module
 *
 * Test program for external modem connected to serial port
 *****************************************************************************/

#include "modem_sample.h"
#include <stdarg.h>

/* Global Variables */
int serial_fd = -1;
int verbose_mode = 1;
volatile sig_atomic_t interrupted = 0;

/*
 * Print message with timestamp
 */
void print_message(const char *format, ...)
{
    va_list args;
    time_t now;
    char timestamp[32];
    struct tm *tm_info;

    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    printf("[%s] ", timestamp);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
}

/*
 * Print error message
 */
void print_error(const char *format, ...)
{
    va_list args;
    time_t now;
    char timestamp[32];
    struct tm *tm_info;

    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    fprintf(stderr, "[%s] ERROR: ", timestamp);

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
}

/*
 * Signal handler for cleanup
 */
void signal_handler(int sig)
{
    interrupted = 1;
    print_message("Signal %d received, cleaning up...", sig);

    if (serial_fd >= 0) {
        modem_hangup(serial_fd);
        close_serial_port(serial_fd);
        serial_fd = -1;
    }

    exit(1);
}

/*
 * Setup signal handlers
 */
void setup_signal_handlers(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
}

/*
 * Wait for RING signal and connection
 * SOFTWARE mode (S0=0): Wait for 2 RINGs, then program sends ATA
 * HARDWARE mode (S0=2): Wait for RING, modem auto-answers after 2 rings, wait for CONNECT
 */
static int wait_for_ring(int fd, int timeout, int *connected_speed)
{
    char line_buf[LINE_BUFFER_SIZE];
    int ring_count = 0;
    time_t start_time, current_time;
    int remaining_timeout;
    int rc;

    /* Initialize connected_speed */
    if (connected_speed) {
        *connected_speed = -1;
    }

    print_message("Starting serial port monitoring...");

#if MODEM_AUTOANSWER_MODE == 1
    /* HARDWARE mode: Wait for RING, modem will auto-answer */
    print_message("HARDWARE mode: Waiting for RING signal (modem will auto-answer after 2)...");
#else
    /* SOFTWARE mode: Wait for 2 RINGs to manually answer */
    print_message("SOFTWARE mode: Waiting for RING signal (need 2 times for manual answer)...");
#endif

    start_time = time(NULL);

    while (!interrupted) {
        current_time = time(NULL);
        remaining_timeout = timeout - (current_time - start_time);

        if (remaining_timeout <= 0) {
            print_error("Timeout waiting for RING/CONNECT signal");
            return ERROR_TIMEOUT;
        }

        /* Read line from serial port */
        rc = serial_read_line(fd, line_buf, sizeof(line_buf), 5); /* 5 second timeout per read */

        if (rc == ERROR_TIMEOUT) {
            /* Continue waiting */
            continue;
        } else if (rc < 0) {
            print_error("Error reading from serial port");
            return rc;
        }

        if (rc > 0) {
            print_message("Received: %s", line_buf);

#if MODEM_AUTOANSWER_MODE == 1
            /* HARDWARE mode: Check for CONNECT (modem auto-answered) */
            if (strstr(line_buf, "CONNECT") != NULL) {
                print_message("Modem auto-answered and connected: %s", line_buf);

                /* Parse connection speed from CONNECT response */
                if (connected_speed) {
                    int speed = parse_connect_speed(line_buf);
                    if (speed > 0) {
                        *connected_speed = speed;
                    }
                }

                return SUCCESS;
            }

            /* Also count RINGs for logging */
            if (detect_ring(line_buf)) {
                ring_count++;
                print_message("RING detected! (count: %d) - waiting for modem auto-answer...", ring_count);
            }
#else
            /* SOFTWARE mode: Count RINGs for manual answer */
            if (detect_ring(line_buf)) {
                ring_count++;
                print_message("RING detected! (count: %d/2)", ring_count);

                if (ring_count >= 2) {
                    print_message("RING signal detected 2 times - Ready to answer call manually");
                    return SUCCESS;
                }
            }
#endif
        }
    }

    return ERROR_TIMEOUT;
}

/*
 * Main program
 */
int main(int argc, char *argv[])
{
    int rc;

    /* Suppress unused parameter warnings */
    (void)argc;
    (void)argv;

    printf("=======================================================\n");
    printf("Modem Sample Program\n");
    printf("=======================================================\n");
    printf("Configuration:\n");
    printf("  Serial Port: %s\n", SERIAL_PORT);
    printf("  Baudrate: %d\n", BAUDRATE);
    printf("  Data Bits: %d, Parity: NONE, Stop Bits: %d\n", BIT_DATA, BIT_STOP);
    printf("  Flow Control: NONE\n");
#if MODEM_AUTOANSWER_MODE == 1
    printf("  Autoanswer Mode: HARDWARE (S0=2, modem auto-answers)\n");
#else
    printf("  Autoanswer Mode: SOFTWARE (S0=0, manual ATA)\n");
#endif
    printf("=======================================================\n\n");

    /* Setup signal handlers */
    setup_signal_handlers();

    /* STEP 1-2: Open and initialize serial port */
    serial_fd = open_serial_port(SERIAL_PORT, BAUDRATE);
    if (serial_fd < 0) {
        print_error("Failed to open serial port");
        return 1;
    }

    /* STEP 3: Send modem initialization command */
    rc = init_modem(serial_fd);
    if (rc != SUCCESS) {
        print_error("Modem initialization failed");
        goto cleanup;
    }

    /* STEP 4: Wait 2 seconds */
    print_message("Waiting 2 seconds...");
    sleep(2);

    /* STEP 5: Send modem autoanswer command */
    rc = set_modem_autoanswer(serial_fd);
    if (rc != SUCCESS) {
        print_error("Failed to set modem autoanswer");
        goto cleanup;
    }

    /* STEP 6: Wait 2 seconds */
    print_message("Waiting 2 seconds...");
    sleep(2);

    /* STEP 7: Monitor serial port for RING signal and connection */
    int connected_speed = -1;

#if MODEM_AUTOANSWER_MODE == 1
    /* HARDWARE mode: wait_for_ring() will wait for modem auto-answer and CONNECT */
    print_message("HARDWARE mode: Waiting for modem to auto-answer...");
    rc = wait_for_ring(serial_fd, RING_WAIT_TIMEOUT, &connected_speed);
    if (rc != SUCCESS) {
        print_error("Failed to detect RING/CONNECT signal");
        goto cleanup;
    }
    print_message("Modem auto-answered successfully");
#else
    /* SOFTWARE mode: wait for 2 RINGs, then send ATA manually */
    print_message("SOFTWARE mode: Waiting for RING signals...");
    rc = wait_for_ring(serial_fd, RING_WAIT_TIMEOUT, NULL);
    if (rc != SUCCESS) {
        print_error("Failed to detect RING signal");
        goto cleanup;
    }

    /* STEP 8: Answer the call with speed detection (send ATA command) */
    print_message("Answering incoming call (ATA) with speed detection...");
    rc = modem_answer_with_speed_adjust(serial_fd, &connected_speed);
    if (rc != SUCCESS) {
        print_error("Failed to answer call");
        goto cleanup;
    }
#endif

    /* STEP 8a: Dynamically adjust serial port speed to match actual connection speed */
    if (connected_speed > 0 && connected_speed != BAUDRATE) {
        print_message("Connection speed (%d bps) differs from configured speed (%d bps)",
                      connected_speed, BAUDRATE);
        print_message("Automatically adjusting to match modem connection speed...");
        rc = adjust_serial_speed(serial_fd, connected_speed);
        if (rc != SUCCESS) {
            print_error("Failed to adjust serial port speed - continuing with original speed");
            /* Continue with original speed - may cause communication issues */
        }
    } else if (connected_speed > 0) {
        print_message("Connection speed matches configured speed: %d bps", connected_speed);
    }

    /* STEP 9: Enable carrier detect after connection */
    rc = enable_carrier_detect(serial_fd);
    if (rc != SUCCESS) {
        print_message("Warning: Failed to enable carrier detect");
        /* Continue anyway - not fatal */
    }

    /* STEP 10: Wait 10 seconds after connection */
    print_message("Connection established. Waiting 10 seconds...");
    sleep(10);

    /* Add extra delay to ensure client is ready */
    print_message("Waiting additional 500ms for client stabilization...");
    usleep(500000);  /* 500ms additional delay */

    /* STEP 11: Send "first\r\n" using robust transmission */
    print_message("=== Sending 'first' message with improved transmission ===");

    /* Log what we're about to send */
    log_transmission("FIRST", "first\r\n", 7);

    /* Use robust_serial_write with carrier checking and retry logic */
    rc = robust_serial_write(serial_fd, "first\r\n", 7);
    if (rc < 0) {
        if (rc == ERROR_HANGUP) {
            print_error("Carrier lost while sending 'first' message");
        } else {
            print_error("Failed to send 'first' message (error: %d)", rc);
        }
        goto cleanup;
    }
    print_message("'first' message sent successfully: %d bytes", rc);

    /* STEP 12: Wait 5 seconds */
    print_message("Waiting 5 seconds...");
    sleep(5);

    /* Verify carrier still present before second transmission */
    print_message("Verifying carrier status before second transmission...");
    rc = verify_carrier_before_send(serial_fd);
    if (rc != SUCCESS) {
        print_error("Carrier check failed before second transmission");
        goto cleanup;
    }
    print_message("Carrier OK - proceeding with second transmission");

    /* STEP 13: Send "second\r\n" using robust transmission */
    print_message("=== Sending 'second' message with improved transmission ===");

    /* Log what we're about to send */
    log_transmission("SECOND", "second\r\n", 8);

    /* Use robust_serial_write with carrier checking and retry logic */
    rc = robust_serial_write(serial_fd, "second\r\n", 8);
    if (rc < 0) {
        if (rc == ERROR_HANGUP) {
            print_error("Carrier lost while sending 'second' message");
        } else {
            print_error("Failed to send 'second' message (error: %d)", rc);
        }
        goto cleanup;
    }
    print_message("'second' message sent successfully: %d bytes", rc);

    /* STEP 14: Disconnect modem (ATH + DTR drop) */
    print_message("Transmission complete. Disconnecting modem...");
    modem_hangup(serial_fd);

    /* If we got here, data transmission was successful */
    rc = SUCCESS;

cleanup:
    /* STEP 15: Close serial port and unlock */
    if (serial_fd >= 0) {
        close_serial_port(serial_fd);
        serial_fd = -1;
    }

    printf("\n=======================================================\n");
    if (rc == SUCCESS) {
        print_message("Program completed successfully");
        printf("=======================================================\n");
        return 0;
    } else {
        print_error("Program completed with errors");
        printf("=======================================================\n");
        return 1;
    }
}
