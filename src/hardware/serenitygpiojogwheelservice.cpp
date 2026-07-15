#include "hardware/serenitygpiojogwheelservice.h"

#include <gpiod.h>

#include <QDebug>
#include <cstring>

namespace {

// GPIO chip and pin/CC assignments from SERENITY.md "Hardware Integration".
// These are BCM/header line offsets on the Raspberry Pi 5's main GPIO
// controller.
//
// The RP1 southbridge chip that exposes the 40-pin header on the Pi 5 does
// NOT reliably enumerate as /dev/gpiochip0 -- its chip number depends on
// probe order relative to other gpiochips in the system (e.g. ones
// registered by the PMIC or USB peripherals) and has been observed as
// gpiochip4 in practice. So instead of hardcoding a device number, the
// chip is found by its driver label ("pinctrl-rp1") at startup.
constexpr auto kRp1ChipLabel = "pinctrl-rp1";
constexpr auto kFallbackGpioChipPath = "/dev/gpiochip0";

constexpr unsigned int kDeckALineA = 17;
constexpr unsigned int kDeckALineB = 27;
constexpr int kDeckAMidiChannel = 0; // MIDI channel 1
constexpr int kDeckAMidiController = 1; // CC 1

constexpr unsigned int kDeckBLineA = 22;
constexpr unsigned int kDeckBLineB = 23;
constexpr int kDeckBMidiChannel = 0; // MIDI channel 1
constexpr int kDeckBMidiController = 2; // CC 2

// Scans all present GPIO chips for one whose driver label matches
// kRp1ChipLabel and returns its device path (e.g. "/dev/gpiochip4"). Falls
// back to kFallbackGpioChipPath if no chip with that label is found (e.g.
// when running on non-Pi5 hardware or a dev machine with no GPIO chips at
// all), so this never blocks startup even if the pinout doesn't apply.
QString findRp1ChipPath() {
    gpiod_chip_iter* pIter = gpiod_chip_iter_new();
    if (!pIter) {
        qWarning() << "SerenityGpioJogWheelService: gpiod_chip_iter_new failed, falling back to"
                    << kFallbackGpioChipPath;
        return QString::fromLatin1(kFallbackGpioChipPath);
    }

    gpiod_chip* pMatch = nullptr;
    gpiod_chip* pChip;
    gpiod_foreach_chip(pIter, pChip) {
        const char* pLabel = gpiod_chip_label(pChip);
        if (pLabel && std::strcmp(pLabel, kRp1ChipLabel) == 0) {
            pMatch = pChip;
            break;
        }
    }

    if (!pMatch) {
        gpiod_chip_iter_free(pIter);
        qWarning() << "SerenityGpioJogWheelService: no GPIO chip labeled" << kRp1ChipLabel
                    << "found, falling back to" << kFallbackGpioChipPath;
        return QString::fromLatin1(kFallbackGpioChipPath);
    }

    const QString path = QLatin1String("/dev/") + QString::fromLatin1(gpiod_chip_name(pMatch));
    // Keep pMatch open until we're done reading its name; free_noclose then
    // releases the iterator's bookkeeping without closing pMatch itself.
    gpiod_chip_iter_free_noclose(pIter);
    gpiod_chip_close(pMatch);
    return path;
}

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

    const QString chipPath = findRp1ChipPath();
    qInfo() << "SerenityGpioJogWheelService: using GPIO chip" << chipPath;

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
