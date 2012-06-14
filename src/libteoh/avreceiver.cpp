#include "avreceiver.h"

#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QStateMachine>
#include <QState>
#include <QHistoryState>
#include <QFinalState>
#include <QTimer>

class AVReceiver::Private
{
    AVReceiver* q;
public:
    Private( AVReceiver* _q ) : q(_q) {}

    void socketError();
    void dataReceived();

    QTcpSocket* connectionSocket;
    QUdpSocket* streamingSocket;

    QAudioOutput* audioOutput;
    QIODevice* audioBuffer;

    QTimer* connectionTimer;

    QStateMachine* stateMachine;
};

AVReceiver::AVReceiver(QObject *parent) :
    QObject(parent),
    d( new Private(this) )
{
    d->connectionSocket = new QTcpSocket(this);
    d->streamingSocket = new QUdpSocket(this);
    d->streamingSocket->bind(2012);
#if QT_VERSION >=0x040800
    d->streamingSocket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    d->streamingSocket->joinMulticastGroup( QHostAddress("239.51.67.81") );
#endif

    connect( d->streamingSocket, SIGNAL(readyRead()), SLOT(dataReceived()) );
    connect( d->streamingSocket, SIGNAL(error(QAbstractSocket::SocketError)), SLOT(socketError()) );

    QAudioFormat audioFormat;
    audioFormat.setFrequency(8000);
    audioFormat.setChannels(1);
    audioFormat.setSampleSize(8);
    audioFormat.setCodec("audio/pcm");
    audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    audioFormat.setSampleType(QAudioFormat::UnSignedInt);

    QAudioDeviceInfo outputDevice = QAudioDeviceInfo::defaultOutputDevice();
    if (!outputDevice.isFormatSupported(audioFormat)) {
        audioFormat = outputDevice.nearestFormat(audioFormat);
        qWarning() << "recording format not supported, using nearest supported";
    }
    //qDebug() << "Selected recording format:" << audioFormat;

    d->audioOutput = new QAudioOutput(outputDevice, audioFormat, this);
    d->audioBuffer = d->audioOutput->start();

    d->connectionTimer = new QTimer(this);

    d->stateMachine = new QStateMachine(this);

    QState* connectingState = new QState;
    connectingState->setProperty("stateId", Connecting);
    QState* connectedState = new QState;
    connectedState->setProperty("stateId", Connected);
    QState* standbyState = new QState(connectedState);
    standbyState->setProperty("stateId", Standby);
    QState* listeningState = new QState(connectedState);
    listeningState->setProperty("stateId", Listening);
    QState* notificationState = new QState(connectedState);
    notificationState->setProperty("stateId", Notification);
    QState* alarmState = new QState(connectedState);
    alarmState->setProperty("stateId", Alarm);
    QHistoryState* reconnectingState = new QHistoryState(connectedState);
    reconnectingState->setProperty("stateId", Reconnecting);
    QFinalState* disconnectedState = new QFinalState;
    disconnectedState->setProperty("stateId", Disconnected);

    connectedState->setInitialState(standbyState);
    reconnectingState->setDefaultState(standbyState);

    d->stateMachine->addState(connectingState);
    d->stateMachine->addState(connectedState);
    d->stateMachine->addState(disconnectedState);
    d->stateMachine->setInitialState(connectingState);
    d->stateMachine->start();

    connectingState->addTransition(d->connectionSocket, SIGNAL(connected()), connectedState);
    connectingState->addTransition(d->connectionTimer, SIGNAL(timeout()), disconnectedState);
    connectedState->addTransition(d->connectionTimer, SIGNAL(timeout()), disconnectedState);

    Q_FOREACH( QAbstractState* s, d->stateMachine->findChildren<QAbstractState*>() ) {
        connect(s, SIGNAL(entered()), this, SIGNAL(stateChanged()));
    }
}

AVReceiver::~AVReceiver()
{
}

AVReceiver::State AVReceiver::state() const
{
    QAbstractState* foundState = 0;
    Q_FOREACH( QAbstractState* s, d->stateMachine->configuration() ) {
        foundState = s;
        // ensure we use the child state if we have nested states
        if ( s->parentState() )
            break;
    }

    return foundState ? static_cast<State>(foundState->property("stateId").toInt()) : Disconnected;
}

void AVReceiver::Private::dataReceived()
{
    QByteArray data;
    while( streamingSocket->hasPendingDatagrams() ) {
        QByteArray datagram;
        datagram.resize(streamingSocket->pendingDatagramSize());
        streamingSocket->readDatagram(datagram.data(), datagram.size());
        if (data.isEmpty())
            data = datagram;
        else
            data.append(datagram);
    }
    qDebug() << "Received" << data.count() << "bytes (socket status:" << streamingSocket->errorString() << ")";

    audioBuffer->write(data);
    audioOutput->resume();
}

void AVReceiver::Private::socketError()
{
    qWarning() << "socket error:" << streamingSocket->error() << streamingSocket->errorString();
    // TODO better error handling
}

#include "moc_avreceiver.cpp"
