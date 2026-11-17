#include "filesystem.h"
#include "city_assets.h"
#include "city_view.h"
#include "retail_audio.h"
#include "retail_radio.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int wmain(int argc, wchar_t** argv)
try {
    if (argc != 2) {
        std::cerr << "Usage: retail_audio_test <extracted game data directory>\n";
        return 2;
    }
    if (retailSoundPath(4) != "raw_004.ogg" || retailSoundPath(29) != "raw_029.ogg" ||
        retailSoundPath(273) != "raw_273.ogg")
        throw std::runtime_error("The retail sound ID did not map to the expected resource.");

    ResourceFileSystem assets(argv[1]);
    RetailAudio audio(assets);
    audio.setBusVolumes(0, 0);
    audio.updateV6Engine(true, 1000, 0);
    if (!audio.playing(175)) throw std::runtime_error("The retail V6 starter did not play.");
    audio.stop(175);
    audio.updateV6Engine(true, 1000, 0);
    if (!audio.playing(180) || audio.playing(179)) throw std::runtime_error("The V6 coast bank did not play.");
    audio.updateV6Engine(true, 3000, 1);
    if (!audio.playing(180) || !audio.playing(179) || audio.playing(172))
        throw std::runtime_error("The first RPM and load update skipped the retail filter or crossfade.");
    for (int i = 0; i < 20; ++i) audio.updateV6Engine(true, 3000, 1);
    if (audio.playing(180) || !audio.playing(178) || !audio.playing(172))
        throw std::runtime_error("The retail V6 RPM windows did not blend the on-load samples.");
    audio.updateV6Engine(false, 1000, 0);
    if (audio.playing(178) || audio.playing(172)) throw std::runtime_error("V6 loops survived engine shutdown.");
    audio.setBusVolumes(0.8f, 0.8f);
    for (const auto id : {CityDoorSounds::openId, CityDoorSounds::closeId}) {
        audio.play(id, false, 0);
        if (!audio.playing(id)) throw std::runtime_error("Retail door cue did not start muted playback.");
        audio.stop(id);
    }
    audio.play(35, true, 0);
    audio.play(225, false, 0);
    if (!audio.playing(35) || !audio.playing(225))
        throw std::runtime_error("Retail volume preview resources did not start playback.");
    audio.stopAll();
    audio.setBusVolumes(0, 0);
    audio.play(25, true);
    audio.play(14, true);
    if (!audio.playing(25) || !audio.playing(14))
        throw std::runtime_error("Scripted priority test could not start the ambient voices.");
    audio.playScriptCue({0, 29, CityAudioAction::playPriorityLoop});
    if (audio.playing(25) || audio.playing(14) || !audio.playing(29))
        throw std::runtime_error("Scripted priority loop did not replace the ambient voices.");
    audio.playScriptCue({0, 29, CityAudioAction::stop});
    if (audio.playing(29)) throw std::runtime_error("Scripted stop did not release the priority loop.");
    audio.setBusVolumes(0.8f, 0.8f);
    RetailAudio slowAudio(assets);
    audio.play(CityDoorSounds::closeId, false, 0, 2);
    slowAudio.play(CityDoorSounds::closeId, false, 0, 0.5f);
    for (int i = 0; i < 200 && audio.playing(CityDoorSounds::closeId); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        audio.update();
        slowAudio.update();
    }
    if (audio.playing(CityDoorSounds::closeId) || !slowAudio.playing(CityDoorSounds::closeId))
        throw std::runtime_error("Pitch did not change one-shot duration.");
    slowAudio.setPitch(CityDoorSounds::closeId, 1);
    slowAudio.setGain(CityDoorSounds::closeId, 0);
    slowAudio.stop(CityDoorSounds::closeId);
    std::cout << "Muted playback at distinct pitches advanced independently.\n";

    audio.play(CityDoorSounds::closeId, true, 0);
    audio.play(4, true, 0);
    audio.setBusVolumes(0.5f, 0.25f);
    audio.setGain(CityDoorSounds::closeId, 1);
    audio.setGain(4, 1);
    if (audio.mixGain(CityDoorSounds::closeId) != 0.5f || audio.mixGain(4) != 0.25f)
        throw std::runtime_error("Music and sound effects did not use separate volume settings.");
    audio.playPriority(29, true, 0);
    if (audio.mixGain(4) != 0)
        throw std::runtime_error("Priority audio did not mute music.");
    audio.stopAll();
    audio.setBusVolumes(0.8f, 0.8f);
    audio.playPriority(29, true, 0);
    audio.play(119, true, 0.5f);
    audio.play(CityDoorSounds::closeId, false, 0);
    if (!audio.playing(119) || audio.mixGain(119) != 0)
        throw std::runtime_error("Priority audio did not mute the restricted effect.");
    audio.setGain(CityDoorSounds::closeId, 0.5f);
    if (audio.mixGain(CityDoorSounds::closeId) != 0.4f)
        throw std::runtime_error("Priority audio muted an unrestricted effect.");
    audio.stop(29);
    if (audio.mixGain(119) != 0.4f)
        throw std::runtime_error("Stopping priority audio did not restore the restricted effect.");
    audio.stopAll();
    audio.play(131, false, 0);
    if (audio.playing(131)) throw std::runtime_error("An empty retail audio slot started playback.");
    audio.play(4, false, 0);
    audio.play(29, true, 0);
    for (int i = 0; i < 35; ++i) {
        audio.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!audio.playing(4) || !audio.playing(29))
        throw std::runtime_error("A queued retail stream stopped prematurely.");
    audio.stop(4);
    if (audio.playing(4) || !audio.playing(29))
        throw std::runtime_error("Stop affected the wrong stream.");
    audio.stop(29);
    if (audio.playing(29)) throw std::runtime_error("The loop did not stop.");
    std::cout << "Muted WaveOut playback, concurrent voices, and stop passed.\n";

    RadioPlayback radio(audio, loadRetailRadio(assets), 0);
    radio.start(2);
    if (radio.currentTrack() != 46 || !audio.playing(46))
        throw std::runtime_error("The default retail station did not start its expected track.");
    radio.change(1);
    if (radio.currentStation() != 3 || radio.currentTrack() != 124 ||
        audio.playing(46) || !audio.playing(124))
        throw std::runtime_error("Changing station did not replace the previous track with tuning audio.");
    for (int i = 0; i < 600 && radio.currentTrack() == 124; ++i) {
        audio.update();
        radio.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (radio.currentTrack() != 44 || !audio.playing(44))
        throw std::runtime_error("The station did not advance after tuning audio finished.");
    radio.change(1);
    radio.change(1);
    if (radio.currentStation() != 0) throw std::runtime_error("Radio station did not wrap.");
    const auto last = *radio.currentTrack();
    radio.stop();
    if (audio.playing(last)) throw std::runtime_error("Radio did not stop.");
    radio.start(2);
    if (radio.currentStation() != 0) throw std::runtime_error("Radio did not retain the selected station.");
    std::cout << "Muted radio start, tuning completion, wrap, stop, and station retention passed.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
