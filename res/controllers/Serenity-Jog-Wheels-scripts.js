var SerenityJogWheels = {};

// Scales the raw per-tick delta before it's applied. 1.0 = raw encoder
// resolution (128 PPR), which felt too sensitive on the physical hardware.
SerenityJogWheels.sensitivity = 0.6;

// [ChannelN],jog is a plain accumulator ControlObject (not a
// ControlTTRotary), so it has no MIDI behavior attached and can't be driven
// via the declarative <diff/> XML option -- see src/engine/controls/
// ratecontrol.cpp. It must be driven from a script using engine.setValue().
// RateControl::process() reads and resets it to 0 each engine callback, so
// ticks are added on top of whatever hasn't been consumed yet rather than
// overwriting it.
//
// value follows the "Diff" (7-bit two's complement) convention sent by
// SerenityGpioJogWheelService: 1..63 = positive ticks, 65..127 = negative
// ticks.
SerenityJogWheels.deckJog = function(channel, control, value, status, group) {
    var delta = value < 64 ? value : value - 128;
    engine.setValue(group,
            "jog",
            engine.getValue(group, "jog") + delta * SerenityJogWheels.sensitivity);
};
