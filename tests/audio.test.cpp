/// \file
/// \brief Tests for M08: PCM clips, the software mixer, the lock-free command
/// queue and the engine/device bridge. Everything is headless; no real audio
/// device is opened. See docs/build/m08-audio.md for the problems found while
/// writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <fa/audio/AudioClip.hpp>
#include <fa/audio/AudioEngine.hpp>
#include <fa/audio/AudioMixer.hpp>
#include <fa/audio/AudioTypes.hpp>
#include <fa/audio/SpscQueue.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/platform/AudioDevice.hpp>
#include <fa/platform/PlatformError.hpp>

namespace {

using Catch::Approx;

using fa::audio::AudioBus;
using fa::audio::AudioClip;
using fa::audio::AudioEngine;
using fa::audio::AudioMixer;
using fa::audio::kInvalidSoundHandle;
using fa::audio::kMaxVoices;
using fa::audio::makeSilence;
using fa::audio::makeTone;
using fa::audio::SoundDesc;
using fa::audio::SoundHandle;
using fa::audio::SpscQueue;

/// \brief Mono or stereo clip whose every sample is \p value.
std::shared_ptr<const AudioClip>
constantClip(std::int16_t value, int frames, int sampleRate = 48000, int channels = 1) {
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = sampleRate;
    clip->channels = channels;
    clip->samples.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels), value);
    return clip;
}

std::vector<std::int16_t> mixFrames(AudioMixer& mixer, int frames) {
    std::vector<std::int16_t> output(static_cast<std::size_t>(frames) * static_cast<std::size_t>(mixer.channels()));
    mixer.mix(std::span<std::int16_t>(output.data(), output.size()));
    return output;
}

double sampleAt(const std::vector<std::int16_t>& output, int frame, int channel, int channels) {
    return static_cast<double>(output[static_cast<std::size_t>(frame * channels + channel)]);
}

/// \brief In-memory IAudioDevice. `pump()` invokes the callback the way SDL would.
class FakeAudioDevice final : public fa::platform::IAudioDevice {
public:
    bool fail = false;
    bool open = false;
    int rate = 48000;
    int channelCount = 2;
    fa::platform::AudioCallback callback = nullptr;
    void* user = nullptr;

    fa::core::Status initialize(int sampleRate, int channels, fa::platform::AudioCallback cb, void* data) override {
        if (fail) {
            return fa::core::errorStatus(fa::platform::platform_error::kAudioInitFailed, "fake device failure");
        }
        rate = sampleRate;
        channelCount = channels;
        callback = cb;
        user = data;
        open = true;
        return fa::core::okStatus();
    }

    void shutdown() override {
        open = false;
        callback = nullptr;
        user = nullptr;
    }

    [[nodiscard]] bool isOpen() const noexcept override { return open; }
    [[nodiscard]] int sampleRate() const noexcept override { return open ? rate : 0; }
    [[nodiscard]] int channels() const noexcept override { return open ? channelCount : 0; }

    std::vector<std::int16_t> pump(int frames) {
        std::vector<std::int16_t> output(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channelCount), 0);
        if (callback != nullptr) {
            callback(user, output.data(), frames);
        }
        return output;
    }
};

}  // namespace

TEST_CASE("AudioClip validates PCM layout and generates clips", "[audio]") {
    AudioClip empty;
    CHECK_FALSE(empty.isValid());
    CHECK(empty.frameCount() == 0);

    AudioClip mono{{100, 200, 300}, 48000, 1};
    CHECK(mono.isValid());
    CHECK(mono.frameCount() == 3);
    CHECK(mono.durationSeconds() == Approx(3.0 / 48000.0));

    AudioClip odd{{1, 2, 3}, 48000, 2};
    CHECK_FALSE(odd.isValid());

    AudioClip badRate{{1, 2}, 0, 1};
    CHECK_FALSE(badRate.isValid());

    const AudioClip tone = makeTone(440.0f, 0.25f);
    CHECK(tone.isValid());
    CHECK(tone.channels == 2);
    CHECK(tone.frameCount() == 12000);
    CHECK(tone.samples.size() == 24000);
    CHECK(tone.durationSeconds() == Approx(0.25).margin(1e-6));

    const AudioClip silence = makeSilence(0.1f);
    CHECK(silence.isValid());
    CHECK(silence.frameCount() == 4800);
    CHECK(silence.samples.size() == 9600);
    for (const std::int16_t value : silence.samples) {
        CHECK(value == 0);
    }
}

TEST_CASE("AudioMixer outputs silence with no voices", "[audio]") {
    AudioMixer mixer;
    const std::vector<std::int16_t> output = mixFrames(mixer, 16);
    for (const std::int16_t value : output) {
        CHECK(value == 0);
    }
    CHECK(mixer.stats().activeVoices == 0);
    CHECK(mixer.stats().mixedFrames == 16);
}

TEST_CASE("AudioMixer mixes a mono voice into both channels", "[audio]") {
    AudioMixer mixer;
    const SoundHandle handle = mixer.add(constantClip(1000, 64), SoundDesc{});
    REQUIRE(handle != kInvalidSoundHandle);

    const std::vector<std::int16_t> output = mixFrames(mixer, 4);
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(sampleAt(output, frame, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, frame, 1, 2) == Approx(1000.0).margin(2.0));
    }
    CHECK(mixer.activeVoices() == 1);
    CHECK(mixer.stats().played == 1);
}

TEST_CASE("AudioMixer applies voice, bus and master gain", "[audio]") {
    AudioMixer mixer;
    SoundDesc desc;
    desc.volume = 0.5f;
    REQUIRE(mixer.add(constantClip(1000, 64), desc) != kInvalidSoundHandle);

    std::vector<std::int16_t> output = mixFrames(mixer, 1);
    CHECK(sampleAt(output, 0, 0, 2) == Approx(500.0).margin(2.0));

    mixer.setBusVolume(AudioBus::SFX, 0.5f);
    output = mixFrames(mixer, 1);
    CHECK(sampleAt(output, 0, 0, 2) == Approx(250.0).margin(2.0));

    mixer.setBusVolume(AudioBus::Master, 0.0f);
    output = mixFrames(mixer, 1);
    CHECK(sampleAt(output, 0, 0, 2) == Approx(0.0).margin(1.0));
}

TEST_CASE("AudioMixer pans hard left and hard right", "[audio]") {
    {
        AudioMixer mixer;
        SoundDesc desc;
        desc.pan = -1.0f;
        REQUIRE(mixer.add(constantClip(1000, 64), desc) != kInvalidSoundHandle);
        const std::vector<std::int16_t> output = mixFrames(mixer, 1);
        CHECK(sampleAt(output, 0, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, 0, 1, 2) == Approx(0.0).margin(1.0));
    }
    {
        AudioMixer mixer;
        SoundDesc desc;
        desc.pan = 1.0f;
        REQUIRE(mixer.add(constantClip(1000, 64), desc) != kInvalidSoundHandle);
        const std::vector<std::int16_t> output = mixFrames(mixer, 1);
        CHECK(sampleAt(output, 0, 0, 2) == Approx(0.0).margin(1.0));
        CHECK(sampleAt(output, 0, 1, 2) == Approx(1000.0).margin(2.0));
    }
}

TEST_CASE("AudioMixer loops and retires one-shot voices", "[audio]") {
    {
        AudioMixer mixer;
        auto clip = std::make_shared<AudioClip>();
        clip->samples = {1000, 2000};
        clip->channels = 1;
        clip->sampleRate = 48000;
        SoundDesc desc;
        desc.loop = true;
        REQUIRE(mixer.add(clip, desc) != kInvalidSoundHandle);

        const std::vector<std::int16_t> output = mixFrames(mixer, 4);
        CHECK(sampleAt(output, 0, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, 1, 0, 2) == Approx(2000.0).margin(2.0));
        CHECK(sampleAt(output, 2, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, 3, 0, 2) == Approx(2000.0).margin(2.0));
        CHECK(mixer.activeVoices() == 1);
    }
    {
        AudioMixer mixer;
        auto clip = std::make_shared<AudioClip>();
        clip->samples = {1000, 2000};
        clip->channels = 1;
        clip->sampleRate = 48000;
        REQUIRE(mixer.add(clip, SoundDesc{}) != kInvalidSoundHandle);

        const std::vector<std::int16_t> output = mixFrames(mixer, 4);
        CHECK(sampleAt(output, 0, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, 1, 0, 2) == Approx(2000.0).margin(2.0));
        CHECK(sampleAt(output, 2, 0, 2) == Approx(0.0).margin(1.0));
        CHECK(sampleAt(output, 3, 0, 2) == Approx(0.0).margin(1.0));
        CHECK(mixer.activeVoices() == 0);
        CHECK(mixer.stats().finished == 1);
    }
}

TEST_CASE("AudioMixer stops immediately and fades out", "[audio]") {
    {
        AudioMixer mixer;
        const SoundHandle handle = mixer.add(constantClip(1000, 64), SoundDesc{});
        REQUIRE(handle != kInvalidSoundHandle);
        CHECK(mixer.stop(handle, 0.0f));
        const std::vector<std::int16_t> output = mixFrames(mixer, 2);
        for (const std::int16_t value : output) {
            CHECK(value == 0);
        }
        CHECK(mixer.activeVoices() == 0);
        CHECK(mixer.stats().stopped == 1);
        CHECK_FALSE(mixer.stop(handle, 0.0f));
    }
    {
        AudioMixer mixer(1000, 2);
        const SoundHandle handle = mixer.add(constantClip(1000, 1000, 1000, 1), SoundDesc{});
        REQUIRE(handle != kInvalidSoundHandle);
        CHECK(mixer.stop(handle, 0.01f));

        const std::vector<std::int16_t> ramp = mixFrames(mixer, 5);
        CHECK(sampleAt(ramp, 0, 0, 2) == Approx(1000.0).margin(3.0));
        CHECK(sampleAt(ramp, 4, 0, 2) == Approx(600.0).margin(5.0));
        CHECK(sampleAt(ramp, 0, 0, 2) > sampleAt(ramp, 4, 0, 2));
        CHECK(mixer.activeVoices() == 1);

        /// The 10-frame fade has 5 frames left after the ramp check above.
        const std::vector<std::int16_t> tail = mixFrames(mixer, 5);
        CHECK(sampleAt(tail, 4, 0, 2) == Approx(100.0).margin(3.0));
        CHECK(mixer.activeVoices() == 0);

        const std::vector<std::int16_t> silent = mixFrames(mixer, 5);
        for (const std::int16_t value : silent) {
            CHECK(value == 0);
        }
    }
}

TEST_CASE("AudioMixer evicts the lowest-priority voice at the limit", "[audio]") {
    AudioMixer mixer;
    SoundDesc quiet;
    for (int index = 0; index < kMaxVoices; ++index) {
        REQUIRE(mixer.add(constantClip(1000, 1000), quiet) != kInvalidSoundHandle);
    }
    REQUIRE(mixer.activeVoices() == kMaxVoices);

    SoundDesc lower = quiet;
    lower.volume = 0.5f;
    CHECK(mixer.add(constantClip(1000, 1000), lower) == kInvalidSoundHandle);
    CHECK(mixer.stats().dropped == 1);
    CHECK(mixer.activeVoices() == kMaxVoices);

    SoundDesc louder = quiet;
    louder.priority = 5;
    const SoundHandle high = mixer.add(constantClip(1000, 1000), louder);
    REQUIRE(high != kInvalidSoundHandle);
    CHECK(mixer.stats().evicted == 1);
    CHECK(mixer.activeVoices() == kMaxVoices);
    CHECK(mixer.stop(high, 0.0f));
}

TEST_CASE("AudioMixer attenuates spatial voices when the listener is enabled", "[audio]") {
    AudioMixer mixer;
    SoundDesc desc;
    desc.spatial = true;
    desc.position = fa::core::Vec2{0.0f, 0.0f};
    REQUIRE(mixer.add(constantClip(1000, 64), desc) != kInvalidSoundHandle);

    mixer.setListener(fa::core::Vec2{0.0f, 0.0f}, true);
    const std::vector<std::int16_t> near = mixFrames(mixer, 1);
    CHECK(sampleAt(near, 0, 0, 2) == Approx(1000.0).margin(3.0));

    mixer.setListener(fa::core::Vec2{1000.0f, 0.0f}, true);
    const std::vector<std::int16_t> far = mixFrames(mixer, 1);
    CHECK(sampleAt(far, 0, 0, 2) < 300.0);

    mixer.setListener(fa::core::Vec2{1000.0f, 0.0f}, false);
    const std::vector<std::int16_t> disabled = mixFrames(mixer, 1);
    CHECK(sampleAt(disabled, 0, 0, 2) == Approx(1000.0).margin(3.0));
}

TEST_CASE("SpscQueue preserves order and counts overflow", "[audio]") {
    SpscQueue<std::unique_ptr<int>, 3> queue;
    CHECK(queue.capacity() == 2);

    CHECK(queue.tryPush(std::make_unique<int>(1)));
    CHECK(queue.tryPush(std::make_unique<int>(2)));
    CHECK_FALSE(queue.tryPush(std::make_unique<int>(3)));
    CHECK(queue.dropped() == 1);
    CHECK(queue.size() == 2);

    std::unique_ptr<int> value;
    REQUIRE(queue.tryPop(value));
    REQUIRE(value != nullptr);
    CHECK(*value == 1);
    REQUIRE(queue.tryPop(value));
    CHECK(*value == 2);
    CHECK_FALSE(queue.tryPop(value));
    CHECK(queue.empty());
}

TEST_CASE("AudioEngine drives a fake device", "[audio]") {
    FakeAudioDevice device;
    AudioEngine engine;
    REQUIRE(engine.initialize(device).isOk());
    CHECK(engine.isInitialized());
    CHECK(engine.stats().sampleRate == 48000);
    CHECK(engine.stats().channels == 2);

    const std::shared_ptr<const AudioClip> clip = constantClip(1000, 480);
    const SoundHandle handle = engine.play(clip, SoundDesc{});
    REQUIRE(handle != kInvalidSoundHandle);

    const std::vector<std::int16_t> output = device.pump(4);
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(sampleAt(output, frame, 0, 2) == Approx(1000.0).margin(2.0));
        CHECK(sampleAt(output, frame, 1, 2) == Approx(1000.0).margin(2.0));
    }
    CHECK(engine.stats().played == 1);
    CHECK(engine.stats().activeVoices == 1);

    engine.setBusVolume(AudioBus::SFX, 0.0f);
    const std::vector<std::int16_t> muted = device.pump(4);
    for (const std::int16_t value : muted) {
        CHECK(value == 0);
    }
    engine.setBusVolume(AudioBus::SFX, 1.0f);

    engine.stop(handle, 0.0f);
    const std::vector<std::int16_t> stopped = device.pump(4);
    for (const std::int16_t value : stopped) {
        CHECK(value == 0);
    }
    CHECK(engine.stats().stopped == 1);
    CHECK(engine.stats().activeVoices == 0);

    engine.shutdown();
    CHECK_FALSE(engine.isInitialized());
    CHECK_FALSE(device.isOpen());
    CHECK(engine.play(clip, SoundDesc{}) == kInvalidSoundHandle);
    engine.shutdown();
}

TEST_CASE("AudioEngine reports device initialization failure", "[audio]") {
    FakeAudioDevice device;
    device.fail = true;
    AudioEngine engine;

    const fa::core::Status status = engine.initialize(device);
    REQUIRE(status.isError());
    CHECK(status.error().code == fa::platform::platform_error::kAudioInitFailed);
    CHECK_FALSE(engine.isInitialized());
    CHECK_FALSE(device.isOpen());

    engine.shutdown();
    CHECK(engine.stats().played == 0);
}
