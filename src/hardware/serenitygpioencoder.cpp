#include "hardware/serenitygpioencoder.h"

#include <gpiod.h>

#include <QDebug>
#include <cstdint>
#include <ctime>
#include <utility>

#include "moc_serenitygpioencoder.cpp"

namespace {

// Standard 2-bit gray-code quadrature decode table, indexed by
// (previousState << 2) | currentState, where each state packs the A and B
// line levels as (A << 1) | B. Invalid/bounce transitions decode to 0.
constexpr int8_t kQuadratureTable[16] = {
        0,
        -1,
        1,
        0,
        1,
        0,
        0,
        -1,
        -1,
        0,
        0,
        1,
        0,
        1,
        -1,
        0,
};

constexpr int kWaitTimeoutMillis = 200;

} // namespace

SerenityGpioEncoder::SerenityGpioEncoder(QString chipPath,
        unsigned int lineOffsetA,
        unsigned int lineOffsetB,
        QString consumerName,
        std::function<void(int)> onTick)
        : m_chipPath(std::move(chipPath)),
          m_lineOffsetA(lineOffsetA),
          m_lineOffsetB(lineOffsetB),
          m_consumerName(std::move(consumerName)),
          m_onTick(std::move(onTick)),
          m_stopRequested(false),
          m_pChip(nullptr),
          m_pLineA(nullptr),
          m_pLineB(nullptr),
          m_lastState(0) {
}

SerenityGpioEncoder::~SerenityGpioEncoder() {
    requestStop();
    wait();
    releaseLines();
}

void SerenityGpioEncoder::requestStop() {
    m_stopRequested.store(true);
}

int SerenityGpioEncoder::readLineBit(gpiod_line* pLine) const {
    return gpiod_line_get_value(pLine) > 0 ? 1 : 0;
}

bool SerenityGpioEncoder::openAndRequestLines() {
    m_pChip = gpiod_chip_open(qPrintable(m_chipPath));
    if (!m_pChip) {
        qWarning() << "SerenityGpioEncoder: failed to open" << m_chipPath;
        return false;
    }

    m_pLineA = gpiod_chip_get_line(m_pChip, m_lineOffsetA);
    m_pLineB = gpiod_chip_get_line(m_pChip, m_lineOffsetB);
    if (!m_pLineA || !m_pLineB) {
        qWarning() << "SerenityGpioEncoder: failed to get lines" << m_lineOffsetA
                    << m_lineOffsetB << "on" << m_chipPath;
        return false;
    }

    gpiod_line_bulk requestBulk;
    gpiod_line_bulk_init(&requestBulk);
    gpiod_line_bulk_add(&requestBulk, m_pLineA);
    gpiod_line_bulk_add(&requestBulk, m_pLineB);

    // Most bare optical/mechanical encoder modules idle high and pull a
    // line low on contact, so an internal pull-up keeps them from floating.
    if (gpiod_line_request_bulk_both_edges_events_flags(&requestBulk,
                qPrintable(m_consumerName),
                GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) != 0) {
        qWarning() << "SerenityGpioEncoder: failed to request edge events on" << m_chipPath
                    << "offsets" << m_lineOffsetA << m_lineOffsetB;
        return false;
    }

    const int a = readLineBit(m_pLineA);
    const int b = readLineBit(m_pLineB);
    m_lastState = (a << 1) | b;
    return true;
}

void SerenityGpioEncoder::releaseLines() {
    // Releasing either line of a bulk request releases the whole request;
    // gpiod_line_release() on each is still the documented, safe way to
    // tear both down individually regardless of request grouping.
    if (m_pLineA) {
        gpiod_line_release(m_pLineA);
        m_pLineA = nullptr;
    }
    if (m_pLineB) {
        gpiod_line_release(m_pLineB);
        m_pLineB = nullptr;
    }
    if (m_pChip) {
        gpiod_chip_close(m_pChip);
        m_pChip = nullptr;
    }
}

void SerenityGpioEncoder::run() {
    if (!openAndRequestLines()) {
        releaseLines();
        return;
    }

    gpiod_line_bulk watchBulk;
    gpiod_line_bulk_init(&watchBulk);
    gpiod_line_bulk_add(&watchBulk, m_pLineA);
    gpiod_line_bulk_add(&watchBulk, m_pLineB);

    while (!m_stopRequested.load()) {
        struct timespec timeout = {};
        timeout.tv_sec = kWaitTimeoutMillis / 1000;
        timeout.tv_nsec = static_cast<long>(kWaitTimeoutMillis % 1000) * 1000000L;

        gpiod_line_bulk eventBulk;
        gpiod_line_bulk_init(&eventBulk);

        const int waitResult = gpiod_line_event_wait_bulk(&watchBulk, &timeout, &eventBulk);
        if (waitResult < 0) {
            qWarning() << "SerenityGpioEncoder: gpiod_line_event_wait_bulk failed on"
                        << m_chipPath;
            break;
        }
        if (waitResult == 0) {
            continue; // timed out with no events; loop back and re-check m_stopRequested
        }

        int state = m_lastState;
        for (unsigned int i = 0; i < eventBulk.num_lines; ++i) {
            gpiod_line* pLine = eventBulk.lines[i];
            struct gpiod_line_event event;
            if (gpiod_line_event_read(pLine, &event) != 0) {
                continue;
            }
            const unsigned int offset = gpiod_line_offset(pLine);
            const bool rising = event.event_type == GPIOD_LINE_EVENT_RISING_EDGE;

            if (offset == m_lineOffsetA) {
                state = rising ? (state | 0b10) : (state & 0b01);
            } else if (offset == m_lineOffsetB) {
                state = rising ? (state | 0b01) : (state & 0b10);
            } else {
                continue;
            }

            const int delta = kQuadratureTable[(m_lastState << 2) | state];
            if (delta != 0 && m_onTick) {
                m_onTick(delta);
            }
            m_lastState = state;
        }
    }

    releaseLines();
}
