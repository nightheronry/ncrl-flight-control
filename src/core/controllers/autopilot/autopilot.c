#include <stdio.h>
#include <stdbool.h>
#include "arm_math.h"
#include "sys_time.h"
#include "polynomial.h"
#include "autopilot.h"
#include "uart.h"
#include "delay.h"
#include "sbus_radio.h"
#include "ahrs.h"
#include "attitude_state.h"
#include "trajectory_following.h"
#include "takeoff_landing.h"
#include "waypoint_following.h"

autopilot_t autopilot;

bool check_motor_lock_condition(bool condition)
{
	return condition; //true: lock, false: unlock
}

void assign_vector_3x1_enu_to_ned(float *ned, float *enu)
{
	ned[0] = enu[1];
	ned[1] = enu[0];
	ned[2] = -enu[2];
}

void autopilot_init(void)
{
	autopilot.mode = AUTOPILOT_MANUAL_FLIGHT_MODE;
	autopilot.armed = false;
	autopilot.motor_locked = false;
	autopilot.landing_speed = 0.0013; //[m/s]
	autopilot.takeoff_speed = 0.0008; //[m/s]
	autopilot.takeoff_height = 1.0f;  //[m]
	autopilot.landing_accept_height_lower = 0.10f; //[m]
	autopilot.landing_accept_height_upper = 0.12f; //[m]
	autopilot.land_avaliable = false;
}

void autopilot_update_uav_state(float pos_enu[3], float vel_enu[3])
{
	autopilot.uav_state.pos[0] = pos_enu[0];
	autopilot.uav_state.pos[1] = pos_enu[1];
	autopilot.uav_state.pos[2] = pos_enu[2];
}

bool autopilot_is_auto_flight_mode(void)
{
	if(autopilot.mode != AUTOPILOT_MANUAL_FLIGHT_MODE &&
	    autopilot.mode != AUTOPILOT_MOTOR_LOCKED_MODE) {
		return true;
	} else {
		return false;
	}
}

bool autopilot_is_armed(void)
{
	return autopilot.armed;
}

void autopilot_lock_motor(void)
{
	/* caution:dangerous function, carefully use! */
	autopilot.motor_locked = true;
}

void autopilot_unlock_motor(void)
{
	/* caution:dangerous function, carefully use! */
	autopilot.motor_locked = false;
}

void autopilot_assign_pos_target_x(float x)
{
	autopilot.wp_now.pos[0] = x;
}

void autopilot_assign_pos_target_y(float y)
{
	autopilot.wp_now.pos[1] = y;
}

void autopilot_assign_pos_target_z(float z)
{
	autopilot.wp_now.pos[2] = z;
}

void autopilot_assign_pos_target(float x, float y, float z)
{
	autopilot.wp_now.pos[0] = x;
	autopilot.wp_now.pos[1] = y;
	autopilot.wp_now.pos[2] = z;
}

void autopilot_assign_vel_target(float vx, float vy, float vz)
{
	autopilot.wp_now.vel[0] = vx;
	autopilot.wp_now.vel[1] = vy;
	autopilot.wp_now.vel[2] = vz;
}

void autopilot_assign_zero_vel_target(void)
{
	autopilot.wp_now.vel[0] = 0.0f;
	autopilot.wp_now.vel[1] = 0.0f;
	autopilot.wp_now.vel[2] = 0.0f;
}

void autopilot_assign_acc_feedforward(float ax, float ay, float az)
{
	autopilot.wp_now.acc_feedforward[0] = ax;
	autopilot.wp_now.acc_feedforward[1] = ay;
	autopilot.wp_now.acc_feedforward[2] = az;
}

void autopilot_assign_zero_acc_feedforward(void)
{
	autopilot.wp_now.acc_feedforward[0] = 0.0f;
	autopilot.wp_now.acc_feedforward[1] = 0.0f;
	autopilot.wp_now.acc_feedforward[2] = 0.0f;
}

void autopilot_set_mode(int new_mode)
{
	autopilot.mode = new_mode;
}

void autopilot_set_armed(void)
{
	autopilot.armed = true;
}

void autopilot_set_disarmed(void)
{
	autopilot.armed = false;
}

int autopilot_get_mode(void)
{
	return autopilot.mode;
}

void autopilot_get_pos_setpoint(float *pos_set)
{
	pos_set[0] = autopilot.wp_now.pos[0];
	pos_set[1] = autopilot.wp_now.pos[1];
	pos_set[2] = autopilot.wp_now.pos[2];
}

void autopilot_get_vel_setpoint(float *vel_set)
{
	vel_set[0] = autopilot.wp_now.vel[0];
	vel_set[1] = autopilot.wp_now.vel[1];
	vel_set[2] = autopilot.wp_now.vel[2];
}

void autopilot_get_accel_feedforward(float *accel_ff)
{
	accel_ff[0] = autopilot.wp_now.acc_feedforward[0];
	accel_ff[1] = autopilot.wp_now.acc_feedforward[1];
	accel_ff[2] = autopilot.wp_now.acc_feedforward[2];
}

int autopilot_trigger_auto_landing(void)
{
	if(autopilot.mode == AUTOPILOT_HOVERING_MODE) {
		autopilot.mode = AUTOPILOT_LANDING_MODE;
		return AUTOPILOT_SET_SUCCEED;
	} else {
		return AUTOPILOT_NOT_IN_HOVERING_MODE;
	}
}

int autopilot_trigger_auto_takeoff(void)
{
	if(autopilot.uav_state.pos[2] < 0.2) {
		autopilot.mode = AUTOPILOT_TAKEOFF_MODE;
		return AUTOPILOT_SET_SUCCEED;
	} else {
		return AUTOPILOT_ALREADY_TAKEOFF;
	}
}

void autopilot_hovering_position_trimming_handler(void)
{
	const float dt = 0.0001;

	/* position setpoint increment in ned body-fixed frame  */
	float x_increment_b = 0.0f;
	float y_increment_b = 0.0f;

	/* position setpoint increment in ned inertial frame */
	float x_increment_i = 0.0f;
	float y_increment_i = 0.0f;

	radio_t rc;
	sbus_rc_read(&rc);

	/* pitch */
	if(rc.pitch > 5.0f || rc.pitch < -5.0f) {
		x_increment_b = rc.pitch * dt;
	}

	/* roll */
	if(rc.roll > 5.0f || rc.roll < -5.0f) {
		y_increment_b = -rc.roll * dt; //TODO: unifying rc sign
	}

	float *R_b2i;
	get_rotation_matrix_b2i(&R_b2i);

	/* position increment in ned inertial frame */
	x_increment_i = R_b2i[0*3 + 0] * x_increment_b + (R_b2i[0*3 + 1] * y_increment_b);
	y_increment_i = R_b2i[1*3 + 0] * x_increment_b + (R_b2i[1*3 + 1] * y_increment_b);

	/* apply increment to autopilot position target variables (enu frame) */
	autopilot.wp_now.pos[0] += y_increment_i;
	autopilot.wp_now.pos[1] += x_increment_i;
}

void autopilot_guidance_handler(void)
{
	/* receive and handle remote controller commands */
	switch(autopilot.mode) {
	case AUTOPILOT_HOVERING_MODE:
		autopilot_hovering_position_trimming_handler();
		break;
	}

	/* if receive halt command */
	if(autopilot.halt_flag == true) {
		autopilot.halt_flag = false;
		autopilot.mode = AUTOPILOT_HOVERING_MODE;

		/* hovering at current position */
		autopilot_assign_pos_target(
		        autopilot.uav_state.pos[0],
		        autopilot.uav_state.pos[1],
		        autopilot.uav_state.pos[2]);
		autopilot_assign_zero_vel_target();
		autopilot_assign_zero_acc_feedforward();
	}

	/* autopilot finite state machine */
	switch(autopilot.mode) {
	case AUTOPILOT_MANUAL_FLIGHT_MODE:
	case AUTOPILOT_HOVERING_MODE:
		break;
	case AUTOPILOT_TRAJECTORY_FOLLOWING_MODE:
		autopilot_trajectory_following_handler();
		break;
	case AUTOPILOT_LANDING_MODE:
		autopilot_landing_handler();
		break;
	case AUTOPILOT_TAKEOFF_MODE:
		autopilot_takeoff_handler();
		break;
	case AUTOPILOT_WAIT_NEXT_WAYPOINT_MODE:
		autopilot_wait_next_waypoint_handler();
		break;
	case AUTOPILOT_FOLLOW_WAYPOINT_MODE:
		autopilot_follow_waypoint_handler();
		break;
	}
}

void debug_print_waypoint_list(void)
{
	char *prompt = "waypoint list:\n\r";
	uart1_puts(prompt, strlen(prompt));

	char s[200] = {0};
	int i;
	for(i = 0; i < autopilot.wp_num; i++) {
		sprintf(s, "wp #%d: x=%.1f, y=%.1f, z=%.1f, heading=%.1f,  stay_time=%.1f, radius=%.1f\n\r",
		        i, autopilot.wp_list[i].pos[0], autopilot.wp_list[i].pos[1],
		        autopilot.wp_list[i].pos[2], autopilot.wp_list[i].heading,
		        autopilot.wp_list[i].halt_time_sec, autopilot.wp_list[i].touch_radius);
		uart1_puts(s, strlen(s));
	}
}

void debug_print_waypoint_status(void)
{
	if(autopilot.mode != AUTOPILOT_WAIT_NEXT_WAYPOINT_MODE && autopilot.mode != AUTOPILOT_FOLLOW_WAYPOINT_MODE) {
		char *no_executing_s = "autopilot off, no executing waypoint mission.\n\r";
		uart1_puts(no_executing_s, strlen(no_executing_s));
		return;
	}

	char s[200] = {'\0'};
	int curr_wp_num = autopilot.curr_wp;
	sprintf(s, "current waypoint = #%d, x=%.1fm, y=%.1fm, z=%.1fm,"
	        " heading=%.1f, stay_time=%.1f, radius=%.1fm\n\r",
	        curr_wp_num,
	        autopilot.wp_list[curr_wp_num].pos[0] * 0.01,
	        autopilot.wp_list[curr_wp_num].pos[1] * 0.01,
	        autopilot.wp_list[curr_wp_num].pos[2] * 0.01,
	        autopilot.wp_list[curr_wp_num].heading,
	        autopilot.wp_list[curr_wp_num].halt_time_sec,
	        autopilot.wp_list[curr_wp_num].touch_radius * 0.01);
	uart1_puts(s, strlen(s));
	freertos_task_delay(1);
}
