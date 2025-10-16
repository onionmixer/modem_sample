# 현재 소스코드 개선안

process_sample.txt 분석 결과에 따른 개선 방안

## 🚨 필수 개선 사항 (CRITICAL)

### 1. ATA(Answer) 명령 추가 - RING 후 실제 연결

**현재 문제:**
- RING 신호를 2회 감지만 하고 실제로 전화를 받지 않음
- 연결되지 않은 상태에서 데이터 전송 시도

**개선 방안:**
```c
// modem_control.c에 추가
int modem_answer(int fd)
{
    char response[BUFFER_SIZE];
    int rc;

    print_message("Answering incoming call...");

    rc = send_at_command(fd, "ATA", response, sizeof(response), 60);

    if (rc == SUCCESS) {
        print_message("Call answered successfully");
    } else {
        print_error("Failed to answer call");
    }

    return rc;
}

// modem_sample.c의 wait_for_ring() 후에 추가
if (ring_count >= 2) {
    print_message("RING signal detected 2 times");

    // ATA 명령으로 실제 연결
    rc = modem_answer(serial_fd);
    if (rc != SUCCESS) {
        print_error("Failed to establish modem connection");
        goto cleanup;
    }

    print_message("MODEM CONNECTION established");
    return SUCCESS;
}
```

---

### 2. CONNECT 응답 감지 및 처리

**현재 문제:**
- OK만 확인하고 CONNECT 계열 응답을 무시
- 연결 실패 응답(NO CARRIER, BUSY 등)을 감지하지 못함

**개선 방안:**
```c
// modem_control.c의 send_at_command() 수정
int send_at_command(int fd, const char *command, char *response, int resp_size, int timeout)
{
    // ... 기존 코드 ...

    if (rc > 0) {
        print_message("Received: %s", line_buf);

        // CONNECT 응답 감지 (연결 성공)
        if (strstr(line_buf, "CONNECT") != NULL) {
            // 속도 정보 추출 (예: CONNECT 9600)
            print_message("Modem connected: %s", line_buf);
            return SUCCESS;
        }

        // 오류 응답 감지
        if (strstr(line_buf, "NO CARRIER") != NULL ||
            strstr(line_buf, "BUSY") != NULL ||
            strstr(line_buf, "NO DIALTONE") != NULL ||
            strstr(line_buf, "NO ANSWER") != NULL) {
            print_error("Modem connection failed: %s", line_buf);
            return ERROR_MODEM;
        }

        // OK 응답
        if (strstr(line_buf, "OK") != NULL) {
            return SUCCESS;
        }

        // ERROR 응답
        if (strstr(line_buf, "ERROR") != NULL) {
            print_error("Modem returned ERROR");
            return ERROR_MODEM;
        }
    }
}
```

---

### 3. Carrier Detect 활성화

**현재 문제:**
- CLOCAL 플래그로 carrier 신호를 무시
- 연결이 끊어져도 감지 불가

**개선 방안:**
```c
// serial_port.c에 추가
int enable_carrier_detect(int fd)
{
    struct termios tios;
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Enabling carrier detect...");

    if ((rc = tcgetattr(fd, &tios)) < 0) {
        print_error("tcgetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Disable CLOCAL - enable carrier detect */
    tios.c_cflag &= ~CLOCAL;

    /* Enable RTS/CTS hardware flow control */
    tios.c_cflag |= CRTSCTS;

    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    print_message("Carrier detect enabled");
    return SUCCESS;
}

// modem_sample.c에서 CONNECT 후 호출
rc = modem_answer(serial_fd);
if (rc == SUCCESS) {
    // Carrier detect 활성화
    enable_carrier_detect(serial_fd);
}
```

---

### 4. DTR Drop Hangup 구현

**현재 문제:**
- ATH 명령만 사용
- 하드웨어 레벨 hangup 미지원

**개선 방안:**
```c
// serial_port.c에 추가
int dtr_drop_hangup(int fd)
{
    struct termios tios;
    speed_t saved_ispeed, saved_ospeed;
    int rc;

    if (fd < 0)
        return ERROR_GENERAL;

    print_message("Performing DTR drop hangup...");

    if ((rc = tcgetattr(fd, &tios)) < 0) {
        print_error("tcgetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Save current speeds */
    saved_ispeed = cfgetispeed(&tios);
    saved_ospeed = cfgetospeed(&tios);

    /* Set speed to 0 to drop DTR */
    cfsetispeed(&tios, B0);
    cfsetospeed(&tios, B0);

    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    /* Wait for modem to recognize hangup */
    sleep(1);

    /* Restore original speeds */
    cfsetispeed(&tios, saved_ispeed);
    cfsetospeed(&tios, saved_ospeed);

    if ((rc = tcsetattr(fd, TCSADRAIN, &tios)) < 0) {
        print_error("tcsetattr failed: %s", strerror(errno));
        return ERROR_PORT;
    }

    print_message("DTR drop hangup completed");
    return SUCCESS;
}

// modem_control.c의 modem_hangup() 수정
int modem_hangup(int fd)
{
    // ... 기존 ATH 명령 ...

    /* DTR drop for hardware hangup */
    dtr_drop_hangup(fd);

    // ... 나머지 코드 ...
}
```

---

## 🔧 권장 개선 사항 (RECOMMENDED)

### 5. 포트 잠금(Port Locking)

**개선 방안:**
```c
// serial_port.c에 추가
#include <sys/stat.h>

static char lock_file[PATH_MAX];

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
                    print_message("Removing stale lock file");
                    unlink(lock_file);
                }
            }
            fclose(fp);
        }
    }

    /* Create lock file */
    fp = fopen(lock_file, "w");
    if (!fp) {
        print_error("Cannot create lock file: %s", strerror(errno));
        return ERROR_PORT;
    }

    fprintf(fp, "%10d\n", getpid());
    fclose(fp);

    print_message("Port locked: %s", lock_file);
    return SUCCESS;
}

void unlock_port(void)
{
    if (lock_file[0] != '\0') {
        unlink(lock_file);
        print_message("Port unlocked: %s", lock_file);
        lock_file[0] = '\0';
    }
}

// open_serial_port()에서 호출
int open_serial_port(const char *device, int baudrate)
{
    // Lock port first
    if (lock_port(device) != SUCCESS) {
        return ERROR_PORT;
    }

    // ... 기존 코드 ...
}

// close_serial_port()에서 호출
void close_serial_port(int fd)
{
    // ... 기존 코드 ...

    unlock_port();
}
```

---

### 6. 연결 통계 기록

**개선 방안:**
```c
// modem_sample.h에 추가
typedef struct {
    time_t connect_time;
    time_t disconnect_time;
    int bytes_sent;
    int bytes_received;
    char remote_id[32];
} connection_stats_t;

extern connection_stats_t conn_stats;

// modem_sample.c에 추가
connection_stats_t conn_stats = {0};

// CONNECT 후
conn_stats.connect_time = time(NULL);

// 데이터 전송 시
conn_stats.bytes_sent += len;

// 연결 종료 시
conn_stats.disconnect_time = time(NULL);
print_message("Connection duration: %d seconds",
              conn_stats.disconnect_time - conn_stats.connect_time);
print_message("Bytes sent: %d, Bytes received: %d",
              conn_stats.bytes_sent, conn_stats.bytes_received);
```

---

### 7. 타임아웃 개선

**개선 방안:**
```c
// AT 명령별로 다른 타임아웃 사용
#define AT_INIT_TIMEOUT     5   // 초기화 명령
#define AT_ANSWER_TIMEOUT  60   // ATA 명령 (1분)
#define AT_HANGUP_TIMEOUT   5   // 종료 명령

// wait_for_ring()에 전체 타임아웃 외에 idle 타임아웃 추가
#define RING_IDLE_TIMEOUT  10   // RING 간격 타임아웃
```

---

## 🎯 우선순위

### Priority 1 (즉시 구현 필요)
1. ✅ ATA 명령 추가
2. ✅ CONNECT 응답 감지
3. ✅ Carrier detect 활성화

### Priority 2 (중요)
4. ✅ DTR drop hangup
5. ⚪ 포트 잠금

### Priority 3 (선택)
6. ⚪ 연결 통계
7. ⚪ 타임아웃 개선

---

## 📌 참고

- **process_sample.txt 2단계**: ATA 명령 및 CONNECT 응답
- **process_sample.txt 5단계**: Carrier detect 메커니즘
- **process_sample.txt 8단계**: DTR drop hangup
- **process_sample.txt 9단계**: 모뎀 응답 처리

---

## 🔄 수정 후 테스트 시나리오

1. 프로그램 시작
2. 시리얼 포트 초기화 (CLOCAL 활성)
3. 모뎀 초기화 (ATZ 등)
4. 자동응답 설정 (S0=0)
5. **RING 신호 대기 (2회)**
6. **ATA 명령 전송** ← 새로 추가
7. **CONNECT 응답 대기** ← 새로 추가
8. **Carrier detect 활성화** ← 새로 추가
9. 10초 대기
10. "first" 전송
11. 5초 대기
12. "second" 전송
13. **ATH + DTR drop** ← 개선
14. 포트 닫기 및 잠금 해제

---

이 개선사항들을 적용하면 process_sample.txt에서 설명하는
완전한 modem 통신 흐름에 더욱 가까워집니다.
