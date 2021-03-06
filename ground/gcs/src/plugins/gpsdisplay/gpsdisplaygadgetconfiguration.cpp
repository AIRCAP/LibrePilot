/**
 ******************************************************************************
 *
 * @file       gpsdisplaygadgetconfiguration.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup GPSGadgetPlugin GPS Gadget Plugin
 * @{
 * @brief A gadget that displays GPS status and enables basic configuration
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

#include "gpsdisplaygadgetconfiguration.h"
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

/**
 * Loads a saved configuration or defaults if non exist.
 *
 */
GpsDisplayGadgetConfiguration::GpsDisplayGadgetConfiguration(QString classId, QSettings &settings, QObject *parent) :
    IUAVGadgetConfiguration(classId, parent)
{
    m_defaultPort     = settings.value("defaultPort", "Unknown").toString();
    m_defaultSpeed    = (QSerialPort::BaudRate)settings.value("defaultSpeed", QSerialPort::UnknownBaud).toInt();
    m_defaultDataBits = (QSerialPort::DataBits)settings.value("defaultDataBits", QSerialPort::UnknownDataBits).toInt();
    m_defaultFlow     = (QSerialPort::FlowControl)settings.value("defaultFlow", QSerialPort::UnknownFlowControl).toInt();
    m_defaultParity   = (QSerialPort::Parity)settings.value("defaultParity", QSerialPort::UnknownParity).toInt();
    m_defaultStopBits = (QSerialPort::StopBits)settings.value("defaultStopBits", QSerialPort::UnknownStopBits).toInt();
    m_connectionMode  = settings.value("connectionMode", "Serial").toString();
    m_defaultTimeOut  = 5000;
}

GpsDisplayGadgetConfiguration::GpsDisplayGadgetConfiguration(const GpsDisplayGadgetConfiguration &obj) :
    IUAVGadgetConfiguration(obj.classId(), obj.parent())
{
    m_defaultSpeed    = obj.m_defaultSpeed;
    m_defaultDataBits = obj.m_defaultDataBits;
    m_defaultFlow     = obj.m_defaultFlow;
    m_defaultParity   = obj.m_defaultParity;
    m_defaultStopBits = obj.m_defaultStopBits;
    m_defaultPort     = obj.m_defaultPort;
    m_connectionMode  = obj.m_connectionMode;
    m_defaultTimeOut  = obj.m_defaultTimeOut;
}

/**
 * Clones a configuration.
 *
 */
IUAVGadgetConfiguration *GpsDisplayGadgetConfiguration::clone() const
{
    return new GpsDisplayGadgetConfiguration(*this);
}

/**
 * Saves a configuration.
 *
 */
void GpsDisplayGadgetConfiguration::saveConfig(QSettings &settings) const
{
    settings.setValue("defaultSpeed", m_defaultSpeed);
    settings.setValue("defaultDataBits", m_defaultDataBits);
    settings.setValue("defaultFlow", m_defaultFlow);
    settings.setValue("defaultParity", m_defaultParity);
    settings.setValue("defaultStopBits", m_defaultStopBits);
    settings.setValue("defaultPort", m_defaultPort);
    settings.setValue("connectionMode", m_connectionMode);
}
