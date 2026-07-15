#pragma once

#include <QThread>
#include <QString>
#include <functional>
#include <atomic>

struct gpiod_chip;
struct gpiod_line;

/// Reads one optical quadrature rotary encoder (two GPIO lines, A and B)
/// through the Linux GPIO character device via libgpiod v1's bulk
/// edge-events API (gpiod_line_request_bulk_both_edges_events_flags /
/// gpiod_line_event_wait_bulk) and reports each detent as a +1 (clockwise)
/// or -1 (counter-clockwise) tick.
///
/// libgpiod v1 is what Ubuntu 24.04 actually packages for the Raspberry Pi
/// 5 (as of this writing there is no v2 package); v2's newer request-object
/// API was tried first but isn't installable via apt on the real target,
/// so this targets v1's bulk API instead.
///
/// Runs its own wait loop on a dedicated thread since edge events must be
/// consumed as they arrive; the wait timeout is used only to notice
/// requestStop() promptly, not to pace reads.
class SerenityGpioEncoder : public QThread {
    Q_OBJECT
  public:
    /// onTick is invoked from this object's thread for every decoded
    /// detent; +1 for clockwise, -1 for counter-clockwise. The callback
    /// must be safe to call from a non-UI thread.
    SerenityGpioEncoder(QString chipPath,
            unsigned int lineOffsetA,
            unsigned int lineOffsetB,
            QString consumerName,
            std::function<void(int)> onTick);
    ~SerenityGpioEncoder() override;

    void requestStop();

  protected:
    void run() override;

  private:
    bool openAndRequestLines();
    void releaseLines();
    int readLineBit(gpiod_line* pLine) const;

    const QString m_chipPath;
    const unsigned int m_lineOffsetA;
    const unsigned int m_lineOffsetB;
    const QString m_consumerName;
    const std::function<void(int)> m_onTick;

    std::atomic<bool> m_stopRequested;
    gpiod_chip* m_pChip;
    gpiod_line* m_pLineA;
    gpiod_line* m_pLineB;

    // Last known combined 2-bit state (bit1 = A, bit0 = B) used to decode
    // the quadrature transition table.
    int m_lastState;
};
