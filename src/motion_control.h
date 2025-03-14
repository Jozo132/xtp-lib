#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>


bool handleAcceleration(float dt, float& speed, float target_speed, float accel, float deccel = 0) {
    deccel = deccel == 0 ? accel : deccel;
    if (speed < target_speed) {
        float rate = (speed >= 0) ? accel : deccel;
        speed += rate * dt;
        if (speed > target_speed) {
            speed = target_speed;
        }
    } else if (speed > target_speed) {
        float rate = (speed >= 0) ? deccel : accel;
        speed -= rate * dt;
        if (speed < target_speed) {
            speed = target_speed;
        }
    }
    return speed == target_speed;
}


// Based on trapzoidal velocity profile, calculate if we were to start accellerating towards target speed, would we reach it in before the end of the move
bool handleAcceleration(float dt, float speed, float target_speed, float accel, float deccel, float distance) {
    deccel = deccel == 0 ? accel : deccel;
    float time_to_target = 0;
    float distance_to_target = 0;
    float time_to_deccel = 0;
    float distance_to_deccel = 0;
    float time_to_accel = 0;
    float distance_to_accel = 0;
    float time_to_cruise = 0;
    float distance_to_cruise = 0;
    float cruise_speed = 0;
    float cruise_distance = 0;
    float accel_distance = 0;
    float deccel_distance = 0;

    if (speed < target_speed) {
        time_to_accel = (target_speed - speed) / accel;
        distance_to_accel = (speed * time_to_accel) + (0.5 * accel * time_to_accel * time_to_accel);
        if (distance_to_accel > distance) {
            time_to_accel = sqrt((2 * distance) / accel);
            distance_to_accel = distance;
        }
    } else if (speed > target_speed) {
        time_to_deccel = (speed - target_speed) / deccel;
        distance_to_deccel = (speed * time_to_deccel) - (0.5 * deccel * time_to_deccel * time_to_deccel);
        if (distance_to_deccel > distance) {
            time_to_deccel = sqrt((2 * distance) / deccel);
            distance_to_deccel = distance;
        }
    }

    time_to_cruise = (distance - distance_to_accel - distance_to_deccel) / target_speed;
    distance_to_cruise = distance - distance_to_accel - distance_to_deccel;
    cruise_speed = target_speed;
    cruise_distance = distance_to_cruise;

    time_to_target = time_to_accel + time_to_cruise + time_to_deccel;
    distance_to_target = distance_to_accel + distance_to_cruise + distance_to_deccel;

    return time_to_target * 1000000 < dt;
}