#include "hardware/serenitygpiojogwheelservice.h"

#include <QDebug>

namespace {

// GPIO chip and pin/CC assignments from SERENITY.md "Hardware Integration".
// These are BCM/header line offsets on the Raspberry Pi 5's main GPIO
// controller.
//
// The RP1 southbridge chip that exposes the 40-pin header on the Pi 5 does
// NOT reliably enumerate as /dev/gpiochip0 -- confirmed on this hardware to
// be /dev/gpiochip4 instead (its number depends on probe order relative to
// other gpiochips registered in the system, e.g. by the PMIC or USB
// peripherals).
constexpr auto kGpioChipPath = "/dev/gpiochip4";

constexpr unsigned int kDeckALineA = 17;
constexpr unsigned int kDeckALineB = 27;
constexpr int kDeckAMidiChannel = 0; // MIDI channel 1
constexpr int kDeckAMidiController = 1; // CC 1

constexpr unsigned int kDeckBLineA = 22;
constexpr unsigned int kDeckBLineB = 23;
constexpr int kDeckBMidiChannel = 0; // MIDI channel 1
constexpr int kDeckBMidiController = 2; // CC 2

} // namespace

SerenityGpioJogWheelService::SerenityGpioJogWheelService()
        : m_midiBridge(QStringLiteral("Serenity Jog Wheels")) {
}

SerenityGpioJogWheelService::~SerenityGpioJogWheelService() {
    stop();
}

bool SerenityGpioJogWheelService::start() {
    if (!m_midiBridge.open()) {
        qWarning() << "SerenityGpioJogWheelService: failed to open ALSA virtual MIDI port,"
                    << "GPIO jog wheels disabled";
        return false;
    }

    const QString chipPath = QString::fromLatin1(kGpioChipPath);

    m_pDeckAEncoder = std::make_unique<SerenityGpioEncoder>(
            chipPath,
            kDeckALineA,
            kDeckALineB,
            QStringLiteral("serenity-jogwheel-deckA"),
            [this](int delta) { sendJogTick(kDeckAMidiChannel, kDeckAMidiController, delta); });
    m_pDeckBEncoder = std::make_unique<SerenityGpioEncoder>(
            chipPath,
            kDeckBLineA,
            kDeckBLineB,
            QStringLiteral("serenity-jogwheel-deckB"),
            [this](int delta) { sendJogTick(kDeckBMidiChannel, kDeckBMidiController, delta); });

    m_pDeckAEncoder->start();
    m_pDeckBEncoder->start();
    return true;
}

void SerenityGpioJogWheelService::stop() {
    if (m_pDeckAEncoder) {
        m_pDeckAEncoder->requestStop();
    }
    if (m_pDeckBEncoder) {
        m_pDeckBEncoder->requestStop();
    }
    // ~SerenityGpioEncoder() waits for the thread and releases the GPIO
    // lines, so resetting is enough to fully stop each encoder.
    m_pDeckAEncoder.reset();
    m_pDeckBEncoder.reset();
    m_midiBridge.close();
}

void SerenityGpioJogWheelService::sendJogTick(int channel, int controller, int delta) {
    // "Diff" (7-bit two's complement) convention: 1 == +1 tick, 127 == -1 tick.
    const int value = delta > 0 ? 1 : 127;
    m_midiBridge.sendControlChange(channel, controller, value);
}
