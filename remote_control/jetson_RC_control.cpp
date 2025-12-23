#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/select.h>

// ---------------- UART ----------------
int uart_open(const char* device, int baudrate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        std::cerr << "❌ UART open error" << std::endl;
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, B9600);  // baudrate = 9600 (아두이노와 맞춤)
    cfsetospeed(&options, B9600);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

void uart_send_text(int fd, int16_t left, int16_t right, int16_t steer) {
    char buf[64];
    snprintf(buf, sizeof(buf), "L=%d,R=%d,S=%d\n", left, right, steer);
    write(fd, buf, strlen(buf));
}

void uart_send_binary(int fd, int16_t left, int16_t right, int16_t steer) {
    uint8_t buf[8];
    buf[0] = 0xAA;  // 헤더
    buf[1] = left & 0xFF;
    buf[2] = (left >> 8) & 0xFF;
    buf[3] = right & 0xFF;
    buf[4] = (right >> 8) & 0xFF;
    buf[5] = steer & 0xFF;
    buf[6] = (steer >> 8) & 0xFF;
    buf[7] = buf[1] ^ buf[2] ^ buf[3] ^ buf[4] ^ buf[5] ^ buf[6]; // 체크섬
    write(fd, buf, 8);
}

// ---------------- Keyboard ----------------
struct termios orig_termios;

void set_conio_terminal_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    new_termios.c_cc[VMIN] = 0;   // Non-blocking read
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);  // Set non-blocking
}

void reset_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

// 키 상태 추적
struct KeyState {
    bool up;
    bool down;
    bool left;
    bool right;
    bool space;
};

KeyState key_state = {false, false, false, false, false};

// ---------------- Control ----------------
#define USE_BINARY 0   // 0 = 텍스트 기반, 1 = 바이너리 기반

void send_command(int fd, int16_t left, int16_t right, int16_t steer) {
#if USE_BINARY
    uart_send_binary(fd, left, right, steer);
#else
    uart_send_text(fd, left, right, steer);
#endif
// ---------------- Control ----------------
#define USE_BINARY 0   // 0 = 텍스트 기반, 1 = 바이너리 기반

void send_command(int fd, int16_t left, int16_t right, int16_t steer) {
#if USE_BINARY
    uart_send_binary(fd, left, right, steer);
#else
    uart_send_text(fd, left, right, steer);
#endif
}

// 키보드 입력 읽기 (non-blocking)
void read_keys() {
    int c;
    while ((c = getchar()) != EOF) {
        if (c == 27) { // ESC 시퀀스 (방향키)
            if (getchar() == 91) {
                int arrow = getchar();
                switch(arrow) {
                    case 'A': key_state.up = true; break;     // ↑
                    case 'B': key_state.down = true; break;   // ↓
                    case 'C': key_state.right = true; break;  // →
                    case 'D': key_state.left = true; break;   // ←
                }
            }
        }
        else if (c == ' ') {
            key_state.space = true;
        }
        else if (c == 'q' || c == 'Q') {
            send_command(-1, 0, 0, 0);  // fd는 main에서 처리
            std::cout << "\n프로그램 종료" << std::endl;
            reset_terminal_mode();
            exit(0);
        }
    }
}

// 키 상태 초기화 (timeout 후)
void reset_keys() {
// ---------------- Main ----------------
int main() {
    int fd = uart_open("/dev/ttyACM0", 9600);
    if (fd == -1) return -1;

    std::cout << "🎮 Jetson RC Control - Game Mode" << std::endl;
    std::cout << "방향키: 제어 | Space: 정지 | Q: 종료" << std::endl;
    std::cout << "↑: 전진 | ↓: 후진 | ←/→: 조향" << std::endl;
    std::cout << "조합 가능: ↑+← (왼쪽 회전), ↑+→ (오른쪽 회전)" << std::endl;
    std::cout << std::endl;

    set_conio_terminal_mode();
    atexit(reset_terminal_mode);

    struct timeval last_input_time;
    gettimeofday(&last_input_time, NULL);
    
    while (true) {
        // 키 입력 읽기
        read_keys();
        
        // 현재 시간 체크
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        long elapsed_ms = (current_time.tv_sec - last_input_time.tv_sec) * 1000 +
                         (current_time.tv_usec - last_input_time.tv_usec) / 1000;
        
        // 키가 눌렸으면 타이머 리셋
        if (key_state.up || key_state.down || key_state.left || 
            key_state.right || key_state.space) {
            gettimeofday(&last_input_time, NULL);
        }
        
        // 100ms 이상 입력 없으면 키 상태 초기화 (키를 뗀 것으로 간주)
        if (elapsed_ms > 100) {
            reset_keys();
        }
        
        // 게임 방식 제어 처리
        process_game_control(fd);
        
        usleep(20000); // 20ms (50Hz)
    }

    close(fd);
    reset_terminal_mode();
    return 0;
}       motor_speed = 200;  // 전진 속도
    } else if (key_state.down) {
        motor_speed = -150; // 후진 속도 (약간 느리게)
    } else {
        motor_speed = 0;    // 정지
    }
    
    // 조향 결정
    if (key_state.left) {
        steer_angle = -60;  // 좌회전
    } else if (key_state.right) {
        steer_angle = 60;   // 우회전
    } else {
        steer_angle = 0;    // 직진
    }
    
    // 명령 전송
    send_command(fd, motor_speed, motor_speed, steer_angle);
    
    // 상태 표시
    if (motor_speed > 0 && steer_angle == 0) {
        std::cout << "\r↑ Forward        " << std::flush;
    } else if (motor_speed > 0 && steer_angle < 0) {
        std::cout << "\r↖ Forward Left   " << std::flush;
    } else if (motor_speed > 0 && steer_angle > 0) {
        std::cout << "\r↗ Forward Right  " << std::flush;
    } else if (motor_speed < 0 && steer_angle == 0) {
        std::cout << "\r↓ Backward       " << std::flush;
    } else if (motor_speed < 0 && steer_angle < 0) {
        std::cout << "\r↙ Backward Left  " << std::flush;
    } else if (motor_speed < 0 && steer_angle > 0) {
        std::cout << "\r↘ Backward Right " << std::flush;
    } else if (motor_speed == 0 && steer_angle != 0) {
        std::cout << "\r⚠ Idle (turning) " << std::flush;
    } else {
        std::cout << "\r⏹ Idle           " << std::flush;
    }
}
    set_conio_terminal_mode();
    atexit(reset_terminal_mode); // 비정상 종료에도 복구

    while (true) {
        if (kbhit()) {
            handle_key(fd);
        }
        usleep(10000); // 10ms sleep
    }

    close(fd);
    reset_terminal_mode(); // 정상 종료 시 복구
    return 0;
}
