/*
 ******************************************************************************
 * @addtogroup UAVOROSBridge UAVO to ROS Bridge Module
 * @{
 *
 * @file       readthread.cpp
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

#include "rosbridge.h"
#include "std_msgs/String.h"
#include "geometry_msgs/PoseStamped.h"
#include "nav_msgs/Odometry.h"
#include <sstream>
#include "boost/thread.hpp"
#include "readthread.h"
#include "uavorosbridgemessage_priv.h"
#include "pios.h"
#include "tf/transform_datatypes.h"

namespace librepilot {
class readthread_priv {
public:
    boost::asio::serial_port *port;
    boost::thread *thread;
    ros::NodeHandle *nodehandle;
    uint8_t rx_buffer[ROSBRIDGEMESSAGE_BUFFERSIZE];
    size_t rx_length;
    rosbridge *parent;
    ros::Publisher state_pub, state2_pub;
    uint32_t sequence;


/**
 * Process incoming bytes from an ROS query thing.
 * @param[in] b received byte
 * @return true if we should continue processing bytes
 */
    void ros_receive_byte(uint8_t b)
    {
        rx_buffer[rx_length] = b;
        rx_length++;
        rosbridgemessage_t *message = (rosbridgemessage_t *)rx_buffer;

        // very simple parser - but not a state machine, just a few checks
        if (rx_length <= offsetof(rosbridgemessage_t, length)) {
            // check (partial) magic number - partial is important since we need to restart at any time if garbage is received
            uint32_t canary = 0xff;
            for (uint32_t t = 1; t < rx_length; t++) {
                canary = (canary << 8) || 0xff;
            }
            if ((message->magic & canary) != (ROSBRIDGEMAGIC & canary)) {
                // parse error, not beginning of message
                rx_length = 0;
                {
                    std_msgs::String msg;
                    std::stringstream bla;
                    bla << "canary failure";
                    msg.data = bla.str();
                    parent->rosinfoPrint(msg.data.c_str());
                }
                return;
            }
        }
        if (rx_length == offsetof(rosbridgemessage_t, timestamp)) {
            if (message->length > (uint32_t)(ROSBRIDGEMESSAGE_BUFFERSIZE - offsetof(rosbridgemessage_t, data))) {
                // parse error, no messages are that long
                rx_length = 0;
                {
                    std_msgs::String msg;
                    std::stringstream bla;
                    bla << "zero length";
                    msg.data = bla.str();
                    parent->rosinfoPrint(msg.data.c_str());
                }
                return;
            }
        }
        if (rx_length == offsetof(rosbridgemessage_t, crc32)) {
            if (message->type >= ROSBRIDGEMESSAGE_END_ARRAY_SIZE) {
                // parse error
                rx_length = 0;
                {
                    std_msgs::String msg;
                    std::stringstream bla;
                    bla << "invalid type" << message->type;
                    msg.data = bla.str();
                    parent->rosinfoPrint(msg.data.c_str());
                }
                return;
            }
            if (message->length != ROSBRIDGEMESSAGE_SIZES[message->type]) {
                // parse error
                rx_length = 0;
                {
                    std_msgs::String msg;
                    std::stringstream bla;
                    bla << "invalid length";
                    msg.data = bla.str();
                    parent->rosinfoPrint(msg.data.c_str());
                }
                return;
            }
        }
        if (rx_length < offsetof(rosbridgemessage_t, data)) {
            // not a parse failure, just not there yet
            return;
        }
        if (rx_length == offsetof(rosbridgemessage_t, data) + ROSBRIDGEMESSAGE_SIZES[message->type]) {
            // complete message received and stored in pointer "message"
            // empty buffer for next message
            rx_length = 0;

            if (PIOS_CRC32_updateCRC(0xffffffff, message->data, message->length) != message->crc32) {
                std_msgs::String msg;
                std::stringstream bla;
                bla << "CRC mismatch";
                msg.data = bla.str();
                parent->rosinfoPrint(msg.data.c_str());
                // crc mismatch
                return;
            }
            switch (message->type) {
            case ROSBRIDGEMESSAGE_PING:
                pong_handler((rosbridgemessage_pingpong_t *)message->data);
                break;
            case ROSBRIDGEMESSAGE_FULLSTATE_ESTIMATE:
                fullstate_estimate_handler(message);
                break;
            default:
            {
                std_msgs::String msg;
                std::stringstream bla;
                bla << "received something";
                msg.data = bla.str();
                parent->rosinfoPrint(msg.data.c_str());
            }
                // do nothing at all and discard the message
            break;
            }
        }
    }

    void fullstate_estimate_handler(rosbridgemessage_t *message)
    {
        rosbridgemessage_fullstate_estimate_t *data = (rosbridgemessage_fullstate_estimate_t *)message->data;
        nav_msgs::Odometry odometry;
        geometry_msgs::PoseStamped pose;

        // ATTENTION:  LibrePilot - like most outdoor platforms uses North-East-Down coordinate frame for all data
        // in body frame
        // x points forward
        // y points right
        // z points down
        // this also goes for the quaternion

        // ROS uses the annozing east-north-up coordinate frame which means axis need to be swapped and signs inverted
        odometry.pose.pose.position.y = data->position[0];
        odometry.pose.pose.position.x = data->position[1];
        odometry.pose.pose.position.z = -data->position[2];

        tf::Quaternion q_0(data->quaternion[1], -data->quaternion[2], -data->quaternion[3], data->quaternion[0]);
        tf::Quaternion q_1, q_2;
        q_1.setEuler(0, 0, M_PI / 2.0);
        quaternionTFToMsg((q_1 * q_0).normalize(), odometry.pose.pose.orientation);
        // quaternionTFToMsg(q_0,odometry.pose.pose.orientation);

        odometry.twist.twist.linear.y  = data->velocity[0];
        odometry.twist.twist.linear.x  = data->velocity[1];
        odometry.twist.twist.linear.z  = -data->velocity[2];
        odometry.twist.twist.angular.y = data->velocity[0];
        odometry.twist.twist.angular.x = data->velocity[1];
        odometry.twist.twist.angular.z = -data->velocity[2];
        // FAKE covariance -- LibrePilot does have a covariance matrix, but its 13x13 and not trivially comparable
        // also ROS documentation on how the covariance is encoded into this double[36] (ro wvs col major, order of members, ...)
        // is very lacing
        for (int t = 0; t < 6; t++) {
            for (int t2 = 0; t < 6; t++) {
                if (t == t2) {
                    odometry.twist.covariance[t * 6 + t2] = 1.0;
                } else {
                    odometry.twist.covariance[t * 6 + t2] = 0.0;
                }
            }
        }
        odometry.header.seq = sequence++;
        odometry.header.stamp.sec  = (uint32_t)(message->timestamp / 1000000);
        odometry.header.stamp.nsec = (uint32_t)1000 * (message->timestamp % 1000000);
        odometry.header.frame_id   = "1";
        odometry.child_frame_id    = "2";
        pose.header = odometry.header;
        pose.pose   = odometry.pose.pose;

        state_pub.publish(odometry);
        state2_pub.publish(pose);
        parent->rosinfoPrint("state published");
    }

    void pong_handler(rosbridgemessage_pingpong_t *data)
    {
        uint8_t tx_buffer[ROSBRIDGEMESSAGE_BUFFERSIZE];
        rosbridgemessage_t *message = (rosbridgemessage_t *)tx_buffer;
        rosbridgemessage_pingpong_t *payload = (rosbridgemessage_pingpong_t *)message->data;

        *payload = *data;
        message->magic     = ROSBRIDGEMAGIC;
        message->type      = ROSBRIDGEMESSAGE_PONG;
        message->length    = ROSBRIDGEMESSAGE_SIZES[message->type];
        boost::posix_time::time_duration diff = boost::posix_time::microsec_clock::local_time() - *parent->getStart();
        message->timestamp = diff.total_microseconds();
        message->crc32     = PIOS_CRC32_updateCRC(0xffffffff, message->data, message->length);
        int res = parent->serialWrite(tx_buffer, message->length + offsetof(rosbridgemessage_t, data));
        std_msgs::String msg;
        std::stringstream bla;
        bla << "received ping : " << (unsigned int)payload->sequence_number;
        bla << " pong write res is  : " << (int)res;
        msg.data = bla.str();
        parent->rosinfoPrint(msg.data.c_str());
        // chatter_pub.publish(msg);
    }


    void run()
    {
        unsigned char c;

        rx_length  = 0;
        state_pub  = nodehandle->advertise<nav_msgs::Odometry>("Octocopter", 10);
        state2_pub = nodehandle->advertise<geometry_msgs::PoseStamped>("octoPose", 10);
        while (ros::ok()) {
            boost::asio::read(*port, boost::asio::buffer(&c, 1));
            ros_receive_byte(c);
            std_msgs::String msg;
            std::stringstream bla;
            bla << std::hex << (unsigned short)c;
            msg.data = bla.str();
            parent->rosinfoPrint(msg.data.c_str());
            // chatter_pub.publish(msg);
        }
    }
};

readthread::readthread(ros::NodeHandle *nodehandle, boost::asio::serial_port *port, rosbridge *parent)
{
    instance = new readthread_priv();
    instance->parent     = parent;
    instance->port       = port;
    instance->nodehandle = nodehandle;
    instance->sequence   = 0;
    instance->thread     = new boost::thread(boost::bind(&readthread_priv::run, instance));
}

readthread::~readthread()
{
    instance->thread->detach();
    delete instance->thread;
    delete instance;
}
}
