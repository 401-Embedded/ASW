#!/usr/bin/env python3
"""
Ackermann Steering Controller Node

This node subscribes to /cmd_vel (geometry_msgs/Twist) and converts
linear.x and angular.z to Ackermann steering commands.
Sends control commands via UART to Arduino.

Vehicle parameters:
- Wheelbase: 17.5 cm (0.175 m)
- Motor PWM range: -255 to 255
- Steering angle range: -60 to 60 degrees
"""

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
import serial
import math
import time


class AckermannController(Node):
    def __init__(self):
        super().__init__('ackermann_controller')
        
        # Declare parameters
        self.declare_parameter('uart_port', '/dev/ttyACM0')
        self.declare_parameter('baud_rate', 9600)
        self.declare_parameter('wheelbase', 0.175)  # 17.5 cm in meters
        self.declare_parameter('track_width', 0.205)  # 20.5 cm in meters
        self.declare_parameter('max_speed', 1.0)  # m/s
        self.declare_parameter('max_steering_angle', 60.0)  # degrees
        self.declare_parameter('cmd_vel_topic', '/cmd_vel')
        self.declare_parameter('enable_differential', True)  # Enable differential control
        
        # Get parameters
        uart_port = self.get_parameter('uart_port').value
        baud_rate = self.get_parameter('baud_rate').value
        self.wheelbase = self.get_parameter('wheelbase').value
        self.track_width = self.get_parameter('track_width').value
        self.max_speed = self.get_parameter('max_speed').value
        self.max_steering_angle = self.get_parameter('max_steering_angle').value
        cmd_vel_topic = self.get_parameter('cmd_vel_topic').value
        self.enable_differential = self.get_parameter('enable_differential').value
        
        # Initialize UART
        try:
            self.serial_port = serial.Serial(uart_port, baud_rate, timeout=1)
            time.sleep(2)  # Wait for Arduino to reset
            self.get_logger().info(f'✅ UART opened: {uart_port} @ {baud_rate}')
        except Exception as e:
            self.get_logger().error(f'❌ Failed to open UART: {e}')
            raise
        
        # Subscribe to cmd_vel
        self.subscription = self.create_subscription(
            Twist,
            cmd_vel_topic,
            self.cmd_vel_callback,
            10
        )
        
        self.get_logger().info('🚗 Ackermann Controller initialized')
        self.get_logger().info(f'   Wheelbase: {self.wheelbase} m')
        self.get_logger().info(f'   Track width: {self.track_width} m')
        self.get_logger().info(f'   Max speed: {self.max_speed} m/s')
        self.get_logger().info(f'   Max steering: ±{self.max_steering_angle}°')
        self.get_logger().info(f'   Differential control: {"Enabled" if self.enable_differential else "Disabled"}')
        self.get_logger().info(f'   Listening to: {cmd_vel_topic}')
    
    def cmd_vel_callback(self, msg):
        """
        Convert cmd_vel to Ackermann steering commands
        
        Now receiving direct Ackermann commands from Nav2:
        - linear.x: forward velocity (m/s)
        - angular.z: steering angle (radians)
        
        Differential control:
        - Calculate inner/outer wheel speeds based on steering angle
        """
        linear_vel = msg.linear.x  # m/s, forward velocity
        steering_angle_rad = -msg.angular.z  # radians, steering angle from Nav2
        
        # Convert radians to degrees
        steering_angle_deg = math.degrees(steering_angle_rad)
        
        # Clamp steering angle to max range
        steering_angle_deg = max(-self.max_steering_angle, 
                                min(self.max_steering_angle, steering_angle_deg))
        
        # Convert velocity to motor PWM (-255 to 255)
        speed_ratio = linear_vel / self.max_speed
        speed_ratio = max(-1.0, min(1.0, speed_ratio)) #전후진의 최댓값 설정
        base_pwm = int(speed_ratio * 255)
        
        # Apply differential control if enabled and turning
        if self.enable_differential and abs(steering_angle_deg) > 1.0:
            left_motor, right_motor = self.calculate_differential(
                base_pwm, steering_angle_deg
            )
        else:
            left_motor = base_pwm
            right_motor = base_pwm
        
        # Convert steering angle to servo value
        steering_value = int(steering_angle_deg)
        
        # Send command to Arduino
        self.send_uart_command(left_motor, right_motor, steering_value)
    
    def calculate_differential(self, base_speed, steering_angle_deg):
        """
        Calculate differential wheel speeds for Ackermann steering
        
        Theory:
        - Turning creates different path radii for inner and outer wheels
        - Inner wheel travels shorter distance, needs slower speed
        - Outer wheel travels longer distance, maintains base speed
        
        Args:
            base_speed: Base motor PWM (-255 to 255)
            steering_angle_deg: Steering angle in degrees (positive = right turn)
        
        Returns:
            (left_motor_pwm, right_motor_pwm): Differential speeds
        """
        if abs(steering_angle_deg) < 0.1:  # Straight line, no differential needed
            return base_speed, base_speed
        
        # Convert steering angle to radians
        steering_angle_rad = math.radians(abs(steering_angle_deg))
        
        # Calculate turning radius at vehicle center
        # R = L / tan(δ), where L = wheelbase, δ = steering angle
        turning_radius = self.wheelbase / math.tan(steering_angle_rad)
        
        # Calculate inner and outer wheel radii
        # Inner wheel radius = R - track_width/2
        # Outer wheel radius = R + track_width/2
        inner_radius = turning_radius - (self.track_width / 2.0)
        outer_radius = turning_radius + (self.track_width / 2.0)
        
        # Speed ratio: inner_wheel_speed / outer_wheel_speed
        if outer_radius > 0.001:  # Avoid division by zero
            speed_ratio = inner_radius / outer_radius
        else:
            speed_ratio = 1.0
        
        # Determine which wheel is inner/outer based on turn direction
        if steering_angle_deg > 0:  # Right turn
            # Right wheel is inner (slower)
            # Left wheel is outer (faster/base speed)
            left_motor = base_speed
            right_motor = int(base_speed * speed_ratio)
        else:  # Left turn
            # Left wheel is inner (slower)
            # Right wheel is outer (faster/base speed)
            left_motor = int(base_speed * speed_ratio)
            right_motor = base_speed
        
        return left_motor, right_motor
    

    def send_uart_command(self, left_motor, right_motor, steering):
        """
        Send command to Arduino via UART
        Format: "L=left,R=right,S=steer\n"
        """
        try:
            command = f"L={left_motor},R={right_motor},S={steering}\n"
            self.serial_port.write(command.encode('utf-8'))
            self.get_logger().info(f'L={left_motor:4d} R={right_motor:4d} S={steering:3d}°')
        except Exception as e:
            self.get_logger().error(f'UART send error: {e}')
    
    def destroy_node(self):
        """Clean up on shutdown"""
        # Stop the vehicle
        self.send_uart_command(0, 0, 0)
        self.serial_port.close()
        self.get_logger().info('🛑 Ackermann Controller stopped')
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    
    try:
        controller = AckermannController()
        rclpy.spin(controller)
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(f'Error: {e}')
    finally:
        if rclpy.ok():
            controller.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
