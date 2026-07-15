#pragma once

#include <memory>

#include "hardware/serenitygpioencoder.h"
#include "hardware/serenitymidibridge.h"

/// Wires the two Deck A/B GPIO jog wheel encoders to MIDI CC messages on a
/// shared ALSA virtual MIDI port, per the pinout and mapping documented in
/// SERENITY.md ("Planned Hardware Integration").
///
/// Each encoder detent is sent immediately as a single-tick MIDI CC using
/// the "Diff" (7-bit two's complement) convention already used by several
/// stock Mixxx controller mappings for jog wheels: value 1 means +1,
/// value 127 means -1. The corresponding Mixxx mapping preset
/// (res/controllers/Serenity Jog Wheels.midi.xml) applies the <diff/>
/// option to the `jog` control for each deck.
class SerenityGpioJogWheelService {
  public:
    SerenityGpioJogWheelService();
    ~SerenityGpioJogWheelService();

    SerenityGpioJogWheelService(const SerenityGpioJogWheelService&) = delete;
    SerenityGpioJogWheelService& operator=(const SerenityGpioJogWheelService&) = delete;

    /// Opens the ALSA virtual MIDI port and starts reading both encoders.
    /// Must be called before ControllerManager::setUpDevices() so the
    /// virtual port exists when Mixxx enumerates MIDI devices.
    /// Returns false (and leaves the service fully stopped) if the GPIO
    /// chip or ALSA sequencer could not be opened.
    bool start();

    /// Stops the encoder threads and closes the ALSA port. Safe to call
    /// even if start() was never called or already failed.
    void stop();

  private:
    void sendJogTick(int channel, int controller, int delta);

    SerenityMidiBridge m_midiBridge;
    std::unique_ptr<SerenityGpioEncoder> m_pDeckAEncoder;
    std::unique_ptr<SerenityGpioEncoder> m_pDeckBEncoder;
};
