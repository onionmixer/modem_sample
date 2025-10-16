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

    rc = send_command_string(fd, MODEM_INIT_COMMAND, AT_COMMAND_TIMEOUT);

    if (rc == SUCCESS) {
        print_message("Modem initialized successfully");
    } else {
        print_error("Modem initialization failed");
    }

    return rc;
}

/*
 * Set modem to autoanswer mode
 */
int set_modem_autoanswer(int fd)
{
    int rc;

    print_message("Setting modem autoanswer...");

    rc = send_command_string(fd, MODEM_AUTOANSWER_COMMAND, AT_COMMAND_TIMEOUT);

    if (rc == SUCCESS) {
        print_message("Modem autoanswer set successfully");
    } else {
        print_error("Failed to set modem autoanswer");
    }

    return rc;
}

/*
 * Answer incoming call (send ATA command)
 * Reference: mbcico/dial.c dialphone() and mbcico/answer.c answer()
 */
int modem_answer(int fd)
{
    char response[BUFFER_SIZE];
    int rc;

    print_message("Answering incoming call (ATA)...");

    /* Send ATA command with longer timeout for connection establishment */
    rc = send_at_command(fd, "ATA", response, sizeof(response), AT_ANSWER_TIMEOUT);

    if (rc == SUCCESS) {
        print_message("Call answered successfully - Connection established");
    } else {
        print_error("Failed to answer call");
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
    rc = send_at_command(fd, MODEM_HANGUP_COMMAND, response, sizeof(response), 3);

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
 * Reference: mbcico/answer.c answer()
 */
int detect_ring(const char *line)
{
    if (!line)
        return 0;

    /* Check for RING string */
    if (strstr(line, "RING") != NULL) {
        return 1;
    }

    return 0;
}
