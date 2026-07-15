#include "hardware/serenitymidibridge.h"

#include <alsa/asoundlib.h>

#include <QDebug>
#include <utility>

SerenityMidiBridge::SerenityMidiBridge(QString clientName, QString portName)
        : m_clientName(std::move(clientName)),
          m_portName(std::move(portName)),
          m_pSeq(nullptr),
          m_portId(-1) {
}

SerenityMidiBridge::~SerenityMidiBridge() {
    close();
}

bool SerenityMidiBridge::open() {
    QMutexLocker locker(&m_seqMutex);
    if (m_pSeq) {
        return true;
    }

    if (snd_seq_open(&m_pSeq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        qWarning() << "SerenityMidiBridge: snd_seq_open failed";
        m_pSeq = nullptr;
        return false;
    }

    snd_seq_set_client_name(m_pSeq, qPrintable(m_clientName));

    m_portId = snd_seq_create_simple_port(m_pSeq,
            qPrintable(m_portName),
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (m_portId < 0) {
        qWarning() << "SerenityMidiBridge: snd_seq_create_simple_port failed";
        snd_seq_close(m_pSeq);
        m_pSeq = nullptr;
        return false;
    }

    return true;
}

void SerenityMidiBridge::close() {
    QMutexLocker locker(&m_seqMutex);
    if (m_pSeq) {
        snd_seq_close(m_pSeq);
        m_pSeq = nullptr;
        m_portId = -1;
    }
}

void SerenityMidiBridge::sendControlChange(int channel, int controller, int value) {
    QMutexLocker locker(&m_seqMutex);
    if (!m_pSeq) {
        return;
    }

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, m_portId);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_controller(&ev, channel, controller, value);
    snd_seq_event_output_direct(m_pSeq, &ev);
}
