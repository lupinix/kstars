/*  Ekos Lin Guider Handler
    Copyright (C) 2016 Jasem Mutlaq <mutlaqja@ikarustech.com>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
*/

#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QUrl>
#include <QVariantMap>
#include <QDebug>
#include <QFile>

#include <KMessageBox>
#include <KLocalizedString>

#include "linguider.h"
#include "Options.h"


namespace Ekos
{

LinGuider::LinGuider()
{
    tcpSocket = new QTcpSocket(this);

    connect(tcpSocket, SIGNAL(readyRead()), this, SLOT(readLinGuider()));
    connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(displayError(QAbstractSocket::SocketError)));

    connect(tcpSocket, SIGNAL(connected()), this, SLOT(onConnected()));

    state = IDLE;
    connection = DISCONNECTED;
}

LinGuider::~LinGuider()
{

}

bool LinGuider::Connect()
{
    if (connection == DISCONNECTED)
    {
        connection = CONNECTING;
        tcpSocket->connectToHost(Options::linGuiderHost(),  Options::linGuiderPort());
    }
    // Already connected, let's connect equipment
    else
        emit newStatus(GUIDE_CONNECTED);

    return true;
}

bool LinGuider::Disconnect()
{   
    connection = DISCONNECTED;
    tcpSocket->disconnectFromHost();

    emit newStatus(GUIDE_DISCONNECTED);

    return true;
}

void LinGuider::displayError(QAbstractSocket::SocketError socketError)
{
    switch (socketError)
    {
    case QAbstractSocket::RemoteHostClosedError:
        break;
    case QAbstractSocket::HostNotFoundError:
        emit newLog(i18n("The host was not found. Please check the host name and port settings in Guide options."));
        emit newStatus(GUIDE_DISCONNECTED);
        break;
    case QAbstractSocket::ConnectionRefusedError:
        emit newLog(i18n("The connection was refused by the peer. Make sure the LinGuider is running, and check that the host name and port settings are correct."));
        emit newStatus(GUIDE_DISCONNECTED);
        break;
    default:
        emit newLog(i18n("The following error occurred: %1.", tcpSocket->errorString()));
    }

    connection = DISCONNECTED;

}

void LinGuider::readLinGuider()
{
    QTextStream stream(tcpSocket);

    while (stream.atEnd() == false)
    {
        QString rawString = stream.readLine();

        if (rawString.isEmpty())
            continue;

        if (Options::verboseLogging())
            emit newLog(rawString);

        qint16 magicNumber = *(reinterpret_cast<qint16*>(rawString.toLatin1().data()));
        if (magicNumber != 0x02)
        {
            emit newLog(i18n("Invalid response."));
            continue;
        }

        qint16 command = *(reinterpret_cast<qint16*>(rawString.toLatin1().data()+2));
        if (command < GET_VER || command > GET_RA_DEC_DRIFT)
        {
            emit newLog(i18n("Invalid response."));
            continue;
        }

        QString reply = rawString.mid(8);

        processResponse(static_cast<LinGuiderCommand>(command), reply);
    }
}

void LinGuider::processResponse(LinGuiderCommand command, const QString &reply)
{
    switch (command)
    {
    case GET_VER:
        emit newLog(i18n("Connected to LinGuider %1", reply));
        if (reply < "v.4.1.0")
        {
            emit newLog(i18n("Only LinGuider v4.1.0 or higher is supported. Please upgrade LinGuider and try again."));
            Disconnect();
        }
        break;

    case FIND_STAR:
    {
        emit newLog(i18n("Auto star selected %1", reply));
        QStringList pos = reply.split(' ');
        if (pos.count() == 2)
        {
            starCenter = reply;
            sendCommand(SET_GUIDER_RETICLE_POS, reply);
        }
        else
        {
            emit newLog(i18n("Failed to process star position."));
            emit newStatus(GUIDE_CALIBRATION_ERROR);
        }
    }
        break;

    case SET_GUIDER_RETICLE_POS:
        if (reply == "OK")
        {
            sendCommand(SET_GUIDER_OVLS_POS, starCenter);
        }
        else
        {
            emit newLog(i18n("Failed to set guider reticle position."));
            emit newStatus(GUIDE_CALIBRATION_ERROR);
        }
        break;
    default:
        break;
    }
}

void LinGuider::onConnected()
{
    emit newStatus(GUIDE_CONNECTED);
    // Get version

    sendCommand(GET_VER);
}

void LinGuider::sendCommand(LinGuiderCommand command, const QString &args)
{    
    //emit newLog(json_doc.toJson(QJsonDocument::Compact));

    //tcpSocket->write(json_doc.toJson(QJsonDocument::Compact));

    // Command format: Magic Number (0x00 0x02), cmd (2 bytes), len_of_param (4 bytes), param (ascii)

    int size = 8 + args.size();

    QByteArray cmd(size, 0);

    // Magic number
    cmd[0] = 0x02;
    cmd[1] = 0x00;

    // Command
    cmd[2] = command;
    cmd[3] = 0x00;

    // Len
    qint32 len = args.size();
    memcpy(cmd.data()+4, &len, 4);

    // Params
    if (args.isEmpty() == false)
        memcpy(cmd.data()+8, args.toLatin1().data(), args.size());

    tcpSocket->write(cmd);
}

bool LinGuider::calibrate()
{
    // Let's start calibraiton. It is already calibrated but in this step we auto-select and star and set the square
    emit newStatus(Ekos::GUIDE_CALIBRATING);

    sendCommand(FIND_STAR);

    return true;
}

bool LinGuider::guide()
{

    //

    return true;
}

bool LinGuider::abort()
{

    return true;
}

bool LinGuider::suspend()
{

    return true;

}

bool LinGuider::resume()
{

    return true;

}

bool LinGuider::dither(double pixels)
{


    return true;
}

}