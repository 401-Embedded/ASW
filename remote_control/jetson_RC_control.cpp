#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <cmath>

// ---------------- Vehicle Geometry ----------------
const float WHEELBASE = 17.5f; // cm
const float TRACK = 20.5f;     // cm

// ---------------- UART ----------------
int uart_open(const char* device, int baudrate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        std::cerr << "❌ UART open error" << std::endl;
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

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

void uart_send_binary(int fd, int16_t left, int16_t right, int16_t steer) {
    uint8_t buf[8];
    buf[0] = 0xAA;
    buf[1] = left & 0xFF;
    buf[2] = (left >> 8) & 0xFF;
    buf[3] = right & 0xFF;
    buf[4] = (right >> 8) & 0xFF;
    buf[5] = steer & 0xFF;
    buf[6] = (steer >> 8) & 0xFF;
    buf[7] = buf[1] ^ buf[2] ^ buf[3] ^ buf[4] ^ buf[5] ^ buf[6];
    write(fd, buf, 8);
}

// ---------------- Keyboard ----------------
struct termios orig_termios;

void set_conio_terminal_mode() {
    tcgetattr(0, &orig_termios);
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

// ---------------- Vehicle Control ----------------
float speed = 0.0f;         // -255 ~ +255
float steer_angle = 0.0f;   // -90 ~ +90
const float SPEED_STEP = 15.0f;
const float STEER_STEP = 5.0f;
const float MAX_SPEED = 255.0f;
const float MAX_STEER = 60.0f;

void compute_motor_output(int16_t& left, int16_t& right) {
    if (fabs(steer_angle) < 1e-2) {
        left = right = (int16_t)speed;
        return;
    }

    float theta = steer_angle * M_PI / 180.0;
    float R = WHEELBASE / tan(theta);

    float v_inner = speed * (R - TRACK/2) / R;
    float v_outer = speed * (R + TRACK/2) / R;

    if (steer_angle > 0) { // 우회전
        left  = (int16_t)v_inner;
        right = (int16_t)v_outer;
    } else { // 좌회전
        left  = (int16_t)v_outer;
        right = (int16_t)v_inner;
    }
}

// ---------------- Main ----------------
int main() {
    int fd = uart_open("/dev/ttyACM0", 115200);
    if (fd == -1) return -1;

    std::cout << "🚗 Jetson Manual RC Control" << std::endl;
    std::cout << "↑↓: 속도, ←→: 조향, SPACE: 정지, q: 종료" << std::endl;

    set_conio_terminal_mode();
    atexit(reset_terminal_mode);

    while (true) {
        if (kbhit()) {
            int c = getchar();

            if (c == 27 && getchar() == 91) {
                switch (getchar()) {
                    case 'A': // ↑
                        speed += SPEED_STEP;
                        if (speed > MAX_SPEED) speed = MAX_SPEED;
                        break;
                    case 'B': // ↓
                        speed -= SPEED_STEP;
                        if (speed < -MAX_SPEED) speed = -MAX_SPEED;
                        break;
                    case 'C': // →
                        steer_angle += STEER_STEP;
                        if (steer_angle > MAX_STEER) steer_angle = MAX_STEER;
                        break;
                    case 'D': // ←
                        steer_angle -= STEER_STEP;
                        if (steer_angle < -MAX_STEER) steer_angle = -MAX_STEER;
                        break;
                }
            }
            else if (c == ' ') {
                speed = 0;
            }
            else if (c == 'q' || c == 'Q') {
                speed = 0;
                uart_send_binary(fd, 0, 0, 0);
                std::cout << "프로그램 종료" << std::endl;
                break;
            }

            int16_t left = 0, right = 0;
            compute_motor_output(left, right);
            uart_send_binary(fd, left, right, (int16_t)steer_angle);

            std::cout << "\rSpeed: " << speed
                      << " | Steer: " << steer_angle
                      << " | L: " << left
                      << " | R: " << right << "   " << std::flush;
        }

        usleep(2000); // 2ms
    }

    close(fd);
    reset_terminal_mode();
    return 0;
}
