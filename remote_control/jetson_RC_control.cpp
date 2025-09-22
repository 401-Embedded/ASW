#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>

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
    tcgetattr(0, &orig_termios); // 원래 모드 저장
    struct termios new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &new_termios);
}

void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
}
int kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

// ---------------- Control ----------------
#define USE_BINARY 0   // 0 = 텍스트 기반, 1 = 바이너리 기반

void send_command(int fd, int16_t left, int16_t right, int16_t steer) {
#if USE_BINARY
    uart_send_binary(fd, left, right, steer);
#else
    uart_send_text(fd, left, right, steer);
#endif
}

void handle_key(int fd) {
    int c = getchar();

    if (c == 27) { // ESC 시퀀스 (방향키)
        if (getchar() == 91) {
            switch(getchar()) {
                case 'A': // ↑
                    send_command(fd, 255, 255, 0);
                    std::cout << "↑ Forward" << std::endl;
                    break;
                case 'B': // ↓
                    send_command(fd, -255, -255, 0);
                    std::cout << "↓ Backward" << std::endl;
                    break;
                case 'C': // →
                    send_command(fd, 255, 220, 45);
                    std::cout << "→ Right" << std::endl;
                    break;
                case 'D': // ←
                    send_command(fd, 220, 255, -30);
                    std::cout << "← Left" << std::endl;
                    break;
            }
        }
    }
    else if (c == 'q' || c == 'Q') {
        std::cout << "프로그램 종료" << std::endl;
        reset_terminal_mode();
        exit(0);
    }
}

// ---------------- Main ----------------
int main() {
    int fd = uart_open("/dev/ttyUSB0", 9600);
    if (fd == -1) return -1;

    std::cout << "Jetson RC Control Start" << std::endl;
    std::cout << "방향키로 제어, q = 종료" << std::endl;

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
