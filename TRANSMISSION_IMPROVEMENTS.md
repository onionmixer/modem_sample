# 데이터 전송 로직 개선 보고서

작성일: 2025-10-16
참고: MBSE BBS 소스코드 (../mbse_source/)

## 개요

info_send.txt에 정리된 MBSE BBS 시스템의 데이터 전송 프로세스를 분석하고,
현재 modem_sample 프로그램의 데이터 전송 로직에 개선사항을 적용하였습니다.

## MBSE BBS 소스코드 분석 결과

### 1. 핵심 I/O 구조 (mbcico/ttyio.c, mbsebbs/ttyio.c)

**발견사항:**
- `tty_write()`: file descriptor 1(stdout)로 직접 write
- `tty_read()`: select()와 타이머를 활용한 timeout 관리
- 버퍼링 시스템: TT_BUFSIZ(1024바이트) 버퍼 + left/next 포인터
- 상태 관리: tty_status 전역 변수 (STAT_OK, STAT_ERROR, STAT_TIMEOUT, STAT_HANGUP)

**에러 감지:**
```c
// mbcico/ttyio.c:256-258
if (hanged_up || (errno == EPIPE) || (errno == ECONNRESET)) {
    tty_status = STAT_HANGUP;
}
```

### 2. Carrier Detection (mbcico/openport.c)

**핵심 함수:**
- `linedrop()` (line 46-51): SIGHUP 시그널 핸들러로 carrier 손실 감지
- `tty_nolocal()` (line 458-473): CLOCAL 비활성화 + CRTSCTS 활성화
- `tty_local()` (line 424-454): DTR drop으로 hangup 수행

**패턴:**
```c
// 연결 설정 시
tios.c_cflag |= CLOCAL;  // Carrier 신호 무시

// 데이터 전송 시 (answer.c:113-116)
rawport();
nolocalport();  // CLOCAL 비활성화 -> Carrier 감지 활성화
```

### 3. Terminal Output (mbsebbs/term.c)

**출력 계층:**
```
clear() -> colour() -> pout() -> PUTSTR() -> tty_write() -> write(1, buf, size)
```

**ANSI 제어:**
- `clear()`: ANSI_HOME + ANSI_CLEAR
- `colour()`: ANSI escape 코드로 색상 제어

## 구현한 개선사항

### 1. Carrier 상태 감지 및 검증

**새로운 함수:**
```c
int check_carrier_status(int fd);            // TIOCMGET으로 DCD 상태 확인
int verify_carrier_before_send(int fd);      // 전송 전 carrier 검증
```

**참고 소스:**
- MBSE mbcico/openport.c의 carrier 관리 패턴
- ioctl(fd, TIOCMGET, &status) 사용

### 2. Robust 전송 함수 (재시도 로직)

**새로운 함수:**
```c
int robust_serial_write(int fd, const char *data, int len);
```

**개선 내용:**
1. **부분 전송 처리**: result != size 감지 및 재전송
2. **재시도 로직**: EAGAIN/EWOULDBLOCK에서 최대 3회 재시도
3. **Carrier 검증**: 전송 전 carrier 상태 확인
4. **Hangup 감지**: EPIPE/ECONNRESET 에러 감지

**참고 소스:**
- MBSE mbcico/ttyio.c:248-268의 tty_write() 에러 처리 패턴
- MBSE의 hanged_up 플래그 패턴

### 3. 버퍼링 전송 (대용량 데이터)

**새로운 함수:**
```c
int buffered_serial_send(int fd, const char *data, int len);
```

**특징:**
- 256바이트 청크로 분할 전송
- 청크 간 10ms 지연으로 수신측 버퍼 오버플로우 방지
- 대용량 전송 시 진행률 표시

**참고 소스:**
- MBSE의 버퍼링 개념 (TT_BUFSIZ, 청크 단위 처리)

### 4. 전송 로깅 (디버깅)

**새로운 함수:**
```c
void log_transmission(const char *label, const char *data, int len);
```

**기능:**
- 헥스 덤프 출력 (최대 32바이트)
- 전송 데이터 가시화

### 5. Handshake 프로토콜 지원

**새로운 함수:**
```c
int wait_for_client_ready(int fd, const char *ready_string, int timeout);
```

**기능:**
- 클라이언트로부터 특정 문자열 수신 대기
- 동적 타이밍 (클라이언트 준비 상태 기반)
- Timeout 및 중단 처리

### 6. 향상된 에러 코드 체계

**추가된 상태 코드:**
```c
#define ERROR_HANGUP    -5

#define STAT_OK         0
#define STAT_ERROR      1
#define STAT_TIMEOUT    2
#define STAT_EOF        3
#define STAT_HANGUP     4
```

**참고 소스:**
- MBSE mbcico/ttyio.c의 tty_status 및 ttystat[] 배열

## main program 변경사항

### 기존 코드:
```c
rc = serial_write(serial_fd, "first\n\r", 7);
if (rc < 0) {
    print_error("Failed to send first message");
    goto cleanup;
}
```

### 개선된 코드:
```c
// 전송 로깅
log_transmission("FIRST", "first\n\r", 7);

// Robust 전송 (carrier 체크 + 재시도)
rc = robust_serial_write(serial_fd, "first\n\r", 7);
if (rc < 0) {
    if (rc == ERROR_HANGUP) {
        print_error("Carrier lost while sending 'first' message");
    } else {
        print_error("Failed to send 'first' message (error: %d)", rc);
    }
    goto cleanup;
}

// 전송 간 carrier 상태 확인
rc = verify_carrier_before_send(serial_fd);
if (rc != SUCCESS) {
    print_error("Carrier check failed before second transmission");
    goto cleanup;
}
```

## 개선 효과

### 1. 전송 안정성 향상
- ✅ Carrier 손실 즉시 감지
- ✅ 일시적 오류에 대한 자동 재시도
- ✅ 부분 전송 처리

### 2. 에러 진단 개선
- ✅ 명확한 에러 원인 구분 (carrier 손실 vs 일반 오류)
- ✅ 전송 데이터 헥스 덤프
- ✅ 상세한 로그 메시지

### 3. 대용량 데이터 지원
- ✅ 청크 단위 버퍼링 전송
- ✅ 수신측 버퍼 오버플로우 방지
- ✅ 진행률 모니터링

### 4. 유연한 프로토콜
- ✅ 클라이언트 준비 상태 기반 전송
- ✅ 고정 타이밍 대신 동적 handshake
- ✅ 양방향 통신 지원

## 비교표: 기존 vs 개선

| 항목 | 기존 구현 | 개선 구현 |
|------|----------|----------|
| **Carrier 감지** | 활성화만 함 (미사용) | 전송 전/중 실시간 감지 |
| **재시도 로직** | 없음 (즉시 실패) | 최대 3회 재시도 (EAGAIN/EWOULDBLOCK) |
| **부분 전송** | 미처리 | 완전 전송까지 재시도 |
| **에러 구분** | 단순 실패 | HANGUP/TIMEOUT/ERROR 구분 |
| **타이밍** | 고정 (10초, 5초) | 동적 (handshake 지원) |
| **디버깅** | 기본 메시지 | 헥스 덤프 + 상세 로그 |
| **대용량 데이터** | 지원 안함 | 청크 단위 버퍼링 전송 |

## MBSE BBS 패턴 적용 요약

### 1. I/O 패턴
✅ select() 기반 timeout 관리
✅ 버퍼링 시스템 (청크 단위 처리)
✅ tty_status 전역 상태 관리

### 2. Carrier 관리
✅ CLOCAL 플래그 제어
✅ TIOCMGET을 통한 DCD 감지
✅ DTR drop hangup

### 3. 에러 처리
✅ EPIPE/ECONNRESET 감지
✅ hanged_up 플래그 패턴
✅ 상태 코드 체계 (STAT_*)

### 4. 전송 계층화
✅ 저수준 I/O (robust_serial_write)
✅ 중간 계층 (buffered_serial_send)
✅ 고수준 로직 (main program)

## 테스트 권장사항

### 1. 정상 전송 테스트
```bash
sudo ./modem_sample
# 예상: carrier 체크 → 전송 성공 → 헥스 덤프 출력
```

### 2. Carrier 손실 시뮬레이션
- 전송 중 물리적 연결 해제
- 예상 동작: ERROR_HANGUP 감지 및 즉시 정리

### 3. 대용량 데이터 테스트
```c
// main()에서 테스트:
char large_data[2048];
memset(large_data, 'A', sizeof(large_data));
buffered_serial_send(serial_fd, large_data, sizeof(large_data));
```

### 4. 재시도 로직 테스트
- EAGAIN을 유발하는 환경 (busy line)
- 예상 동작: 자동 재시도 후 성공

## 참고 자료

### MBSE BBS 소스 파일
- `../mbse_source/mbcico/ttyio.c`: 저수준 I/O 구현
- `../mbse_source/mbcico/openport.c`: 포트 관리 및 carrier 제어
- `../mbse_source/mbsebbs/ttyio.c`: BBS I/O 구현
- `../mbse_source/mbsebbs/term.c`: 터미널 출력 함수
- `../mbse_source/mbcico/answer.c`: 수신 응답 처리

### 프로젝트 파일
- `info_send.txt`: 데이터 전송 프로세스 정리
- `process_sample.txt`: 전체 처리 과정

## 결론

MBSE BBS의 30년 이상 검증된 전송 로직 패턴을 적용하여,
현재 modem_sample 프로그램의 안정성, 진단성, 확장성을 크게 향상시켰습니다.

특히:
1. **Carrier 감지 실시간 적용**으로 연결 문제 즉시 대응
2. **재시도 로직**으로 일시적 네트워크 문제 극복
3. **상세 로깅**으로 문제 진단 시간 단축
4. **버퍼링 전송**으로 대용량 데이터 지원

이는 실제 운영 환경에서 필수적인 기능들입니다.
