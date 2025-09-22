g++ camera_stream.cpp -o camera_stream \
    `pkg-config --cflags --libs opencv4` -lpthread

./camera_stream

## 접속
http://<Jetson_IP>:8080/stream
