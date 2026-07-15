#include "hardware/serenitygpioencoder.h"

#include <gpiod.h>
#include <poll.h>

#include <QDebug>
#include <cstdint>
#include <utility>

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

constexpr int kPollTimeoutMillis = 200;
constexpr size_t kEdgeEventBufferCapacity = 16;

int lineValueBit(gpiod_line_request* pRequest, unsigned int offset) {
    return gpiod_line_request_get_value(pRequest, offset) == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
}

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
          m_pRequest(nullptr),
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

bool SerenityGpioEncoder::openAndRequestLines() {
    m_pChip = gpiod_chip_open(qPrintable(m_chipPath));
    if (!m_pChip) {
        qWarning() << "SerenityGpioEncoder: failed to open" << m_chipPath;
        return false;
    }

    gpiod_line_settings* pSettings = gpiod_line_settings_new();
    if (!pSettings) {
        qWarning() << "SerenityGpioEncoder: gpiod_line_settings_new failed";
        return false;
    }
    gpiod_line_settings_set_direction(pSettings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(pSettings, GPIOD_LINE_EDGE_BOTH);
    // Most bare optical/mechanical encoder modules idle high and pull a
    // line low on contact, so an internal pull-up keeps them from floating.
    gpiod_line_settings_set_bias(pSettings, GPIOD_LINE_BIAS_PULL_UP);

    gpiod_line_config* pLineConfig = gpiod_line_config_new();
    if (!pLineConfig) {
        qWarning() << "SerenityGpioEncoder: gpiod_line_config_new failed";
        gpiod_line_settings_free(pSettings);
        return false;
    }
    const unsigned int offsets[2] = {m_lineOffsetA, m_lineOffsetB};
    if (gpiod_line_config_add_line_settings(pLineConfig, offsets, 2, pSettings) != 0) {
        qWarning() << "SerenityGpioEncoder: gpiod_line_config_add_line_settings failed";
        gpiod_line_settings_free(pSettings);
        gpiod_line_config_free(pLineConfig);
        return false;
    }

    gpiod_request_config* pRequestConfig = gpiod_request_config_new();
    if (pRequestConfig) {
        gpiod_request_config_set_consumer(pRequestConfig, qPrintable(m_consumerName));
    }

    m_pRequest = gpiod_chip_request_lines(m_pChip, pRequestConfig, pLineConfig);

    if (pRequestConfig) {
        gpiod_request_config_free(pRequestConfig);
    }
    gpiod_line_config_free(pLineConfig);
    gpiod_line_settings_free(pSettings);

    if (!m_pRequest) {
        qWarning() << "SerenityGpioEncoder: gpiod_chip_request_lines failed for"
                    << m_chipPath << "offsets" << m_lineOffsetA << m_lineOffsetB;
        return false;
    }

    const int a = lineValueBit(m_pRequest, m_lineOffsetA);
    const int b = lineValueBit(m_pRequest, m_lineOffsetB);
    m_lastState = (a << 1) | b;
    return true;
}

void SerenityGpioEncoder::releaseLines() {
    if (m_pRequest) {
        gpiod_line_request_release(m_pRequest);
        m_pRequest = nullptr;
    }
    if (m_pChip) {
        gpiod_chip_close(m_pChip);
        m_pChip = nullptr;
    }
}

void SerenityGpioEncoder::processEdgeEvents(void* pEventBufferVoid, int numEvents) {
    auto* pEventBuffer = static_cast<gpiod_edge_event_buffer*>(pEventBufferVoid);
    int state = m_lastState;
    for (int i = 0; i < numEvents; ++i) {
        gpiod_edge_event* pEvent = gpiod_edge_event_buffer_get_event(pEventBuffer, i);
        if (!pEvent) {
            continue;
        }
        const unsigned int offset = gpiod_edge_event_get_line_offset(pEvent);
        const bool rising = gpiod_edge_event_get_event_type(pEvent) == GPIOD_EDGE_EVENT_RISING_EDGE;

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

void SerenityGpioEncoder::run() {
    if (!openAndRequestLines()) {
        return;
    }

    gpiod_edge_event_buffer* pEventBuffer = gpiod_edge_event_buffer_new(kEdgeEventBufferCapacity);
    if (!pEventBuffer) {
        qWarning() << "SerenityGpioEncoder: gpiod_edge_event_buffer_new failed";
        releaseLines();
        return;
    }

    const int fd = gpiod_line_request_get_fd(m_pRequest);
    struct pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!m_stopRequested.load()) {
        const int pollResult = ::poll(&pfd, 1, kPollTimeoutMillis);
        if (pollResult < 0) {
            qWarning() << "SerenityGpioEncoder: poll() failed on" << m_chipPath;
            break;
        }
        if (pollResult == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }
        const int numEvents = gpiod_line_request_read_edge_events(
                m_pRequest, pEventBuffer, kEdgeEventBufferCapacity);
        if (numEvents < 0) {
            qWarning() << "SerenityGpioEncoder: gpiod_line_request_read_edge_events failed on"
                        << m_chipPath;
            break;
        }
        processEdgeEvents(pEventBuffer, numEvents);
    }

    gpiod_edge_event_buffer_free(pEventBuffer);
    releaseLines();
}
