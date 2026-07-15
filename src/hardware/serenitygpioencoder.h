#pragma once

#include <QThread>
#include <QString>
#include <functional>
#include <atomic>

struct gpiod_chip;
struct gpiod_line_request;

/// Reads one optical quadrature rotary encoder (two GPIO lines, A and B)
/// through the Linux GPIO character device via libgpiod v2 and reports each
/// detent as a +1 (clockwise) or -1 (counter-clockwise) tick.
///
/// Runs its own poll loop on a dedicated thread since edge events must be
/// consumed as they arrive; the poll timeout is used only to notice
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
    void processEdgeEvents(void* pEventBuffer, int numEvents);

    const QString m_chipPath;
    const unsigned int m_lineOffsetA;
    const unsigned int m_lineOffsetB;
    const QString m_consumerName;
    const std::function<void(int)> m_onTick;

    std::atomic<bool> m_stopRequested;
    gpiod_chip* m_pChip;
    gpiod_line_request* m_pRequest;

    // Last known combined 2-bit state (bit1 = A, bit0 = B) used to decode
    // the quadrature transition table.
    int m_lastState;
};
