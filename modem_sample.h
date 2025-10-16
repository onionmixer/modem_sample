#ifndef MODEM_SAMPLE_H
#define MODEM_SAMPLE_H

/* Feature test macros - must be before any includes */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>

/* Configuration Constants */
#define SERIAL_PORT     "/dev/ttyUSB0"
#define BAUDRATE        4800  /* Changed from 9600 to match client */
#define BIT_PARITY      0  /* NONE */
#define BIT_DATA        8
#define BIT_STOP        1
#define FLOW_CONTROL    0  /* NONE */

/* Modem Commands */
#define MODEM_INIT_COMMAND      "ATZ; AT&F Q0 V1 X4 &C1 &D2 S7=60 S10=120 S30=5"
#define MODEM_AUTOANSWER_SOFTWARE_COMMAND "ATE0 S0=0"
#define MODEM_AUTOANSWER_HARDWARE_COMMAND "ATE0 S0=2"
#define MODEM_HANGUP_COMMAND    "ATH"

/* Autoanswer Mode Configuration */
#define MODEM_AUTOANSWER_MODE   1  /* 0=SOFTWARE (manual ATA), 1=HARDWARE (auto-answer after 2 rings) */

/* Buffer Sizes */
#define BUFFER_SIZE     1024
#define LINE_BUFFER_SIZE 256

/* Timeouts (seconds) */
#define AT_COMMAND_TIMEOUT  5
#define AT_ANSWER_TIMEOUT   60  /* ATA command - wait longer */
#define RING_WAIT_TIMEOUT   60
#define RING_IDLE_TIMEOUT   10  /* Timeout between RINGs */

/* Return Codes (matching MBSE BBS style) */
#define SUCCESS         0
#define ERROR_PORT      -1
#define ERROR_TIMEOUT   -2
#define ERROR_MODEM     -3
#define ERROR_GENERAL   -4
#define ERROR_HANGUP    -5

/* Status Codes (from MBSE ttyio.c) */
#define STAT_OK         0
#define STAT_ERROR      1
#define STAT_TIMEOUT    2
#define STAT_EOF        3
#define STAT_HANGUP     4

/* Retry Configuration */
#define MAX_WRITE_RETRY     3
#define RETRY_DELAY_US      100000  /* 100ms */
#define TX_CHUNK_SIZE       256     /* Bytes per chunk for large transfers */
#define TX_CHUNK_DELAY_US   10000   /* 10ms between chunks */

/* Global Variables */
extern int serial_fd;
extern int verbose_mode;
extern volatile sig_atomic_t interrupted;

/* Serial Port Functions (serial_port.c) */
int open_serial_port(const char *device, int baudrate);
void close_serial_port(int fd);
int serial_write(int fd, const char *data, int len);
int serial_read(int fd, char *buffer, int size, int timeout);
int serial_read_line(int fd, char *buffer, int size, int timeout);
void serial_flush_input(int fd);
void serial_flush_output(int fd);
int serial_check_available(int fd);
int enable_carrier_detect(int fd);
int dtr_drop_hangup(int fd);
int lock_port(const char *device);
void unlock_port(void);
int adjust_serial_speed(int fd, int new_baudrate);

/* Enhanced Transmission Functions (from MBSE patterns) */
int check_carrier_status(int fd);
int verify_carrier_before_send(int fd);
int robust_serial_write(int fd, const char *data, int len);
int buffered_serial_send(int fd, const char *data, int len);
void log_transmission(const char *label, const char *data, int len);
int wait_for_client_ready(int fd, const char *ready_string, int timeout);

/* Modem Control Functions (modem_control.c) */
int send_at_command(int fd, const char *command, char *response, int resp_size, int timeout);
int init_modem(int fd);
int set_modem_autoanswer(int fd);
int modem_answer_with_speed_adjust(int fd, int *connected_speed);
int modem_hangup(int fd);
int detect_ring(const char *line);
int parse_connect_speed(const char *connect_str);

/* Utility Functions */
void print_message(const char *format, ...);
void print_error(const char *format, ...);
void signal_handler(int sig);
void setup_signal_handlers(void);

#endif /* MODEM_SAMPLE_H */
