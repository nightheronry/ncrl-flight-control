#include <math.h>
#include <stdio.h>
#include "arm_math.h"
#include "mpu6500.h"
#include "optitrack.h"
#include "ahrs.h"
#include "madgwick_ahrs.h"
#include "lpf.h"
#include "uart.h"
#include "matrix.h"
#include "delay.h"
#include "proj_config.h"
#include "se3_math.h"
#include "quaternion.h"

#define dt 0.0025 //0.0025s = 400Hz

extern optitrack_t optitrack;

MAT_ALLOC(q_last, 4, 1);
MAT_ALLOC(q_now, 4, 1);
MAT_ALLOC(dq, 4, 1);
MAT_ALLOC(w, 3, 1);
MAT_ALLOC(f, 4, 3);

madgwick_t madgwick_ahrs;

void ahrs_init(float *init_accel)
{
	//initialize matrices
	MAT_INIT(q_last, 4, 1);
	MAT_INIT(q_now, 4, 1);
	MAT_INIT(dq, 4, 1);
	MAT_INIT(w, 3, 1);
	MAT_INIT(f, 4, 3);

	_mat_(q_last)[0] = 1.0f;
	_mat_(q_last)[1] = 0.0f;
	_mat_(q_last)[2] = 0.0f;
	_mat_(q_last)[3] = 0.0f;

	/* initialize madgwick filter */
	madgwick_init(&madgwick_ahrs, 400, 0.3);
}

void convert_gravity_to_quat(float *a, float *q)
{
	float _sqrt;

	if(a[2] >= 0.0f) {
		//q0
		arm_sqrt_f32(0.5f * (a[2] + 1.0f), &_sqrt);
		q[0] = _sqrt;
		//q1
		arm_sqrt_f32(2.0f * (a[2] + 1.0f), &_sqrt);
		q[1] = +a[1] / _sqrt;
		//q2
		arm_sqrt_f32(2.0f * (a[2] + 1.0f), &_sqrt);
		q[2] = -a[0] / _sqrt;
		//q3
		q[3] = 0.0f;
	} else {
		//q0
		arm_sqrt_f32(2.0f * (1.0f - a[2]), &_sqrt);
		q[0] = -a[1] / _sqrt;
		//q1
		arm_sqrt_f32((1.0f - a[2]) * 0.5f, &_sqrt);
		q[1] = +_sqrt;
		//q2
		q[2] = 0.0f;
		//q3
		arm_sqrt_f32(2.0f * (1.0f - a[2]), &_sqrt);
		q[3] = +a[0] / _sqrt;
	}

	quat_normalize(q);
}

void align_ahrs_with_optitrack_yaw(float *q_ahrs)
{
	if(optitrack_available() == true) {
		euler_t optitrack_euler;
		quat_to_euler(optitrack.q, &optitrack_euler);

		float half_psi = optitrack_euler.yaw * 0.5f;

		float q_yaw[4];
		q_yaw[0] = arm_cos_f32(half_psi);
		q_yaw[1] = 0.0f;
		q_yaw[2] = 0.0f;
		q_yaw[3] = arm_sin_f32(half_psi);

		float q_original[4];
		q_original[0] = q_ahrs[0];
		q_original[1] = q_ahrs[1];
		q_original[2] = q_ahrs[2];
		q_original[3] = q_ahrs[3];
		quaternion_mult(q_yaw, q_original, q_ahrs);
	}
}

void ahrs_complementary_filter_estimate(float *accel, float *gyro)
{
	/* check the paper: Keeping a Good Attitude: A Quaternion-Based Orientation Filter for IMUs and MARGs
	 * by Roberto G. Valenti, Ivan Dryanovski and Jizhong Xiao */

	/* construct system transition function f */
	float half_q0_dt = 0.5f * _mat_(q_last)[0] * dt;
	float half_q1_dt = 0.5f * _mat_(q_last)[1] * dt;
	float half_q2_dt = 0.5f * _mat_(q_last)[2] * dt;
	float half_q3_dt = 0.5f * _mat_(q_last)[3] * dt;
	_mat_(f)[0] = -half_q1_dt;
	_mat_(f)[1] = -half_q2_dt;
	_mat_(f)[2] = -half_q3_dt;
	_mat_(f)[3] = +half_q0_dt;
	_mat_(f)[4] = -half_q3_dt;
	_mat_(f)[5] = +half_q2_dt;
	_mat_(f)[6] = +half_q3_dt;
	_mat_(f)[7] = +half_q0_dt;
	_mat_(f)[8] = -half_q1_dt;
	_mat_(f)[9] = -half_q2_dt;
	_mat_(f)[10] = +half_q1_dt;
	_mat_(f)[11] = +half_q0_dt;

	/* angular rate from rate gyro */
	_mat_(w)[0] = gyro[0];
	_mat_(w)[1] = gyro[1];
	_mat_(w)[2] = gyro[2];

	/* rate gyro integration */
	MAT_MULT(&f, &w, &dq); //calculate dq = f * w
	MAT_ADD(&q_last, &dq, &q_last);  //calculate x = x + dq
	quat_normalize(&_mat_(q_last)[0]); //renormalization

	/* convert gravity vector to quaternion */
	float q_gravity[4] = {0};
	normalize_3x1(accel); //normalize acceleromter
	convert_gravity_to_quat(accel, q_gravity);

	/* fuse gyroscope and acceleromter using LERP algorithm */
	float a = 0.995f;
	_mat_(q_now)[0] = (_mat_(q_last)[0] * a) + (q_gravity[0]* (1.0 - a));
	_mat_(q_now)[1] = (_mat_(q_last)[1] * a) + (q_gravity[1]* (1.0 - a));
	_mat_(q_now)[2] = (_mat_(q_last)[2] * a) + (q_gravity[2]* (1.0 - a));
	_mat_(q_now)[3] = (_mat_(q_last)[3] * a) + (q_gravity[3]* (1.0 - a));
	//it is crucial to renormalize the quaternion since LERP don't maintain
	//the unit length propertety of the quaternion
	quat_normalize(_mat_(q_now));

	/* update state variables for rate gyro */
	_mat_(q_last)[0] = _mat_(q_now)[0];
	_mat_(q_last)[1] = _mat_(q_now)[1];
	_mat_(q_last)[2] = _mat_(q_now)[2];
	_mat_(q_last)[3] = _mat_(q_now)[3];
}

void reset_quaternion_yaw_angle(float *q)
{
	float q_original[4];
	q_original[0] = q[0];
	q_original[1] = q[1];
	q_original[2] = q[2];
	q_original[3] = q[3];

	euler_t euler;
	quat_to_euler(q, &euler);
	float half_psi = euler.yaw / 2.0f;

	float q_negative_yaw[4];
	q_negative_yaw[0] = arm_cos_f32(-half_psi);
	q_negative_yaw[1] = 0.0f;
	q_negative_yaw[2] = 0.0f;
	q_negative_yaw[3] = arm_sin_f32(-half_psi);

	quaternion_mult(q_negative_yaw, q_original, q);
}

void ahrs_estimate(ahrs_t *ahrs, float *accel, float *gyro)
{
	/* note that acceleromter senses the negative gravity acceleration (normal force)
	 * a_imu = (R(phi, theta, psi) * a_translation) - (R(phi, theta, psi) * g) */
	float gravity[3];
	gravity[0] = -accel[0];
	gravity[1] = -accel[1];
	gravity[2] = -accel[2];

	float gyro_rad[3];
	gyro_rad[0] = deg_to_rad(gyro[0]);
	gyro_rad[1] = deg_to_rad(gyro[1]);
	gyro_rad[2] = deg_to_rad(gyro[2]);

#if (SELECT_AHRS == AHRS_COMPLEMENTARY_FILTER)
	ahrs_complementary_filter_estimate(gravity, gyro_rad);
#elif (SELECT_AHRS == AHRS_MADGWICK_FILTER)
	/* note that acceleromter senses the negative gravity acceleration (normal force) */
	madgwick_imu_ahrs(&madgwick_ahrs, gravity[0], gravity[1], gravity[2],
	                  gyro_rad[0], gyro_rad[1], gyro_rad[2]);
	_mat_(q_now)[0] = madgwick_ahrs.q[0];
	_mat_(q_now)[1] = madgwick_ahrs.q[1];
	_mat_(q_now)[2] = madgwick_ahrs.q[2];
	_mat_(q_now)[3] = madgwick_ahrs.q[3];
#endif

#if (SELECT_LOCALIZATION == LOCALIZATION_USE_OPTITRACK)
	reset_quaternion_yaw_angle(_mat_(q_now));
	align_ahrs_with_optitrack_yaw(_mat_(q_now));
#endif

	euler_t euler;
	quat_to_euler(&_mat_(q_now)[0], &euler);
	ahrs->attitude.roll = rad_to_deg(euler.roll);
	ahrs->attitude.pitch = rad_to_deg(euler.pitch);
	ahrs->attitude.yaw = rad_to_deg(euler.yaw);

	ahrs->q[0] = _mat_(q_now)[0];
	ahrs->q[1] = _mat_(q_now)[1];
	ahrs->q[2] = _mat_(q_now)[2];
	ahrs->q[3] = _mat_(q_now)[3];
}
