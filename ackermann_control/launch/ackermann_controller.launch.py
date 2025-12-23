#!/usr/bin/env python3
"""
Launch file for Ackermann Controller
"""
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    return LaunchDescription([
        # Declare launch arguments
        DeclareLaunchArgument(
            'uart_port',
            default_value='/dev/ttyACM0',
            description='UART port for Arduino communication'
        ),
        DeclareLaunchArgument(
            'baud_rate',
            default_value='9600',
            description='UART baud rate'
        ),
        DeclareLaunchArgument(
            'wheelbase',
            default_value='0.175',
            description='Vehicle wheelbase in meters (default: 17.5cm)'
        ),
        DeclareLaunchArgument(
            'track_width',
            default_value='0.205',
            description='Vehicle track width in meters (default: 20.5cm)'
        ),
        DeclareLaunchArgument(
            'max_speed',
            default_value='1.0',
            description='Maximum speed in m/s'
        ),
        DeclareLaunchArgument(
            'max_steering_angle',
            default_value='60.0',
            description='Maximum steering angle in degrees'
        ),
        DeclareLaunchArgument(
            'cmd_vel_topic',
            default_value='/cmd_vel',
            description='Topic to subscribe for velocity commands'
        ),
        DeclareLaunchArgument(
            'enable_differential',
            default_value='True',
            description='Enable differential wheel speed control'
        ),
        
        # Ackermann controller node
        Node(
            package='ackermann_control',
            executable='ackermann_controller',
            name='ackermann_controller',
            output='screen',
            parameters=[{
                'uart_port': LaunchConfiguration('uart_port'),
                'baud_rate': LaunchConfiguration('baud_rate'),
                'wheelbase': LaunchConfiguration('wheelbase'),
                'track_width': LaunchConfiguration('track_width'),
                'max_speed': LaunchConfiguration('max_speed'),
                'max_steering_angle': LaunchConfiguration('max_steering_angle'),
                'cmd_vel_topic': LaunchConfiguration('cmd_vel_topic'),
                'enable_differential': LaunchConfiguration('enable_differential'),
            }]
        ),
    ])
