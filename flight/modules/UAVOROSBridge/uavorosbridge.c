/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup UAVOROSBridge UAVO to ROS Bridge Module
 * @{
 *
 * @file       uavorosridge.c
 * @author     The LibrePilot Project, http://www.librepilot.org Copyright (C) 2016.
 *             Max Planck Institute for intelligent systems, http://www.is.mpg.de Copyright (C) 2016.
 * @brief      Bridges certain UAVObjects to ROS on USB VCP
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
 *
 * Additional note on redistribution: The copyright and license notices above
 * must be maintained in each individual source file that is a derivative work
 * of this source file; otherwise redistribution is prohibited.
 */

#include "openpilot.h"
#include "hwsettings.h"
#include "taskinfo.h"
#include "callbackinfo.h"
#include "insgps.h"
/*
   #include "receiverstatus.h"
   #include "flightmodesettings.h"
   #include "flightbatterysettings.h"
   #include "flightbatterystate.h"
   #include "gpspositionsensor.h"
   #include "manualcontrolsettings.h"
   #include "oplinkstatus.h"
   #include "accessorydesired.h"
   #include "airspeedstate.h"
   #include "actuatorsettings.h"
   #include "systemstats.h"
   #include "systemalarms.h"
   #include "takeofflocation.h"
   #include "homelocation.h"
   #include "stabilizationdesired.h"
   #include "stabilizationsettings.h"
   #include "stabilizationbank.h"
   #include "stabilizationsettingsbank1.h"
   #include "stabilizationsettingsbank2.h"
   #include "stabilizationsettingsbank3.h"
   #include "magstate.h"
 */

#include "manualcontrolcommand.h"
#include "accessorydesired.h"
#include "actuatordesired.h"
#include "auxpositionsensor.h"
#include "pathdesired.h"
#include "poilocation.h"
#include "flightmodesettings.h"
#include "flightstatus.h"
#include "positionstate.h"
#include "velocitystate.h"
#include "attitudestate.h"
#include "gyrostate.h"
#include "gyrosensor.h"
#include "accelsensor.h"
#include "rosbridgesettings.h"
#include "rosbridgestatus.h"
#include "objectpersistence.h"

#include "pios_sensors.h"

#if defined(PIOS_INCLUDE_ROS_BRIDGE)

#include <uavorosbridgemessage_priv.h>

struct ros_bridge {
    uintptr_t     com;

    uint32_t      lastPingTimestamp;
    uint8_t       myPingSequence;
    uint8_t       remotePingSequence;
    PiOSDeltatimeConfig roundtrip;
    double        roundTripTime;
    int32_t       pingTimer;
    int32_t       stateTimer;
    int32_t       rateTimer;
    float         rateAccumulator[3];
    float         rawGyrAccumulator[3];
    float         rawAccAccumulator[3];
    int32_t       gyrTimer;
    int32_t       accTimer;
    uint8_t       rx_buffer[ROSBRIDGEMESSAGE_BUFFERSIZE];
    size_t        rx_length;
    volatile bool scheduled[ROSBRIDGEMESSAGE_END_ARRAY_SIZE];
};

#if defined(PIOS_ROS_STACK_SIZE)
#define STACK_SIZE_BYTES  PIOS_ROS_STACK_SIZE
#else
#define STACK_SIZE_BYTES  2048
#endif
#define TASK_PRIORITY     CALLBACK_TASK_AUXILIARY
#define CALLBACK_PRIORITY CALLBACK_PRIORITY_REGULAR
#define CBTASK_PRIORITY   CALLBACK_TASK_AUXILIARY

static bool module_enabled = false;
static struct ros_bridge *ros;
static int32_t uavoROSBridgeInitialize(void);
static void uavoROSBridgeRxTask(void *parameters);
static void uavoROSBridgeTxTask(void);
static DelayedCallbackInfo *callbackHandle;
static rosbridgemessage_handler ping_handler, ping_r_handler, pong_handler, pong_r_handler, fullstate_estimate_handler, imu_average_handler, gimbal_estimate_handler, flightcontrol_r_handler, pos_estimate_r_handler;
static ROSBridgeSettingsData settings;
void AttitudeCb(__attribute__((unused)) UAVObjEvent *ev);
void SettingsCb(__attribute__((unused)) UAVObjEvent *ev);
void RateCb(__attribute__((unused)) UAVObjEvent *ev);
void RawGyrCb(__attribute__((unused)) UAVObjEvent *ev);
void RawAccCb(__attribute__((unused)) UAVObjEvent *ev);

static rosbridgemessage_handler *const rosbridgemessagehandlers[ROSBRIDGEMESSAGE_END_ARRAY_SIZE] = {
    ping_handler,
    NULL,
    NULL,
    NULL,
    pong_handler,
    fullstate_estimate_handler,
    imu_average_handler,
    gimbal_estimate_handler
};


/**
 * Process incoming bytes from an ROS query thing.
 * @param[in] b received byte
 * @return true if we should continue processing bytes
 */
static void ros_receive_byte(struct ros_bridge *m, uint8_t b)
{
    m->rx_buffer[m->rx_length] = b;
    m->rx_length++;
    rosbridgemessage_t *message = (rosbridgemessage_t *)m->rx_buffer;

    // very simple parser - but not a state machine, just a few checks
    if (m->rx_length <= offsetof(rosbridgemessage_t, length)) {
        // check (partial) magic number - partial is important since we need to restart at any time if garbage is received
        uint32_t canary = 0xff;
        for (uint32_t t = 1; t < m->rx_length; t++) {
            canary = (canary << 8) | 0xff;
        }
        if ((message->magic & canary) != (ROSBRIDGEMAGIC & canary)) {
            // parse error, not beginning of message
            goto rxfailure;
        }
    }
    if (m->rx_length == offsetof(rosbridgemessage_t, timestamp)) {
        if (message->length > (uint32_t)(ROSBRIDGEMESSAGE_BUFFERSIZE - offsetof(rosbridgemessage_t, data))) {
            // parse error, no messages are that long
            goto rxfailure;
        }
    }
    if (m->rx_length == offsetof(rosbridgemessage_t, crc32)) {
        if (message->type >= ROSBRIDGEMESSAGE_END_ARRAY_SIZE) {
            // parse error
            goto rxfailure;
        }
        if (message->length != ROSBRIDGEMESSAGE_SIZES[message->type]) {
            // parse error
            goto rxfailure;
        }
    }
    if (m->rx_length < offsetof(rosbridgemessage_t, data)) {
        // not a parse failure, just not there yet
        return;
    }
    if (m->rx_length == offsetof(rosbridgemessage_t, data) + ROSBRIDGEMESSAGE_SIZES[message->type]) {
        // complete message received and stored in pointer "message"
        // empty buffer for next message
        m->rx_length = 0;

        if (PIOS_CRC32_updateCRC(0xffffffff, message->data, message->length) != message->crc32) {
            // crc mismatch
            goto rxfailure;
        }
        uint32_t rxpackets;
        ROSBridgeStatusRxPacketsGet(&rxpackets);
        rxpackets++;
        ROSBridgeStatusRxPacketsSet(&rxpackets);
        switch (message->type) {
        case ROSBRIDGEMESSAGE_PING:
            ping_r_handler(m, message);
            break;
        case ROSBRIDGEMESSAGE_POS_ESTIMATE:
            pos_estimate_r_handler(m, message);
            break;
        case ROSBRIDGEMESSAGE_FLIGHTCONTROL:
            flightcontrol_r_handler(m, message);
            break;
        case ROSBRIDGEMESSAGE_GIMBALCONTROL:
            // TODO implement gimbal control somehow
            break;
        case ROSBRIDGEMESSAGE_PONG:
            pong_r_handler(m, message);
            break;
        default:
            // do nothing at all and discard the message
            break;
        }
    }
    return;

rxfailure:
    m->rx_length = 0;
    uint32_t rxfail;
    ROSBridgeStatusRxFailGet(&rxfail);
    rxfail++;
    ROSBridgeStatusRxFailSet(&rxfail);
}

static uint32_t hwsettings_rosspeed_enum_to_baud(uint8_t baud)
{
    switch (baud) {
    case HWSETTINGS_ROSSPEED_2400:
        return 2400;

    case HWSETTINGS_ROSSPEED_4800:
        return 4800;

    case HWSETTINGS_ROSSPEED_9600:
        return 9600;

    case HWSETTINGS_ROSSPEED_19200:
        return 19200;

    case HWSETTINGS_ROSSPEED_38400:
        return 38400;

    case HWSETTINGS_ROSSPEED_57600:
        return 57600;

    default:
    case HWSETTINGS_ROSSPEED_115200:
        return 115200;
    }
}


/**
 * Module start routine automatically called after initialization routine
 * @return 0 when was successful
 */
static int32_t uavoROSBridgeStart(void)
{
    if (!module_enabled) {
        // give port to telemetry if it doesn't have one
        // stops board getting stuck in condition where it can't be connected to gcs
        if (!PIOS_COM_TELEM_RF) {
            PIOS_COM_TELEM_RF = PIOS_COM_ROS;
        }

        return -1;
    }

    PIOS_DELTATIME_Init(&ros->roundtrip, 1e-3f, 1e-6f, 10.0f, 1e-1f);
    ros->pingTimer  = 0;
    ros->stateTimer = 0;
    ros->rateTimer  = 0;
    ros->gyrTimer   = 0;
    ros->accTimer   = 0;
    ros->rateAccumulator[0]   = 0;
    ros->rateAccumulator[1]   = 0;
    ros->rateAccumulator[2]   = 0;
    ros->rawGyrAccumulator[0] = 0;
    ros->rawGyrAccumulator[1] = 0;
    ros->rawGyrAccumulator[2] = 0;
    ros->rawAccAccumulator[0] = 0;
    ros->rawAccAccumulator[1] = 0;
    ros->rawAccAccumulator[2] = 0;
    ros->rx_length = 0;
    ros->myPingSequence = 0x66;

    xTaskHandle taskHandle;

    xTaskCreate(uavoROSBridgeRxTask, "uavoROSBridge", STACK_SIZE_BYTES / 4, NULL, TASK_PRIORITY, &taskHandle);
    PIOS_TASK_MONITOR_RegisterTask(TASKINFO_RUNNING_UAVOROSBRIDGE, taskHandle);
    PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);

    return 0;
}

/**
 * Module initialization routine
 * @return 0 when initialization was successful
 */
static int32_t uavoROSBridgeInitialize(void)
{
    if (PIOS_COM_ROS) {
        ros = pios_malloc(sizeof(*ros));
        if (ros != NULL) {
            memset(ros, 0x00, sizeof(*ros));

            ros->com = PIOS_COM_ROS;

            ROSBridgeSettingsInitialize();
            ROSBridgeSettingsConnectCallback(&SettingsCb);
            SettingsCb(NULL);
            ROSBridgeStatusInitialize();
            AUXPositionSensorInitialize();
            HwSettingsInitialize();
            HwSettingsROSSpeedOptions rosSpeed;
            HwSettingsROSSpeedGet(&rosSpeed);

            PIOS_COM_ChangeBaud(PIOS_COM_ROS, hwsettings_rosspeed_enum_to_baud(rosSpeed));
            callbackHandle = PIOS_CALLBACKSCHEDULER_Create(&uavoROSBridgeTxTask, CALLBACK_PRIORITY, CBTASK_PRIORITY, CALLBACKINFO_RUNNING_UAVOROSBRIDGE, STACK_SIZE_BYTES);
            VelocityStateInitialize();
            PositionStateInitialize();
            AttitudeStateInitialize();
            AttitudeStateConnectCallback(&AttitudeCb);
            GyroStateInitialize();
            GyroStateConnectCallback(&RateCb);
            GyroSensorInitialize();
            GyroSensorConnectCallback(&RawGyrCb);
            AccelSensorInitialize();
            AccelSensorConnectCallback(&RawAccCb);
            FlightStatusInitialize();
            PathDesiredInitialize();
            PoiLocationInitialize();
            ActuatorDesiredInitialize();
            FlightModeSettingsInitialize();
            ManualControlCommandInitialize();
            AccessoryDesiredInitialize();

            module_enabled = true;
            return 0;
        }
    }

    return -1;
}
MODULE_INITCALL(uavoROSBridgeInitialize, uavoROSBridgeStart);

/** various handlers **/
static void ping_r_handler(struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_pingpong_t *data = (rosbridgemessage_pingpong_t *)&(m->data);

    rb->remotePingSequence = data->sequence_number;
    rb->scheduled[ROSBRIDGEMESSAGE_PONG] = true;
    PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);
}

static void ping_handler(struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_pingpong_t *data = (rosbridgemessage_pingpong_t *)&(m->data);

    data->sequence_number = ++rb->myPingSequence;
    rb->roundtrip.last    = PIOS_DELAY_GetRaw();
}

static void pong_r_handler(struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_pingpong_t *data = (rosbridgemessage_pingpong_t *)&(m->data);

    if (data->sequence_number != rb->myPingSequence) {
        return;
    }
    float roundtrip = PIOS_DELTATIME_GetAverageSeconds(&(rb->roundtrip));
    ROSBridgeStatusPingRoundTripTimeSet(&roundtrip);
}

static void flightcontrol_r_handler(__attribute__((unused)) struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_flightcontrol_t *data = (rosbridgemessage_flightcontrol_t *)&(m->data);
    FlightStatusFlightModeOptions mode;

    FlightStatusFlightModeGet(&mode);
    if (mode != FLIGHTSTATUS_FLIGHTMODE_ROSCONTROLLED) {
        return;
    }
    PathDesiredData pathDesired;
    PathDesiredGet(&pathDesired);
    switch (data->mode) {
    case ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_WAYPOINT:
    {
        FlightModeSettingsPositionHoldOffsetData offset;
        FlightModeSettingsPositionHoldOffsetGet(&offset);
        pathDesired.End.North        = boundf(data->control[0], settings.GeoFenceBoxMin.North, settings.GeoFenceBoxMax.North);
        pathDesired.End.East         = boundf(data->control[1], settings.GeoFenceBoxMin.East, settings.GeoFenceBoxMax.East);
        pathDesired.End.Down         = boundf(data->control[2], settings.GeoFenceBoxMin.Down, settings.GeoFenceBoxMax.Down);
        pathDesired.Start.North      = pathDesired.End.North + offset.Horizontal;
        pathDesired.Start.East       = pathDesired.End.East;
        pathDesired.Start.Down       = pathDesired.End.Down;
        pathDesired.StartingVelocity = 0.0f;
        pathDesired.EndingVelocity   = 0.0f;
        pathDesired.Mode = PATHDESIRED_MODE_GOTOENDPOINT;
    }
    break;
    case ROSBRIDGEMESSAGE_FLIGHTCONTROL_MODE_ATTITUDE:
    {
        pathDesired.ModeParameters[0] = data->control[0];
        pathDesired.ModeParameters[1] = data->control[1];
        pathDesired.ModeParameters[2] = data->control[2];
        pathDesired.ModeParameters[3] = data->control[3];
        pathDesired.Mode = PATHDESIRED_MODE_FIXEDATTITUDE;
    }
    break;
    }
    PathDesiredSet(&pathDesired);
    PoiLocationData poiLocation;
    PoiLocationGet(&poiLocation);
    poiLocation.North = data->poi[0];
    poiLocation.East  = data->poi[1];
    poiLocation.Down  = data->poi[2];
    PoiLocationSet(&poiLocation);
}
static void pos_estimate_r_handler(__attribute__((unused)) struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_pos_estimate_t *data = (rosbridgemessage_pos_estimate_t *)&(m->data);
    AUXPositionSensorData pos;

    pos.North = data->position[0];
    pos.East  = data->position[1];
    pos.Down  = data->position[2];
    AUXPositionSensorSet(&pos);
}

static void pong_handler(struct ros_bridge *rb, rosbridgemessage_t *m)
{
    rosbridgemessage_pingpong_t *data = (rosbridgemessage_pingpong_t *)&(m->data);


    data->sequence_number = rb->remotePingSequence;
}

static void fullstate_estimate_handler(__attribute__((unused)) struct ros_bridge *rb, rosbridgemessage_t *m)
{
    PositionStateData pos;
    VelocityStateData vel;
    AttitudeStateData att;
    FlightStatusFlightModeOptions mode;
    FlightStatusArmedOptions armed;
    float thrust;
    AccessoryDesiredData accessory;
    ManualControlCommandData manualcontrol;

    ManualControlCommandGet(&manualcontrol);
    FlightStatusArmedGet(&armed);
    FlightStatusFlightModeGet(&mode);
    ActuatorDesiredThrustGet(&thrust);
    PositionStateGet(&pos);
    VelocityStateGet(&vel);
    AttitudeStateGet(&att);
    rosbridgemessage_fullstate_estimate_t *data = (rosbridgemessage_fullstate_estimate_t *)&(m->data);
    data->quaternion[0] = att.q1;
    data->quaternion[1] = att.q2;
    data->quaternion[2] = att.q3;
    data->quaternion[3] = att.q4;
    data->position[0]   = pos.North;
    data->position[1]   = pos.East;
    data->position[2]   = pos.Down;
    data->velocity[0]   = vel.North;
    data->velocity[1]   = vel.East;
    data->velocity[2]   = vel.Down;
    data->rotation[0]   = ros->rateAccumulator[0];
    data->rotation[1]   = ros->rateAccumulator[1];
    data->rotation[2]   = ros->rateAccumulator[2];
    data->thrust = thrust;
    data->ROSControlled = (mode == FLIGHTSTATUS_FLIGHTMODE_ROSCONTROLLED) ? 1 : 0;
    data->FlightMode    = manualcontrol.FlightModeSwitchPosition;
    data->armed = (armed == FLIGHTSTATUS_ARMED_ARMED) ? 1 : 0;
    data->controls[0]   = manualcontrol.Roll;
    data->controls[1]   = manualcontrol.Pitch;
    data->controls[2]   = manualcontrol.Yaw;
    data->controls[3]   = manualcontrol.Thrust;
    data->controls[4]   = manualcontrol.Collective;
    data->controls[5]   = manualcontrol.Throttle;
    for (int t = 0; t < 4; t++) {
        if (AccessoryDesiredInstGet(t, &accessory) == 0) {
            data->accessory[t] = accessory.AccessoryVal;
        }
    }

    int x, y;
    float *P[13];
    INSGetPAddress(P);
    for (x = 0; x < 10; x++) {
        for (y = 0; y < 10; y++) {
            data->matrix[x * 10 + y] = P[x][y];
        }
    }
    if (ros->rateTimer >= 1) {
        float factor = 1.0f / (float)ros->rateTimer;
        data->rotation[0] *= factor;
        data->rotation[1] *= factor;
        data->rotation[2] *= factor;
        ros->rateAccumulator[0] = 0;
        ros->rateAccumulator[1] = 0;
        ros->rateAccumulator[2] = 0;
        ros->rateTimer = 0;
    }
}
static void imu_average_handler(__attribute__((unused)) struct ros_bridge *rb, __attribute__((unused)) rosbridgemessage_t *m)
{
    rosbridgemessage_imu_average_t *data = (rosbridgemessage_imu_average_t *)&(m->data);

    data->gyro_average[0]  = ros->rawGyrAccumulator[0];
    data->gyro_average[1]  = ros->rawGyrAccumulator[1];
    data->gyro_average[2]  = ros->rawGyrAccumulator[2];
    data->accel_average[0] = ros->rawAccAccumulator[0];
    data->accel_average[1] = ros->rawAccAccumulator[1];
    data->accel_average[2] = ros->rawAccAccumulator[2];
    data->gyrsamples = ros->gyrTimer;
    data->accsamples = ros->accTimer;
    if (ros->gyrTimer >= 1) {
        float factor = 1.0f / (float)ros->gyrTimer;
        data->gyro_average[0]    *= factor;
        data->gyro_average[1]    *= factor;
        data->gyro_average[2]    *= factor;
        ros->rawGyrAccumulator[0] = 0;
        ros->rawGyrAccumulator[1] = 0;
        ros->rawGyrAccumulator[2] = 0;
        ros->gyrTimer = 0;
    }
    if (ros->accTimer >= 1) {
        float factor = 1.0f / (float)ros->accTimer;
        data->accel_average[0]   *= factor;
        data->accel_average[1]   *= factor;
        data->accel_average[2]   *= factor;
        ros->rawAccAccumulator[0] = 0;
        ros->rawAccAccumulator[1] = 0;
        ros->rawAccAccumulator[2] = 0;
        ros->accTimer = 0;
    }
}
static void gimbal_estimate_handler(__attribute__((unused)) struct ros_bridge *rb, __attribute__((unused)) rosbridgemessage_t *m)
{
    // TODO
}

/**
 * Main task routine
 * @param[in] parameters parameter given by PIOS_Callback_Create()
 */
static void uavoROSBridgeTxTask(void)
{
    uint8_t buffer[ROSBRIDGEMESSAGE_BUFFERSIZE]; // buffer on the stack? could also be in static RAM but not reuseale by other callbacks then
    rosbridgemessage_t *message = (rosbridgemessage_t *)buffer;

    for (rosbridgemessagetype_t type = ROSBRIDGEMESSAGE_PING; type < ROSBRIDGEMESSAGE_END_ARRAY_SIZE; type++) {
        if (ros->scheduled[type] && rosbridgemessagehandlers[type] != NULL) {
            message->magic       = ROSBRIDGEMAGIC;
            message->length      = ROSBRIDGEMESSAGE_SIZES[type];
            message->type        = type;
            message->timestamp   = PIOS_DELAY_GetuS();
            (*rosbridgemessagehandlers[type])(ros, message);
            message->crc32       = PIOS_CRC32_updateCRC(0xffffffff, message->data, message->length);
            int32_t ret = PIOS_COM_SendBufferNonBlocking(ros->com, buffer, offsetof(rosbridgemessage_t, data) + message->length);
            // int32_t ret = PIOS_COM_SendBuffer(ros->com, buffer, offsetof(rosbridgemessage_t, data) + message->length);
            ros->scheduled[type] = false;
            if (ret >= 0) {
                uint32_t txpackets;
                ROSBridgeStatusTxPacketsGet(&txpackets);
                txpackets++;
                ROSBridgeStatusTxPacketsSet(&txpackets);
            } else {
                uint32_t txfail;
                ROSBridgeStatusTxFailGet(&txfail);
                txfail++;
                ROSBridgeStatusTxFailSet(&txfail);
            }
            PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);
            return;
        }
    }
    // nothing scheduled, do a ping in 10 secods time
}

/**
 * Event Callback on Gyro updates (called with PIOS_SENSOR_RATE - roughly 500 Hz - from State Estimation)
 */
void RateCb(__attribute__((unused)) UAVObjEvent *ev)
{
    GyroStateData gyr;

    GyroStateGet(&gyr);
    ros->rateAccumulator[0] += gyr.x;
    ros->rateAccumulator[1] += gyr.y;
    ros->rateAccumulator[2] += gyr.z;
    ros->rateTimer++;
}

/**
 * Event Callback on Gyro updates (called with PIOS_SENSOR_RATE - roughly 500 Hz - from State Estimation)
 */
void RawGyrCb(__attribute__((unused)) UAVObjEvent *ev)
{
    GyroSensorData gyr;

    GyroSensorGet(&gyr);
    ros->rawGyrAccumulator[0] += gyr.x;
    ros->rawGyrAccumulator[1] += gyr.y;
    ros->rawGyrAccumulator[2] += gyr.z;
    ros->gyrTimer++;
}

/**
 * Event Callback on Accel updates (called with PIOS_SENSOR_RATE - roughly 500 Hz - from State Estimation)
 */
void RawAccCb(__attribute__((unused)) UAVObjEvent *ev)
{
    AccelSensorData acc;

    AccelSensorGet(&acc);
    ros->rawAccAccumulator[0] += acc.x;
    ros->rawAccAccumulator[1] += acc.y;
    ros->rawAccAccumulator[2] += acc.z;
    ros->accTimer++;
    ros->scheduled[ROSBRIDGEMESSAGE_IMU_AVERAGE] = true;
    if (callbackHandle) {
        PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);
    }
}

/**
 * Event Callback on Attitude updates (called with PIOS_SENSOR_RATE - roughly 500 Hz - from State Estimation)
 */
void AttitudeCb(__attribute__((unused)) UAVObjEvent *ev)
{
    bool dispatch = false;

    if (++ros->pingTimer >= settings.UpdateRate.Ping && settings.UpdateRate.Ping > 0) {
        ros->pingTimer = 0;
        dispatch = true;
        ros->scheduled[ROSBRIDGEMESSAGE_PING] = true;
    }
    if (++ros->stateTimer >= settings.UpdateRate.State && settings.UpdateRate.State > 0) {
        ros->stateTimer = 0;
        dispatch = true;
        ros->scheduled[ROSBRIDGEMESSAGE_FULLSTATE_ESTIMATE] = true;
    }
    if (dispatch && callbackHandle) {
        PIOS_CALLBACKSCHEDULER_Dispatch(callbackHandle);
    }
}

/**
 * Event Callback on Settings
 */
void SettingsCb(__attribute__((unused)) UAVObjEvent *ev)
{
    ROSBridgeSettingsGet(&settings);
}

/**
 * Main task routine
 * @param[in] parameters parameter given by PIOS_Thread_Create()
 */
static void uavoROSBridgeRxTask(__attribute__((unused)) void *parameters)
{
    while (1) {
        uint8_t b = 0;
        uint16_t count = PIOS_COM_ReceiveBuffer(ros->com, &b, 1, ~0);
        if (count) {
            ros_receive_byte(ros, b);
        }
    }
}

#endif // PIOS_INCLUDE_ROS_BRIDGE
/**
 * @}
 * @}
 */
