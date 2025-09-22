#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>

#include <opencv2/opencv.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

// ===== 공유 프레임 =====
struct SharedFrame {
    cv::Mat latest;                 // 최신 프레임 (BGR)
    std::mutex m;
    std::condition_variable cv;
    bool has_new = false;
    std::atomic<bool> stop{false};
};

// Jetson용 GStreamer 파이프라인
static std::string jetson_gst_pipeline(int width, int height, int fps, int sensor_id=0) {
    std::ostringstream ss;
    ss << "nvarguscamerasrc sensor-id=" << sensor_id << " ! "
       << "video/x-raw(memory:NVMM),width=" << width
       << ",height=" << height
       << ",framerate=" << fps << "/1 ! "
       << "nvvidconv ! video/x-raw,format=BGRx ! "
       << "videoconvert ! video/x-raw,format=BGR ! "
       << "appsink max-buffers=1 drop=true sync=false";
    return ss.str();
}

// 클라이언트 하나에 MJPEG 스트림 전송
static void handle_client(int client_fd, SharedFrame* shared, int jpeg_quality = 80) {
    // HTTP 헤더 전송
    std::string header =
        "HTTP/1.0 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    send(client_fd, header.c_str(), header.size(), 0);

    std::vector<uchar> jpgBuf;
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, jpeg_quality };

    while (!shared->stop.load()) {
        cv::Mat frame;
        {
            std::unique_lock<std::mutex> lk(shared->m);
            // 새 프레임을 기다림 (최대 500ms)
            shared->cv.wait_for(lk, std::chrono::milliseconds(500), [&]{
                return shared->stop.load() || shared->has_new;
            });
            if (shared->stop.load()) break;
            if (!shared->latest.empty()) shared->latest.copyTo(frame);
            shared->has_new = false;
        }
        if (frame.empty()) continue;

        // JPEG 인코딩
        jpgBuf.clear();
        if (!cv::imencode(".jpg", frame, jpgBuf, params)) {
            std::cerr << "[WARN] JPEG 인코딩 실패\n";
            continue;
        }

        // 멀티파트 조각 작성 및 전송
        std::ostringstream oss;
        oss << "--frame\r\n"
            << "Content-Type: image/jpeg\r\n"
            << "Content-Length: " << jpgBuf.size() << "\r\n\r\n";
        std::string partHeader = oss.str();

        if (send(client_fd, partHeader.c_str(), partHeader.size(), 0) <= 0) break;
        if (send(client_fd, (const char*)jpgBuf.data(), jpgBuf.size(), 0) <= 0) break;
        if (send(client_fd, "\r\n", 2, 0) <= 0) break;

        // 프레임 간 텀 (원하는 FPS에 맞춰 조정)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(client_fd);
}

// 매우 단순한 HTTP 서버 (GET /stream 만 처리)
static void http_server(SharedFrame* shared, int port = 8080) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return;
    }
    if (listen(server_fd, 8) < 0) {
        perror("listen");
        close(server_fd);
        return;
    }

    std::cerr << "[INFO] MJPEG server listening on http://<HOST>:" << port << "/stream\n";

    while (!shared->stop.load()) {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        int client_fd = accept(server_fd, (sockaddr*)&cli, &clen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        // 간단한 리퀘스트 파싱 (첫 줄만)
        char buf[1024]; ssize_t n = recv(client_fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) { close(client_fd); continue; }
        buf[n] = '\0';
        std::string req(buf);
        // GET /stream -> MJPEG, 그 외 -> 간단 안내 페이지
        if (req.find("GET /stream") == 0) {
            std::thread(handle_client, client_fd, shared, 80).detach();
        } else {
            std::string page =
                "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
                "<html><body>"
                "<h3>MJPEG stream</h3>"
                "<img src=\"/stream\" />"
                "</body></html>";
            send(client_fd, page.c_str(), page.size(), 0);
            close(client_fd);
        }
    }

    close(server_fd);
}

int main() {
    // 카메라 파라미터
    int camWidth = 1280, camHeight = 720, camFps = 30;

    // GStreamer 파이프라인을 OpenCV로 오픈
    std::string pipeline = jetson_gst_pipeline(camWidth, camHeight, camFps, /*sensor_id=*/0);
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened()) {
        std::cerr << "[ERR] 카메라 파이프라인 오픈 실패\n";
        std::cerr << "pipeline: " << pipeline << std::endl;
        return 1;
    }

    SharedFrame shared;

    // 캡처 스레드
    std::thread captureThread([&]{
        cv::Mat frame;
        while (!shared.stop.load()) {
            if (!cap.read(frame) || frame.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (frame.channels() == 4) cv::cvtColor(frame, frame, cv::COLOR_BGRA2BGR);
            {
                std::lock_guard<std::mutex> lk(shared.m);
                frame.copyTo(shared.latest);
                shared.has_new = true;
            }
            shared.cv.notify_one();
        }
    });

    // HTTP 서버 스레드
    std::thread serverThread(http_server, &shared, /*port=*/8080);

    std::cerr << "[INFO] 브라우저에서 접속:  http://<Jetson_IP>:8080/stream\n";
    std::cerr << "[INFO] 종료하려면 Ctrl+C\n";

    // 메인 스레드는 대기
    // (간단히 키 입력 대기 또는 sleep로 대체 가능)
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(60));

    // 종료 처리(도달하지 않음; 필요시 신호 처리 추가)
    shared.stop.store(true);
    shared.cv.notify_all();
    if (captureThread.joinable()) captureThread.join();
    if (serverThread.joinable()) serverThread.join();
    return 0;
}
