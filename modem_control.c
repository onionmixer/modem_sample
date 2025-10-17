/*****************************************************************************
 * Modem Control Module
 * Based on MBSE BBS mbcico/dial.c and mbcico/chat.c
 *****************************************************************************/

#include "modem_sample.h"

/*
 * Send AT command and wait for response
 * Reference: mbcico/chat.c chat() and mbcico/dial.c initmodem()
 */
int send_at_command(int fd, const char *command, char *response, int resp_size, int timeout)
{
    char cmd_buf[256];
    char line_buf[LINE_BUFFER_SIZE];
    int len, rc;
    time_t start_time, current_time;
    int remaining_timeout;

    if (fd < 0 || !command)
        return ERROR_GENERAL;

    /* Flush input buffer before sending command */
    serial_flush_input(fd);

    /* Prepare command with CR terminator */
    snprintf(cmd_buf, sizeof(cmd_buf), "%s\r", command);
    len = strlen(cmd_buf);

    print_message("Sending: %s", command);

    /* Send command */
    rc = serial_write(fd, cmd_buf, len);
    if (rc < 0) {
        print_error("Failed to send AT command");
        return ERROR_MODEM;
    }

    /* Small delay after sending */
    usleep(100000); /* 100ms */

    /* Read response lines until OK or ERROR or timeout */
    start_time = time(NULL);
    response[0] = '\0';

    while (1) {
        current_time = time(NULL);
        remaining_timeout = timeout - (current_time - start_time);

        if (remaining_timeout <= 0) {
            print_error("Timeout waiting for modem response");
            return ERROR_TIMEOUT;
        }

        rc = serial_read_line(fd, line_buf, sizeof(line_buf), remaining_timeout);

        if (rc < 0) {
            if (rc == ERROR_TIMEOUT) {
                print_error("Timeout reading modem response");
            }
            return rc;
        }

        if (rc > 0) {
            /* Print received line */
            print_message("Received: %s", line_buf);

            /* Store response if buffer provided */
            if (response && resp_size > 0) {
                if (strlen(response) + rc + 2 < (size_t)resp_size) {
                    if (response[0] != '\0')
                        strcat(response, "\n");
                    strcat(response, line_buf);
                }
            }

            /* Check for CONNECT (successful connection) */
            if (strstr(line_buf, "CONNECT") != NULL) {
                print_message("Modem connected: %s", line_buf);
                return SUCCESS;
            }

            /* Check for connection errors */
            if (strstr(line_buf, "NO CARRIER") != NULL) {
                print_error("Connection failed: NO CARRIER");
                return ERROR_MODEM;
            }
            if (strstr(line_buf, "BUSY") != NULL) {
                print_error("Connection failed: BUSY");
                return ERROR_MODEM;
            }
            if (strstr(line_buf, "NO DIALTONE") != NULL) {
                print_error("Connection failed: NO DIALTONE");
                return ERROR_MODEM;
            }
            if (strstr(line_buf, "NO ANSWER") != NULL) {
                print_error("Connection failed: NO ANSWER");
                return ERROR_MODEM;
            }

            /* Check for OK */
            if (strstr(line_buf, "OK") != NULL) {
                return SUCCESS;
            }

            /* Check for ERROR */
            if (strstr(line_buf, "ERROR") != NULL) {
                print_error("Modem returned ERROR");
                return ERROR_MODEM;
            }
        }
    }

    return SUCCESS;
}

/*
 * Parse and send multiple AT commands separated by semicolon
 * Reference: mbcico/dial.c initmodem()
 */
static int send_command_string(int fd, const char *cmd_string, int timeout)
{
    char *commands, *cmd, *saveptr;
    char response[BUFFER_SIZE];
    int rc;

    if (!cmd_string || strlen(cmd_string) == 0)
        return SUCCESS;

    /* Make a copy of the command string for parsing */
    commands = strdup(cmd_string);
    if (!commands)
        return ERROR_GENERAL;

    /* Parse commands separated by semicolon */
    cmd = strtok_r(commands, ";", &saveptr);

    while (cmd != NULL) {
        /* Trim leading spaces */
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;

        if (strlen(cmd) > 0) {
            rc = send_at_command(fd, cmd, response, sizeof(response), timeout);
            if (rc != SUCCESS) {
                free(commands);
                return rc;
            }

            /* Small delay between commands */
            usleep(200000); /* 200ms */
        }

        cmd = strtok_r(NULL, ";", &saveptr);
    }

    free(commands);
    return SUCCESS;
}

/*
 * Initialize modem with initialization commands
 * Reference: mbcico/dial.c initmodem()
 */
int init_modem(int fd)
{
    int rc;

    print_message("Initializing modem...");

    rc = send_command_string(fd, config.modem_init_command, config.at_command_timeout);

    if (rc == SUCCESS) {
        print_message("Modem initialized successfully");
    } else {
        print_error("Modem initialization failed");
    }

    return rc;
}

/*
 * Set modem to autoanswer mode
 * Supports both SOFTWARE (S0=0) and HARDWARE (S0=2) modes
 */
int set_modem_autoanswer(int fd)
{
    int rc;
    const char *command;

if (config.autoanswer_mode == 1) {
        /* HARDWARE mode: Modem auto-answers after 2 rings */
        command = config.modem_autoanswer_hardware_command;
        print_message("Setting modem to HARDWARE autoanswer mode (S0=2)...");
    } else {
        /* SOFTWARE mode: Manual answer with ATA command */
        command = config.modem_autoanswer_software_command;
        print_message("Setting modem to SOFTWARE autoanswer mode (S0=0)...");
    }

    rc = send_command_string(fd, command, config.at_command_timeout);

    if (rc == SUCCESS) {
        if (config.autoanswer_mode == 1) {
            print_message("Modem autoanswer set successfully - will auto-answer after 2 RINGs");
        } else {
            print_message("Modem autoanswer set successfully - manual ATA required");
        }
    } else {
        print_error("Failed to set modem autoanswer");
    }

    return rc;
}

/*
 * Hangup modem connection
 * Reference: mbcico/dial.c hangup()
 */
int modem_hangup(int fd)
{
    char response[BUFFER_SIZE];
    int rc;

    print_message("Hanging up modem...");

    /* Flush buffers */
    serial_flush_input(fd);
    serial_flush_output(fd);

    /* Small delay before hangup command */
    usleep(500000); /* 500ms */

    /* Disable carrier detect before hangup to prevent I/O errors */
    print_message("Disabling carrier detect for hangup...");
    struct termios tios;
    if (tcgetattr(fd, &tios) == 0) {
        tios.c_cflag |= CLOCAL;  /* Re-enable CLOCAL to ignore carrier */
        tcsetattr(fd, TCSANOW, &tios);
    }

    /* Send ATH hangup command (may timeout if connection already dropped) */
    rc = send_at_command(fd, config.modem_hangup_command, response, sizeof(response), 3);

    if (rc == SUCCESS) {
        print_message("ATH command successful");
    } else if (rc == ERROR_TIMEOUT) {
        print_message("ATH timeout (connection may already be dropped)");
    } else {
        print_message("ATH command completed (status: %d)", rc);
    }

    /* Perform DTR drop for hardware hangup */
    /* Note: This may fail if carrier is already lost, but that's OK */
    rc = dtr_drop_hangup(fd);
    if (rc != SUCCESS) {
        print_message("DTR drop completed with warning (connection may already be dropped)");
    }

    /* Flush again */
    serial_flush_input(fd);
    serial_flush_output(fd);

    print_message("Modem hangup completed");

    return SUCCESS; /* Always return success for hangup */
}

/*
 * Detect RING in a line
 */
int detect_ring(const char *line)
{
    return (line && strstr(line, "RING") != NULL);
}

/*
 * Parse CONNECT response to extract connection speed
 * Examples: "CONNECT 1200", "CONNECT 2400/ARQ", "CONNECT 9600/V42"
 * Enhanced version with better error handling and logging
 */
int parse_connect_speed(const char *connect_str)
{
    int speed = 0;
    const char *ptr;
    char temp_str[64];

    if (!connect_str) {
        print_error("parse_connect_speed: connect_str is NULL");
        return -1;
    }

    /* Log the raw CONNECT response for debugging */
    print_message("Parsing CONNECT response: '%s'", connect_str);

    /* Find "CONNECT" */
    ptr = strstr(connect_str, "CONNECT");
    if (!ptr) {
        print_error("No 'CONNECT' found in response string");
        return -1;
    }

    /* Skip "CONNECT" and any spaces */
    ptr += 7; /* length of "CONNECT" */
    while (*ptr == ' ' || *ptr == '\t')
        ptr++;

    /* Copy the speed part for analysis */
    strncpy(temp_str, ptr, sizeof(temp_str) - 1);
    temp_str[sizeof(temp_str) - 1] = '\0';

    /* Remove any trailing protocol information */
    char *slash_pos = strchr(temp_str, '/');
    if (slash_pos) {
        *slash_pos = '\0';
        print_message("Protocol detected: '%s'", slash_pos + 1);
    }

    /* Remove any trailing spaces or newlines */
    char *end = temp_str + strlen(temp_str) - 1;
    while (end > temp_str && (*end == ' ' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    /* Extract the speed number */
    if (sscanf(temp_str, "%d", &speed) == 1) {
        print_message("Successfully parsed connection speed: %d bps", speed);

        /* Validate speed is reasonable */
        if (speed < 300 || speed > 115200) {
            print_message("Warning: Unusual speed %d bps - may be incorrect", speed);
        }

        return speed;
    }

    /* Check for common CONNECT variations */
    if (strlen(temp_str) == 0) {
        print_message("CONNECT without speed - assuming 300 bps (legacy modem)");
        return 300;
    }

    /* Try to extract from more complex formats */
    if (strstr(temp_str, "1200")) speed = 1200;
    else if (strstr(temp_str, "2400")) speed = 2400;
    else if (strstr(temp_str, "4800")) speed = 4800;
    else if (strstr(temp_str, "9600")) speed = 9600;
    else if (strstr(temp_str, "19200")) speed = 19200;
    else if (strstr(temp_str, "38400")) speed = 38400;
    else if (strstr(temp_str, "57600")) speed = 57600;
    else if (strstr(temp_str, "115200")) speed = 115200;

    if (speed > 0) {
        print_message("Extracted speed from complex format: %d bps", speed);
        return speed;
    }

    print_error("Failed to parse speed from CONNECT response: '%s'", temp_str);
    return -1;
}

/*
 * Answer incoming call with speed detection (SOFTWARE mode only)
 * Sends ATA command and parses CONNECT response to detect connection speed
 * Used when MODEM_AUTOANSWER_MODE = 0 (SOFTWARE mode)
 */
int modem_answer_with_speed_adjust(int fd, int *connected_speed)
{
    char line_buf[LINE_BUFFER_SIZE];
    int rc;
    int speed = -1;
    time_t start_time, current_time;
    int remaining_timeout;

    print_message("Answering incoming call (ATA) with speed detection...");

    /* Flush input buffer before sending command */
    serial_flush_input(fd);

    /* Send ATA command */
    rc = serial_write(fd, "ATA\r", 4);
    if (rc < 0) {
        print_error("Failed to send ATA command");
        return ERROR_MODEM;
    }

    /* Wait for CONNECT response */
    start_time = time(NULL);

    while (1) {
        current_time = time(NULL);
        remaining_timeout = config.at_answer_timeout - (current_time - start_time);

        if (remaining_timeout <= 0) {
            print_error("Timeout waiting for modem response");
            return ERROR_TIMEOUT;
        }

        rc = serial_read_line(fd, line_buf, sizeof(line_buf), remaining_timeout);

        if (rc < 0) {
            if (rc == ERROR_TIMEOUT) {
                print_error("Timeout reading modem response");
            }
            return rc;
        }

        if (rc > 0) {
            print_message("Received: %s", line_buf);

            /* Check for CONNECT */
            if (strstr(line_buf, "CONNECT") != NULL) {
                print_message("Modem connected: %s", line_buf);

                /* Parse speed from CONNECT response */
                speed = parse_connect_speed(line_buf);
                if (speed > 0 && connected_speed) {
                    *connected_speed = speed;
                }

                return SUCCESS;
            }

            /* Check for errors */
            if (strstr(line_buf, "NO CARRIER") != NULL) {
                print_error("Connection failed: NO CARRIER");
                return ERROR_MODEM;
            }
            if (strstr(line_buf, "BUSY") != NULL) {
                print_error("Connection failed: BUSY");
                return ERROR_MODEM;
            }
            if (strstr(line_buf, "NO ANSWER") != NULL) {
                print_error("Connection failed: NO ANSWER");
                return ERROR_MODEM;
            }
        }
    }

    return SUCCESS;
}

/*
 * Verify modem readiness for incoming calls
 * Checks modem status and configuration before monitoring
 */
int verify_modem_readiness(int fd)
{
    char response[BUFFER_SIZE];
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Checking modem readiness...");

    /* Check if modem is responsive */
    rc = send_at_command(fd, "AT", response, sizeof(response), config.at_command_timeout);
    if (rc != SUCCESS) {
        print_error("Modem not responding to AT command");
        return rc;
    }

    /* Check S0 register value */
    rc = send_at_command(fd, "ATS0?", response, sizeof(response), config.at_command_timeout);
    if (rc != SUCCESS) {
        print_error("Failed to read S0 register");
        return rc;
    }

    print_message("Modem S0 status: %s", response);

    /* Verify modem is in proper state for auto-answer */
    if (strstr(response, "0") == response) {
        print_message("Warning: S0=0 (manual answer mode) detected");
        print_message("For hardware auto-answer, S0 should be 2");
    } else if (strstr(response, "2") == response) {
        print_message("S0=2 confirmed - hardware auto-answer ready");
    } else {
        print_message("S0=%s detected - custom auto-answer ring count", response);
    }

    return SUCCESS;
}

/*
 * Validate connection quality after establishment
 * Monitors carrier signal and line quality
 */
int validate_connection_quality(int fd, int duration_seconds)
{
    time_t start_time;
    int carrier_checks = 0;
    int carrier_ok = 0;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Validating connection quality for %d seconds...", duration_seconds);

    start_time = time(NULL);

    while (!interrupted && (time(NULL) - start_time) < duration_seconds) {
        /* Check carrier status */
        int carrier = check_carrier_status(fd);
        carrier_checks++;

        if (carrier > 0) {
            carrier_ok++;
        } else if (carrier < 0) {
            print_error("Failed to check carrier status during validation");
            return ERROR_PORT;
        } else {
            print_error("Carrier lost during validation period");
            return ERROR_HANGUP;
        }

        /* Check for any error indicators on the line */
        if (serial_check_available(fd)) {
            char error_buf[64];
            int rc = serial_read(fd, error_buf, sizeof(error_buf) - 1, 0);
            if (rc > 0) {
                error_buf[rc] = '\0';
                if (strstr(error_buf, "NO CARRIER") ||
                    strstr(error_buf, "ERROR") ||
                    strstr(error_buf, "DISCONNECT")) {
                    print_error("Connection error during validation: %s", error_buf);
                    return ERROR_MODEM;
                }
            }
        }

        sleep(1); /* Check every second */
    }

    /* Calculate carrier quality percentage */
    if (carrier_checks > 0) {
        int quality = (carrier_ok * 100) / carrier_checks;
        print_message("Connection validation completed: %d%% carrier stability", quality);

        if (quality < 90) {
            print_message("Warning: Connection quality below optimal (%d%%)", quality);
            return ERROR_MODEM; /* Not fatal, but indicates poor quality */
        }
    }

    return SUCCESS;
}

/*
 * Enhanced error recovery for modem issues
 * Attempts to recover from common modem problems
 */
int recover_modem_error(int fd, int error_type)
{
    char response[BUFFER_SIZE];
    int rc;
    int retry_count = 0;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Attempting modem error recovery (type: %d)...", error_type);

    while (retry_count < config.max_recovery_attempts && !interrupted) {
        retry_count++;
        print_message("Recovery attempt %d/%d", retry_count, config.max_recovery_attempts);

        switch (error_type) {
            case ERROR_MODEM:
                /* Try to reset modem and reconfigure */
                print_message("Resetting modem configuration...");
                rc = send_at_command(fd, "ATZ", response, sizeof(response), 5);
                if (rc == SUCCESS) {
                    /* Re-apply auto-answer settings */
                    rc = set_modem_autoanswer(fd);
                    if (rc == SUCCESS) {
                        print_message("Modem recovery successful");
                        return SUCCESS;
                    }
                }
                break;

            case ERROR_TIMEOUT:
                /* Try to clear any stuck conditions */
                print_message("Clearing potential modem hang condition...");
                serial_flush_input(fd);
                serial_flush_output(fd);

                /* Send carriage return to wake modem */
                rc = serial_write(fd, "\r", 1);
                if (rc < 0) {
                    print_error("Failed to send wake-up character");
                    return rc;
                }

                usleep(500000); /* Wait 500ms */

                /* Check if modem responds */
                rc = send_at_command(fd, "AT", response, sizeof(response), 3);
                if (rc == SUCCESS) {
                    print_message("Modem wake-up successful");
                    return SUCCESS;
                }
                break;

            default:
                print_error("Unknown error type for recovery: %d", error_type);
                return ERROR_GENERAL;
        }

        /* Wait before retry */
        if (retry_count < 3) {
            print_message("Waiting 2 seconds before retry...");
            sleep(2);
        }
    }

    print_error("Modem recovery failed after %d attempts", retry_count);
    return ERROR_MODEM;
}
