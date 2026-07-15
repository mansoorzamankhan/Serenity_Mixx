#pragma once

#include <QMutex>
#include <QString>

// Matches ALSA's own forward declaration in <alsa/seq.h> exactly
// (snd_seq_t is a typedef, not a struct tag, so "struct snd_seq_t;" here
// would declare a different, conflicting type).
typedef struct _snd_seq snd_seq_t;

/// Owns a single ALSA sequencer client with one virtual MIDI output port.
/// Mixxx's own MIDI controller subsystem (via PortMidi) discovers this port
/// like any other external MIDI device, so no special-cased input path is
/// needed on the Mixxx side -- just a controller mapping preset.
///
/// sendControlChange() is safe to call concurrently from multiple encoder
/// threads sharing the same bridge; access to the underlying ALSA sequencer
/// handle is serialized internally since snd_seq_t is not thread-safe.
class SerenityMidiBridge {
  public:
    explicit SerenityMidiBridge(QString clientName = QStringLiteral("Serenity Jog Wheels"),
            QString portName = QStringLiteral("Jog Wheels MIDI Out"));
    ~SerenityMidiBridge();

    /// Opens the ALSA sequencer client and creates the virtual port.
    /// Returns false if ALSA sequencer initialization failed.
    bool open();
    void close();
    bool isOpen() const {
        return m_pSeq != nullptr;
    }

    /// Sends a Control Change message. channel is 0-15, controller and
    /// value are 0-127.
    void sendControlChange(int channel, int controller, int value);

  private:
    const QString m_clientName;
    const QString m_portName;
    QMutex m_seqMutex;
    snd_seq_t* m_pSeq;
    int m_portId;
};
