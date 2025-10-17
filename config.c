/*****************************************************************************
 * Configuration Module
 * Handles loading and parsing of configuration files
 *****************************************************************************/

#include "modem_sample.h"
#include <ctype.h>

/* Global configuration structure */
modem_config_t config;

/* Configuration key-value storage for flexible parsing */
#define MAX_CONFIG_ENTRIES 100
typedef struct {
    char key[128];
    char value[512];
} config_entry_t;

static config_entry_t config_entries[MAX_CONFIG_ENTRIES];
static int config_count = 0;

/*
 * Initialize default configuration values
 */
void init_default_config(void)
{
    /* Serial Port Configuration */
    strcpy(config.serial_port, "/dev/ttyUSB0");
    config.baudrate = 4800;
    config.data_bits = 8;
    strcpy(config.parity, "NONE");
    config.stop_bits = 1;
    strcpy(config.flow_control, "NONE");

    /* Modem Configuration */
    strcpy(config.modem_init_command, "ATZ; AT&F Q0 V1 X4 &C1 &D2 S7=60 S10=120 S30=5");
    strcpy(config.modem_autoanswer_software_command, "ATE0 S0=0");
    strcpy(config.modem_autoanswer_hardware_command, "ATE0 S0=2");
    strcpy(config.modem_hangup_command, "ATH");

    /* Autoanswer Mode Configuration */
    config.autoanswer_mode = 1;  /* HARDWARE */

    /* Timeout Configuration (seconds) */
    config.at_command_timeout = 5;
    config.at_answer_timeout = 60;
    config.ring_wait_timeout = 60;
    config.ring_idle_timeout = 10;
    config.connect_timeout = 30;

    /* Buffer Sizes */
    config.buffer_size = 1024;
    config.line_buffer_size = 256;

    /* Retry Configuration */
    config.max_write_retry = 3;
    config.retry_delay_us = 100000;  /* 100ms */
    config.tx_chunk_size = 256;
    config.tx_chunk_delay_us = 10000;  /* 10ms */

    /* Logging Configuration */
    config.verbose_mode = 1;
    config.enable_transmission_log = 1;
    config.enable_timing_log = 1;

    /* Advanced Options */
    config.enable_carrier_detect = 1;
    config.enable_connection_validation = 1;
    config.validation_duration = 2;
    config.enable_error_recovery = 1;
    config.max_recovery_attempts = 3;

    /* Clear configuration entries */
    config_count = 0;
    memset(config_entries, 0, sizeof(config_entries));
}

/*
 * Trim whitespace from string
 */
static void trim_string(char *str)
{
    char *end;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    /* All spaces? */
    if (*str == '\0') {
        return;
    }

    /* Trim trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    /* Write new null terminator */
    *(end + 1) = '\0';
}

/*
 * Parse a configuration line
 */
static int parse_config_line(const char *line, char *key, char *value)
{
    const char *equals;
    int key_len, value_len;

    if (!line || !key || !value) {
        return -1;
    }

    /* Skip comments and empty lines */
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
        return 0;  /* Skip this line */
    }

    /* Find equals sign */
    equals = strchr(line, '=');
    if (!equals) {
        return -1;  /* Invalid line format */
    }

    /* Extract key */
    key_len = equals - line;
    if (key_len >= 127) {  /* Leave room for null terminator */
        key_len = 127;
    }
    if (key_len > 0) {
        memcpy(key, line, key_len);
        key[key_len] = '\0';
    } else {
        key[0] = '\0';
    }
    trim_string(key);

    /* Extract value */
    value_len = strlen(equals + 1);
    if (value_len >= 512) {
        value_len = 511;
    }
    strcpy(value, equals + 1);
    trim_string(value);

    return 1;  /* Successfully parsed */
}

/*
 * Load configuration from file
 */
int load_config(const char *config_file)
{
    FILE *fp;
    char line[1024];
    char key[128];
    char value[512];
    int line_num = 0;
    int parsed_count = 0;

    if (!config_file) {
        print_error("load_config: config_file is NULL");
        return ERROR_GENERAL;
    }

    /* Initialize default values first */
    init_default_config();

    fp = fopen(config_file, "r");
    if (!fp) {
        print_error("Failed to open config file '%s': %s", config_file, strerror(errno));
        print_message("Using default configuration values");
        return SUCCESS;  /* Not fatal - use defaults */
    }

    print_message("Loading configuration from: %s", config_file);

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Remove trailing newline */
        line[strcspn(line, "\r\n")] = '\0';

        int result = parse_config_line(line, key, value);
        if (result == 1) {
            /* Store in configuration entries array */
            if (config_count < MAX_CONFIG_ENTRIES) {
                strcpy(config_entries[config_count].key, key);
                strcpy(config_entries[config_count].value, value);
                config_count++;
                parsed_count++;
            }
        } else if (result == -1) {
            print_error("Invalid config line %d: %s", line_num, line);
            /* Continue parsing other lines */
        }
    }

    fclose(fp);

    /* Now apply the loaded values to the config structure */

    /* Serial Port Configuration */
    strcpy(config.serial_port, get_config_string("serial_port", config.serial_port));
    config.baudrate = get_config_int("baudrate", config.baudrate);
    config.data_bits = get_config_int("data_bits", config.data_bits);
    strcpy(config.parity, get_config_string("parity", config.parity));
    config.stop_bits = get_config_int("stop_bits", config.stop_bits);
    strcpy(config.flow_control, get_config_string("flow_control", config.flow_control));

    /* Modem Configuration */
    strcpy(config.modem_init_command, get_config_string("modem_init_command", config.modem_init_command));
    strcpy(config.modem_autoanswer_software_command,
           get_config_string("modem_autoanswer_software_command", config.modem_autoanswer_software_command));
    strcpy(config.modem_autoanswer_hardware_command,
           get_config_string("modem_autoanswer_hardware_command", config.modem_autoanswer_hardware_command));
    strcpy(config.modem_hangup_command, get_config_string("modem_hangup_command", config.modem_hangup_command));

    /* Autoanswer Mode Configuration */
    config.autoanswer_mode = get_config_int("autoanswer_mode", config.autoanswer_mode);

    /* Timeout Configuration */
    config.at_command_timeout = get_config_int("at_command_timeout", config.at_command_timeout);
    config.at_answer_timeout = get_config_int("at_answer_timeout", config.at_answer_timeout);
    config.ring_wait_timeout = get_config_int("ring_wait_timeout", config.ring_wait_timeout);
    config.ring_idle_timeout = get_config_int("ring_idle_timeout", config.ring_idle_timeout);
    config.connect_timeout = get_config_int("connect_timeout", config.connect_timeout);

    /* Buffer Sizes */
    config.buffer_size = get_config_int("buffer_size", config.buffer_size);
    config.line_buffer_size = get_config_int("line_buffer_size", config.line_buffer_size);

    /* Retry Configuration */
    config.max_write_retry = get_config_int("max_write_retry", config.max_write_retry);
    config.retry_delay_us = get_config_int("retry_delay_us", config.retry_delay_us);
    config.tx_chunk_size = get_config_int("tx_chunk_size", config.tx_chunk_size);
    config.tx_chunk_delay_us = get_config_int("tx_chunk_delay_us", config.tx_chunk_delay_us);

    /* Logging Configuration */
    config.verbose_mode = get_config_int("verbose_mode", config.verbose_mode);
    config.enable_transmission_log = get_config_int("enable_transmission_log", config.enable_transmission_log);
    config.enable_timing_log = get_config_int("enable_timing_log", config.enable_timing_log);

    /* Advanced Options */
    config.enable_carrier_detect = get_config_int("enable_carrier_detect", config.enable_carrier_detect);
    config.enable_connection_validation = get_config_int("enable_connection_validation", config.enable_connection_validation);
    config.validation_duration = get_config_int("validation_duration", config.validation_duration);
    config.enable_error_recovery = get_config_int("enable_error_recovery", config.enable_error_recovery);
    config.max_recovery_attempts = get_config_int("max_recovery_attempts", config.max_recovery_attempts);

    print_message("Configuration loaded successfully: %d settings parsed", parsed_count);
    return SUCCESS;
}

/*
 * Get integer value from configuration
 */
int get_config_int(const char *key, int default_value)
{
    int i;

    if (!key) {
        return default_value;
    }

    for (i = 0; i < config_count; i++) {
        if (strcmp(config_entries[i].key, key) == 0) {
            char *endptr;
            long value = strtol(config_entries[i].value, &endptr, 10);

            if (endptr != config_entries[i].value && *endptr == '\0') {
                return (int)value;
            } else {
                print_error("Invalid integer value for %s: %s", key, config_entries[i].value);
                return default_value;
            }
        }
    }

    return default_value;
}

/*
 * Get string value from configuration
 */
const char *get_config_string(const char *key, const char *default_value)
{
    int i;

    if (!key) {
        return default_value;
    }

    for (i = 0; i < config_count; i++) {
        if (strcmp(config_entries[i].key, key) == 0) {
            return config_entries[i].value;
        }
    }

    return default_value;
}

/*
 * Print current configuration
 */
void print_config(void)
{
    print_message("=== Current Configuration ===");

    print_message("Serial Port: %s at %d baud (%d-%s-%d, flow: %s)",
                  config.serial_port, config.baudrate, config.data_bits,
                  config.parity, config.stop_bits, config.flow_control);

    print_message("Autoanswer Mode: %s (S0=%s)",
                  config.autoanswer_mode ? "HARDWARE" : "SOFTWARE",
                  config.autoanswer_mode ? "2" : "0");

    print_message("Timeouts: AT=%ds, Answer=%ds, Ring=%ds, Connect=%ds",
                  config.at_command_timeout, config.at_answer_timeout,
                  config.ring_wait_timeout, config.connect_timeout);

    print_message("Buffers: %d bytes, Line: %d bytes",
                  config.buffer_size, config.line_buffer_size);

    print_message("Retry: Max %d attempts, Delay %d us",
                  config.max_write_retry, config.retry_delay_us);

    print_message("Logging: Verbose=%s, TX Log=%s, Timing=%s",
                  config.verbose_mode ? "ON" : "OFF",
                  config.enable_transmission_log ? "ON" : "OFF",
                  config.enable_timing_log ? "ON" : "OFF");

    print_message("Advanced: Carrier Detect=%s, Validation=%s (%ds), Recovery=%s",
                  config.enable_carrier_detect ? "ON" : "OFF",
                  config.enable_connection_validation ? "ON" : "OFF",
                  config.validation_duration,
                  config.enable_error_recovery ? "ON" : "OFF");

    print_message("==============================");
}