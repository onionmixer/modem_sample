/*****************************************************************************
 * Modem Sample Program - Main Module
 *
 * Test program for external modem connected to serial port
 *****************************************************************************/

#include "modem_sample.h"
#include <stdarg.h>

/* Global Variables */
int serial_fd = -1;
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
    char line_buf[config.line_buffer_size];
    int ring_count = 0;
    time_t start_time, current_time, last_ring_time = 0;
    int remaining_timeout;
    int rc;

    /* Initialize connected_speed */
    if (connected_speed) {
        *connected_speed = -1;
    }

    if (config.enable_timing_log) {
        print_message("Starting serial port monitoring...");
    }

  if (config.autoanswer_mode == 1) {
        /* HARDWARE mode: Wait for RING, modem will auto-answer */
        print_message("HARDWARE mode: Waiting for RING signal (modem will auto-answer after 2 rings)...");
        if (config.enable_timing_log) {
            print_message("Enhanced logging: Tracking RING timing and modem response patterns");
        }
    } else {
        /* SOFTWARE mode: Wait for 2 RINGs to manually answer */
        print_message("SOFTWARE mode: Waiting for RING signal (need 2 times for manual answer)...");
    }

    start_time = time(NULL);

    while (!interrupted) {
        current_time = time(NULL);
        remaining_timeout = timeout - (current_time - start_time);

        if (remaining_timeout <= 0) {
            print_error("Timeout waiting for RING/CONNECT signal after %d seconds", timeout);
            return ERROR_TIMEOUT;
        }

        /* Additional timeout check if we've received RINGs but no CONNECT */
        if (last_ring_time > 0 && (current_time - last_ring_time) > config.connect_timeout) {
            print_error("Timeout: RINGs received but no CONNECT after %d seconds", config.connect_timeout);
            return ERROR_TIMEOUT;
        }

        /* Read line from serial port */
        rc = serial_read_line(fd, line_buf, config.line_buffer_size, 5); /* 5 second timeout per read */

        if (rc == ERROR_TIMEOUT) {
            /* Continue waiting */
            continue;
        } else if (rc < 0) {
            print_error("Error reading from serial port");
            return rc;
        }

        if (rc > 0) {
            print_message("Received: %s", line_buf);

            if (config.autoanswer_mode == 1) {
                /* HARDWARE mode: Enhanced CONNECT detection with detailed logging */
                if (strstr(line_buf, "CONNECT") != NULL) {
                    int elapsed_time = current_time - start_time;
                    print_message("=== MODEM AUTO-ANSWER DETECTED ===");
                    print_message("CONNECT response: %s", line_buf);
                    if (config.enable_timing_log) {
                        print_message("Total time from start: %d seconds", elapsed_time);
                        print_message("RING count received: %d", ring_count);
                    }

                    /* Parse connection speed from CONNECT response */
                    if (connected_speed) {
                        int speed = parse_connect_speed(line_buf);
                        if (speed > 0) {
                            *connected_speed = speed;
                            print_message("Detected connection speed: %d bps", speed);
                        } else {
                            print_message("Warning: Could not parse speed from CONNECT response");
                        }
                    }

                    print_message("Hardware auto-answer sequence completed successfully");
                    return SUCCESS;
                }

                /* Enhanced RING detection with timing analysis */
                if (detect_ring(line_buf)) {
                    ring_count++;
                    current_time = time(NULL);

                    if (last_ring_time == 0) {
                        if (config.enable_timing_log) {
                            print_message("=== FIRST RING DETECTED ===");
                            print_message("RING #%d at %ld seconds from start", ring_count, current_time - start_time);
                            print_message("Modem should auto-answer after RING #2 (S0=2)");
                        }
                    } else {
                        int ring_interval = current_time - last_ring_time;
                        if (config.enable_timing_log) {
                            print_message("RING #%d detected (interval: %d seconds from previous RING)",
                                         ring_count, ring_interval);

                            if (ring_count == 2) {
                                print_message("=== SECOND RING DETECTED ===");
                                print_message("Modem should auto-answer NOW (S0=2 configuration)");
                                print_message("Waiting for CONNECT response...");
                            }
                        }
                    }

                    last_ring_time = current_time;
                }

                /* Enhanced detection of potential connection issues */
                if (strstr(line_buf, "NO CARRIER") != NULL) {
                    print_error("Connection lost during ringing phase: %s", line_buf);
                    return ERROR_MODEM;
                }
                if (strstr(line_buf, "BUSY") != NULL) {
                    print_error("Line busy during ringing phase: %s", line_buf);
                    return ERROR_MODEM;
                }
                if (strstr(line_buf, "ERROR") != NULL) {
                    print_error("Modem error during ringing phase: %s", line_buf);
                    return ERROR_MODEM;
                }
            } else {
                /* SOFTWARE mode: Count RINGs for manual answer */
                if (detect_ring(line_buf)) {
                    ring_count++;
                    print_message("RING detected! (count: %d/2)", ring_count);

                    if (ring_count >= 2) {
                        print_message("RING signal detected 2 times - Ready to answer call manually");
                        return SUCCESS;
                    }
                }
            }
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
    printf("  Serial Port: %s\n", config.serial_port);
    printf("  Baudrate: %d\n", config.baudrate);
    printf("  Data Bits: %d, Parity: %s, Stop Bits: %d\n", config.data_bits, config.parity, config.stop_bits);
    printf("  Flow Control: %s\n", config.flow_control);
    if (config.autoanswer_mode == 1) {
        printf("  Autoanswer Mode: HARDWARE (S0=2, modem auto-answers)\n");
    } else {
        printf("  Autoanswer Mode: SOFTWARE (S0=0, manual ATA)\n");
    }
    printf("=======================================================\n\n");

    /* Setup signal handlers */
    setup_signal_handlers();

    /* STEP 1: Load configuration */
    const char *config_file = "modem_sample.conf";
    rc = load_config(config_file);
    if (rc != SUCCESS) {
        print_message("Using default configuration");
        init_default_config();
    }

    if (config.verbose_mode) {
        print_config();
    }

    /* STEP 2-3: Open and initialize serial port */
    serial_fd = open_serial_port(config.serial_port, config.baudrate);
    if (serial_fd < 0) {
        print_error("Failed to open serial port");
        return 1;
    }

    /* STEP 4: Send modem initialization command */
    rc = init_modem(serial_fd);
    if (rc != SUCCESS) {
        print_error("Modem initialization failed");
        goto cleanup;
    }

    /* STEP 5: Wait 2 seconds */
    print_message("Waiting 2 seconds...");
    sleep(2);

    /* STEP 6: Send modem autoanswer command */
    rc = set_modem_autoanswer(serial_fd);
    if (rc != SUCCESS) {
        print_error("Failed to set modem autoanswer");
        goto cleanup;
    }

    /* STEP 7: Wait 2 seconds */
    print_message("Waiting 2 seconds...");
    sleep(2);

    /* STEP 7: Monitor serial port for RING signal and connection */
    int connected_speed = -1;

    if (config.autoanswer_mode == 1) {
        /* HARDWARE mode: Enhanced auto-answer monitoring with validation */
        print_message("=== HARDWARE AUTO-ANSWER MODE ===");
        print_message("S0=2: Modem will automatically answer after 2 RINGs");
        print_message("Monitoring: RING timing, modem response, and CONNECT detection");

        /* Pre-connection validation */
        print_message("Validating modem readiness before monitoring...");
        rc = verify_modem_readiness(serial_fd);
        if (rc != SUCCESS) {
            print_error("Modem not ready for incoming calls");
            goto cleanup;
        }

        print_message("Modem ready - Starting enhanced RING/CONNECT monitoring...");
        rc = wait_for_ring(serial_fd, config.ring_wait_timeout, &connected_speed);
        if (rc != SUCCESS) {
            if (rc == ERROR_TIMEOUT) {
                print_error("Timeout: No RING/CONNECT detected within %d seconds", config.ring_wait_timeout);
                print_message("Possible causes:");
                print_message("  - No incoming calls received");
                print_message("  - Modem S0 register not properly set to 2");
                print_message("  - Serial port communication issues");
                print_message("  - Caller hung up before 2nd RING");

                /* Attempt error recovery */
                print_message("Attempting recovery from timeout condition...");
                rc = recover_modem_error(serial_fd, ERROR_TIMEOUT);
                if (rc == SUCCESS) {
                    print_message("Recovery successful - you may try again");
                }
            } else if (rc == ERROR_MODEM) {
                print_error("Modem error during auto-answer sequence");
                print_message("Check modem configuration and phone line connection");

                /* Attempt error recovery */
                print_message("Attempting modem error recovery...");
                rc = recover_modem_error(serial_fd, ERROR_MODEM);
                if (rc == SUCCESS) {
                    print_message("Modem recovery successful - you may try again");
                }
            } else {
                print_error("Failed to detect RING/CONNECT signal (error: %d)", rc);

                /* Attempt general error recovery */
                print_message("Attempting general error recovery...");
                rc = recover_modem_error(serial_fd, rc);
                if (rc == SUCCESS) {
                    print_message("Recovery successful - you may try again");
                }
            }
            goto cleanup;
        }

        /* Post-connection validation */
        print_message("=== AUTO-ANSWER SUCCESSFUL ===");
        if (config.enable_connection_validation) {
            print_message("Validating connection stability...");
            rc = validate_connection_quality(serial_fd, config.validation_duration);
            if (rc != SUCCESS) {
                print_error("Connection validation failed - connection may be unstable");
                print_message("Continuing anyway, but data transmission may fail");
            }
        }
    } else {
        /* SOFTWARE mode: wait for 2 RINGs, then send ATA manually */
        print_message("SOFTWARE mode: Waiting for RING signals...");
        rc = wait_for_ring(serial_fd, config.ring_wait_timeout, NULL);
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
    }

    /* STEP 8a: Dynamically adjust serial port speed to match actual connection speed */
    if (connected_speed > 0 && connected_speed != config.baudrate) {
        print_message("Connection speed (%d bps) differs from configured speed (%d bps)",
                      connected_speed, config.baudrate);
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
    if (config.enable_carrier_detect) {
        rc = enable_carrier_detect(serial_fd);
        if (rc != SUCCESS) {
            print_message("Warning: Failed to enable carrier detect");
            /* Continue anyway - not fatal */
        }
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
    if (config.enable_transmission_log) {
        log_transmission("FIRST", "first\r\n", 7);
    }

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
    if (config.enable_transmission_log) {
        log_transmission("SECOND", "second\r\n", 8);
    }

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
