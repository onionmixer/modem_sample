# Modem Sample Program

외장 모뎀을 시리얼 포트를 통해 테스트하는 샘플 프로그램입니다.

## 기능

1. 시리얼 포트 초기화 (8N1, 9600bps)
2. 모뎀 초기화 (AT 명령)
3. 자동응답 설정
4. RING 신호 감지 (2회)
5. 데이터 전송 ("first", "second")
6. 모뎀 연결 해제

## 빌드

```bash
make
```

## 사용법

```bash
./modem_sample
```

**주의**: 시리얼 포트 접근 권한이 필요합니다.

```bash
# 현재 사용자를 dialout 그룹에 추가
sudo usermod -a -G dialout $USER

# 또는 sudo로 실행
sudo ./modem_sample
```

## 설정

프로그램 설정은 `modem_sample.h` 파일에서 변경할 수 있습니다:

- `SERIAL_PORT`: 시리얼 포트 경로 (기본: /dev/ttyUSB0)
- `BAUDRATE`: 통신 속도 (기본: 9600)
- `MODEM_INIT_COMMAND`: 모뎀 초기화 명령
- `MODEM_AUTOANSWER_COMMAND`: 자동응답 설정 명령

## 파일 구조

- `modem_sample.h` - 헤더 파일
- `modem_sample.c` - 메인 프로그램
- `serial_port.c` - 시리얼 포트 처리
- `modem_control.c` - 모뎀 제어
- `Makefile` - 빌드 설정
- `TODO.txt` - 개발 계획 및 참고 사항

## 참고

이 프로그램은 MBSE BBS의 mbcico 모듈 코드를 참고하여 작성되었습니다.

주요 참고 파일:
- mbcico/openport.c - 시리얼 포트 초기화
- mbcico/ttyio.c - TTY I/O 처리
- mbcico/dial.c - 모뎀 제어

## 라이선스

GPL v2 (MBSE BBS 라이선스 준수)
