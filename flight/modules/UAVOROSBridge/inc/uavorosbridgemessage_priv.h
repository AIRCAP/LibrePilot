/**
 ******************************************************************************
 * @addtogroup LibrePilotModules LibrePilot Modules
 * @{
 * @addtogroup UAVOROSBridgeModule ROS Bridge Module
 * @{
 *
 * @file       uavorosbridgemessage_priv.h
 * @author     The LibrePilot Project, http://www.librepilot.org Copyright (C) 2016.
 *             Max Planck Institute for intelligent systems, http://www.is.mpg.de Copyright (C) 2016.
 * @brief      Message definition for UAVO ROS Bridge
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef UAVOROSBRIDGEMESSAGE_H
#define UAVOROSBRIDGEMESSAGE_H

#define ROSBRIDGEMAGIC 0x76543210

typedef enum {
	ROSBRIDGEMESSAGE_PING,
	ROSBRIDGEMESSAGE_POSVEL_ESTIMATE,
	ROSBRIDGEMESSAGE_FLIGHTCONTROL,
	ROSBRIDGEMESSAGE_GIMBALCONTROL,
	ROSBRIDGEMESSAGE_PONG,
	ROSBRIDGEMESSAGE_FULLSTATE_ESTIMATE,
	ROSBRIDGEMESSAGE_IMU_AVERAGE
	ROSBRIDGEMESSAGE_GIMBAL_ESTIMATE,
	ROSBRIDGEMESSAGE_END_ARRAY_SIZE,
} rosbridgemessagetype_t;

struct rosbridgemessage_s {
	uint32_t magic;
	uint32_t length;
	uint32_t timestamp;
	rosbridgemessagetype_t type
	uint32_t crc32;
	uint8_t data[];
};

typedef struct rosbridgemessage_s rosbridgemessage_t;

typedef struct {
	uint8_t sequence_number;
} rosbridgemessage_pingpong_t;

typedef struct {
	double position[3];
	double posvar[3];
	double velocity[3];
	double velvar[3];
} rosbridgemessage_posvel_estimate_t;

typedef enum {
	ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_MANUAL,
	ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_RATE,
	ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_ATTITUDE,
	ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_VELOCITY,
	ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_WAYPOINT,
} rosbridgemessage_flightcontrol_mode_t;

typedef struct {
	double control[4];
	rosbridgemessage_flightcontrol_mode_t mode;
} rosbridgemessage_flightcontrol_t;

typedef struct {
	double control[3];
} rosbridgemessage_gimbalcontrol_t;

typedef struct {
	double quaternion[4];
	double qvar[4];
	double position[3];
	double posvar[3];
	double velocity[3];
	double velvar[3];
} rosbridgemessage_fullstate_estimate_t;

typedef struct {
	uint32_t since;
	uint32_t numsamples;
	double gyro_average[3];
	double accel_average[3];
} rosbridgemessage_imu_average_t;

typedef struct {
	double quaternion[4];
	double qvar[4];
} rosbridgemessage_gimbal_estimate_t;

static const int32_t ROSBRIDGEMESSAGE_UPDATE_RATES[ROSBRIDGEMESSAGE_END_ARRAY_SIZE]= {
	-1, // ping
	-1, // posvel estimate
	-1, // flightcontrol
	-1, // gimbal_control -- all of the above are incoming messages from ROS
	-1, // pong -- not periodic but triggered
	20, // fullstate_estimate
	20, // imu_average
	-1, // gimbal_estimate -- not yet implemented
};

static const int32_t ROSBRIDGEMESSAGE_SIZES[ROSBRIDGEMESSAGE_END_ARRAY_SIZE]= {
	sizeof(rosbridgemessage_pingpong_t),
	sizeof(rosbridgemessage_posvel_estimate_t),
	sizeof(rosbridgemessage_flightcontrol_t),
	sizeof(rosbridgemessage_gimbalcontrol_t),
	sizeof(rosbridgemessage_fullstate_estimate_t),
	sizeof(rosbridgemessage_imu_average_t),
	sizeof(rosbridgemessage_gimbal_estimate_t),
};

#define ROSBRIDGEMESSAGE_BUFFERSIZE (offsetof(rosbridgemessage_t,data) + sizeof(rosbridgemessage_fullstate_estimate_t) + 2)

#endif // UAVOROSBRIDGEMESSAGE_H

/**
 * @}
 * @}
 */
