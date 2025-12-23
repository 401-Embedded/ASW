# Ackermann Control

ROS2 패키지: cmd_vel을 애크먼 스티어링 제어 신호로 변환하여 UART를 통해 아두이노로 전송

## 개요

이 패키지는 Nav2 등의 자율주행 스택에서 발행하는 `/cmd_vel` (Twist 메시지)를 구독하여, 애크먼 기하학을 적용한 후 모터와 서보 제어 명령으로 변환. 
변환된 명령을 UART를 통해 아두이노로 전송.

### 주요 기능

- **애크먼 스티어링 변환**: `linear.x`와 `angular.z`를 애크먼 기하학 공식으로 변환
- **디퍼렌셜 제어**: 회전 시 내륜과 외륜의 속도를 자동으로 조절 (실제 차량의 디퍼렌셜 기어 효과)
- **UART 통신**: 텍스트 기반 프로토콜로 아두이노와 통신
- **ROS2 파라미터 지원**: 런타임 설정 가능한 차량 파라미터

## 차량 파라미터

- **Wheelbase**: 17.5 cm (0.175 m) - 앞뒤 바퀴 간격
- **Track Width**: 20.5 cm (0.205 m) - 좌우 바퀴 간격
- **모터 PWM 범위**: -255 ~ 255
- **스티어링 각도 범위**: -60° ~ 60° lanuch 파일에서 default 값 변경 가능
- **구동 방식**: 2WD (후륜구동), 좌우 독립 모터 제어

## 설치

```bash
cd ~/ros2_ws/src
# git clone 진행

cd ~/ros2_ws
colcon build --packages-select ackermann_control
source install/setup.bash
```

## 사용법

### 기본 실행

```bash
ros2 launch ackermann_control ackermann_controller.launch.py
```

### 커스텀 파라미터로 실행

```bash
ros2 launch ackermann_control ackermann_controller.launch.py \
  uart_port:=/dev/ttyACM0 \
  baud_rate:=9600 \
  wheelbase:=0.175 \
  max_speed:=1.0 \
  max_steering_angle:=60.0 \
  cmd_vel_topic:=/cmd_vel
```

### 노드만 직접 실행

```bash
ros2 run ackermann_control ackermann_controller
```

## 토픽

### Subscribed Topics

- `/cmd_vel` (geometry_msgs/Twist): 속도 명령
  - `linear.x`: 전진 속도 (m/s)
  - `angular.z`: 각속도 (rad/s)

### Published Topics

없음 (UART를 통해 하드웨어로 직접 전송)

## 파라미터

| 파라미터 | 타입 | 기본값 | 설명 |
|---------|------|--------|------|
| `uart_port` | string | `/dev/ttyACM0` | 아두이노 UART 포트 |
| `baud_rate` | int | `9600` | UART 통신 속도 |
| `wheelbase` | double | `0.175` | 차량 축간 거리 (m) |
| `track_width` | double | `0.205` | 차량 윤거/좌우 바퀴 간격 (m) |
| `max_speed` | double | `1.0` | 최대 속도 (m/s) |
| `max_steering_angle` | double | `60.0` | 최대 조향각 (도) |
| `cmd_vel_topic` | string | `/cmd_vel` | 구독할 속도 명령 토픽 |
| `enable_differential` | bool | `true` | 디퍼렌셜 제어 활성화 |

## 애크먼 변환 공식

### 1. 전진/후진 시

```
steering_angle = atan2(angular_velocity * wheelbase, linear_velocity)
```

### 2. 제자리 회전 시

애크먼 차량은 제자리 회전이 불가능하므로, 최소 반경 회전을 수행합니다:
- 최대 조향각을 적용
- 느린 속도로 전진하여 타이트한 회전 반경 확보
- 속도는 angular velocity에 비례하되 안전을 위해 최대 80 PWM으로 제한

### 3. 모터 PWM 계산

```
motor_pwm = (linear_velocity / max_speed) * 255
```

### 4. 디퍼렌셜 제어 (내륜차/외륜차 보정)

회전 시 실제 차량의 디퍼렌셜 기어처럼 내륜과 외륜의 속도를 다르게 제어:

```
turning_radius = wheelbase / tan(steering_angle)
inner_radius = turning_radius - track_width/2
outer_radius = turning_radius + track_width/2

speed_ratio = inner_radius / outer_radius

# 오른쪽 회전 시:
left_motor = base_speed      (외륜 - 빠름)
right_motor = base_speed * speed_ratio  (내륜 - 느림)

# 왼쪽 회전 시:
left_motor = base_speed * speed_ratio   (내륜 - 느림)
right_motor = base_speed     (외륜 - 빠름)
```


## UART 프로토콜

아두이노로 전송되는 명령 형식:

```
L=<left_motor>,R=<right_motor>,S=<steering>\n
```

예시:
```
L=200,R=200,S=30\n  # 앞으로 이동하면서 오른쪽으로 30도 조향
L=-150,R=-150,S=-45\n  # 뒤로 이동하면서 왼쪽으로 45도 조향
L=0,R=0,S=0\n  # 정지
```



## 문제 해결

### UART 포트를 찾을 수 없음

```bash
# 연결된 USB 장치 확인
ls /dev/ttyACM* /dev/ttyUSB*

# 권한 문제 해결
sudo chmod 666 /dev/ttyACM0
# 또는 사용자를 dialout 그룹에 추가
sudo usermod -a -G dialout $USER
# 로그아웃 후 다시 로그인
```

### pyserial이 설치되지 않음

```bash
pip3 install pyserial
```

### cmd_vel이 수신되지 않음

```bash
# 토픽 확인
ros2 topic list
ros2 topic echo /cmd_vel

# 노드 상태 확인
ros2 node list
ros2 node info /ackermann_controller
```
