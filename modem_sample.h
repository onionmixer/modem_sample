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

/* Configuration Structure */
typedef struct {
    /* Serial Port Configuration */
    char serial_port[256];
    int baudrate;
    int data_bits;
    char parity[16];
    int stop_bits;
    char flow_control[16];

    /* Modem Configuration */
    char modem_init_command[512];
    char modem_autoanswer_software_command[128];
    char modem_autoanswer_hardware_command[128];
    char modem_hangup_command[64];

    /* Autoanswer Mode Configuration */
    int autoanswer_mode;  /* 0=SOFTWARE, 1=HARDWARE */

    /* Timeout Configuration (seconds) */
    int at_command_timeout;
    int at_answer_timeout;
    int ring_wait_timeout;
    int ring_idle_timeout;
    int connect_timeout;

    /* Buffer Sizes */
    int buffer_size;
    int line_buffer_size;

    /* Retry Configuration */
    int max_write_retry;
    int retry_delay_us;
    int tx_chunk_size;
    int tx_chunk_delay_us;

    /* Logging Configuration */
    int verbose_mode;
    int enable_transmission_log;
    int enable_timing_log;

    /* Advanced Options */
    int enable_carrier_detect;
    int enable_connection_validation;
    int validation_duration;
    int enable_error_recovery;
    int max_recovery_attempts;
} modem_config_t;

/* Global Variables */
extern int serial_fd;
extern volatile sig_atomic_t interrupted;
extern modem_config_t config;

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

/* Enhanced Modem Functions */
int verify_modem_readiness(int fd);
int validate_connection_quality(int fd, int duration_seconds);
int recover_modem_error(int fd, int error_type);

/* Configuration Functions (config.c) */
int load_config(const char *config_file);
void init_default_config(void);
void print_config(void);
int get_config_int(const char *key, int default_value);
const char *get_config_string(const char *key, const char *default_value);

/* Utility Functions */
void print_message(const char *format, ...);
void print_error(const char *format, ...);
void signal_handler(int sig);
void setup_signal_handlers(void);

#endif /* MODEM_SAMPLE_H */
