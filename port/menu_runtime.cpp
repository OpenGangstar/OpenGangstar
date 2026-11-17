#include "menu_runtime.h"
#include "menu_font.h"
#include "menu_raster.h"
#include "filesystem.h"
#include "retail_achievements.h"
#include "win32_touch.h"
#include "retail_audio.h"
#include "retail_save.h"

#include "FileSystemBase.h"
#include "IFileStream.h"

#include "base/tu_file.h"
#include "gameswf/gameswf_environment.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr char menuFile[] = "newmenu.swf";
constexpr unsigned volumePreviewMusic = 35;
constexpr unsigned volumePreviewEffect = 225;

void applyButtonLabel(gameswf::as_object& button)
{
    gameswf::as_value value;
    gameswf::as_value field;
    if (!button.get_member("_retailLabel", &value) || !button.get_member("text", &field)) return;
    auto* text = gameswf::cast_to<gameswf::edit_text_character>(field.to_object());
    if (text) text->set_text_value(value.to_tu_string());
}

struct MenuFileOpener {
    MenuFileOpener(std::vector<unsigned char>& bytes, FileSystemBase& assets,
                   const std::map<uint16_t, std::string>& exports);
    ~MenuFileOpener();

    std::vector<unsigned char>& bytes;
    FileSystemBase& assets;
    const std::map<uint16_t, std::string>& exports;
    size_t boundCount = 0;
};

thread_local MenuFileOpener* loadingMenu = nullptr;

MenuFileOpener::MenuFileOpener(std::vector<unsigned char>& bytes, FileSystemBase& assets,
                               const std::map<uint16_t, std::string>& exports)
    : bytes(bytes), assets(assets), exports(exports)
{
    loadingMenu = this;
    gameswf::register_file_opener_callback([](const char* path) -> tu_file* {
        if (!loadingMenu || std::strcmp(path, menuFile) != 0) return nullptr;
        auto& data = loadingMenu->bytes;
        return new tu_file(tu_file::memory_buffer, static_cast<int>(data.size()), data.data());
    });
}

MenuFileOpener::~MenuFileOpener() { loadingMenu = nullptr; }

void buttonFrame(const gameswf::fn_call& call, const char* label)
{
    auto* button = gameswf::cast_to<gameswf::character>(call.this_ptr);
    if (button && button->goto_labeled_frame(label)) {
        button->set_play_state(gameswf::character::STOP);
        applyButtonLabel(*button);
    }
}

gameswf::character* findClip(gameswf::as_object& owner, const char* name)
{
    gameswf::as_value value;
    if (!owner.get_member(name, &value)) return nullptr;
    return gameswf::cast_to<gameswf::character>(value.to_object());
}

gameswf::character& clip(gameswf::as_object& owner, const char* name)
{
    auto* found = findClip(owner, name);
    if (!found) throw std::runtime_error(std::string("Missing retail menu clip ") + name);
    return *found;
}

bool takeRelease(gameswf::character& button)
{
    gameswf::as_value released;
    if (!button.get_member("_retailReleased", &released) || !released.to_bool()) return false;
    button.set_member("_retailReleased", false);
    return true;
}

gameswf::rect screenBounds(gameswf::character& button)
{
    gameswf::rect bounds{};
    button.get_bound(&bounds);
    if (auto* parent = button.get_parent()) parent->get_world_matrix().transform(&bounds);
    return bounds;
}

void setText(gameswf::character& field, const char* value)
{
    auto* text = gameswf::cast_to<gameswf::edit_text_character>(&field);
    if (!text) text = gameswf::cast_to<gameswf::edit_text_character>(findClip(field, "text"));
    if (text) text->set_text_value(value);
}

void bindButton(gameswf::as_object& button)
{
    button.set_member("onRollOver", gameswf::as_value(+[](const gameswf::fn_call& event) {
        buttonFrame(event, "focus_in");
    }));
    button.set_member("onRollOut", gameswf::as_value(+[](const gameswf::fn_call& event) {
        buttonFrame(event, "focus_out");
    }));
    button.set_member("onPress", gameswf::as_value(+[](const gameswf::fn_call& event) {
        buttonFrame(event, "selected");
    }));
    button.set_member("onRelease", gameswf::as_value(+[](const gameswf::fn_call& event) {
        buttonFrame(event, "released");
        auto* clip = gameswf::cast_to<gameswf::character>(event.this_ptr);
        if (clip) {
            clip->set_member("_retailReleased", true);
            std::cout << "Released menu button " << clip->get_name().c_str() << ".\n" << std::flush;
        }
    }));
    button.set_member("onReleaseOutside", gameswf::as_value(+[](const gameswf::fn_call& event) {
        buttonFrame(event, "focus_out");
    }));
}
}

namespace gameswf {
bitmap_info* load_opengangstar_bitmap(Uint16 id)
{
    if (!loadingMenu) return nullptr;
    const auto name = loadingMenu->exports.find(id);
    if (name == loadingMenu->exports.end()) return nullptr;
    const std::unique_ptr<IFileStream> texture(loadingMenu->assets.openResource(name->second.c_str()));
    if (!texture) throw std::runtime_error("Could not open retail SWF bitmap " + name->second);
    auto pixels = readTexture(*texture, true);
    ++loadingMenu->boundCount;
    return makeMenuBitmap(std::move(pixels));
}
}

struct MenuRuntime::Runtime {
    std::vector<unsigned char> bytes;
    MenuRaster renderer;
    gameswf::player player;
    gameswf::gc_ptr<gameswf::root> root;
    gameswf::gc_ptr<gameswf::character> newGameButton;
    gameswf::character* continueButton = nullptr;
    bool canContinue = false;
    std::optional<RetailAchievementState> achievements;
    enum class Page { main, options, sound, language, confirmExit };
    Page page = Page::main;
    gameswf::character *main = nullptr, *options = nullptr, *volumes = nullptr;
    gameswf::character *optionsButton = nullptr, *soundButton = nullptr, *backButton = nullptr;
    gameswf::character *exitButton = nullptr, *confirmExit = nullptr, *yesButton = nullptr, *noButton = nullptr;
    gameswf::character *languageButton = nullptr, *languagePage = nullptr, *languageValue = nullptr;
    gameswf::character *previousLanguage = nullptr, *nextLanguage = nullptr, *applyLanguage = nullptr;
    std::vector<std::string> labels;
    size_t languageIndex = 0;
    std::optional<RetailLanguage> languageRequested;
    bool exitRequested = false;
    std::array<gameswf::character*, 2> sliders{}, mutes{}, values{};
    std::array<gameswf::rect, 2> sliderBounds{};
    RetailAudioPreferences preferences;
    ResourceFileSystem& assets;
    RetailAudio preview;
    std::optional<size_t> slider;
    bool pointerDown = false;
    std::optional<int> menuFocus;
    bool volumeChanged = false;

    explicit Runtime(ResourceFileSystem& assets) : assets(assets), preview(assets) {}

    void show(Page next)
    {
        if (page == Page::sound && next != Page::sound) preview.stopAll();
        page = next;
        menuFocus.reset();
        if (page == Page::sound) {
            preview.setBusVolumes(preferences.effects, preferences.music);
            playPreview(volumePreviewMusic, true);
        }
        main->set_visible(page == Page::main || page == Page::confirmExit);
        for (auto* button : {newGameButton.get_ptr(), optionsButton, exitButton})
            button->set_member("enabled", page == Page::main);
        continueButton->set_member("enabled", page == Page::main && canContinue);
        options->set_visible(page == Page::options || page == Page::sound || page == Page::language);
        languagePage->set_visible(page == Page::language);
        soundButton->set_member("enabled", page == Page::options);
        languageButton->set_member("enabled", page == Page::options);
        volumes->set_visible(page == Page::sound);
        confirmExit->set_visible(page == Page::confirmExit);
    }

    void playPreview(unsigned id, bool loop)
    {
        try {
            preview.play(id, loop);
        } catch (const std::runtime_error& error) {
            std::cerr << "Could not play menu volume preview: " << error.what() << '\n';
        }
    }

    void stepLanguage(size_t step)
    {
        languageIndex = (languageIndex + step) % retailLanguagePacks.size();
        if (!labels.empty()) setText(*languageValue, labels.at(2 + languageIndex).c_str());
    }

    void refreshVolumes()
    {
        const std::array gains{preferences.effects, preferences.music};
        for (size_t index = 0; index < gains.size(); ++index) {
            const auto gain = gains[index];
            const auto lastFrame = std::max(0, sliders[index]->get_frame_count() - 1);
            // The retail widget uses one frame per ten percentage points.
            sliders[index]->goto_frame(std::min(static_cast<int>(gain * 10), lastFrame));
            mutes[index]->goto_labeled_frame(gain == 0 ? "off" : "on");
            mutes[index]->set_play_state(gameswf::character::STOP);
            const auto text = std::to_string(std::lround(gain * 100));
            setText(*values[index], text.c_str());
        }
    }

    void changeVolume(size_t index, float value)
    {
        auto& gain = index == 0 ? preferences.effects : preferences.music;
        if (gain == value) return;
        gain = value;
        preview.setBusVolumes(preferences.effects, preferences.music);
        volumeChanged = true;
        refreshVolumes();
    }

    void saveVolumes()
    {
        if (!volumeChanged) return;
        try {
            saveRetailAudioPreferences(assets, preferences);
            volumeChanged = false;
        } catch (const std::runtime_error& error) {
            std::cerr << "Could not save volume preferences: " << error.what() << '\n';
        }
    }
};

MenuRuntime::MenuRuntime(ResourceFileSystem& assets, const std::map<uint16_t, std::string>& exports,
                         RetailLanguage language, bool startInOptions)
    : runtime(std::make_unique<Runtime>(assets))
{
    std::optional<RetailSavedState> checkpoint;
    try {
        checkpoint = readWindowsOpeningCheckpoint(assets);
        runtime->canContinue = checkpoint.has_value();
    } catch (const std::runtime_error& error) {
        std::cerr << "Could not load the Windows checkpoint: " << error.what() << '\n';
    }
    try {
        runtime->achievements = loadMenuAchievementState(assets, checkpoint ? &*checkpoint : nullptr);
        if (runtime->achievements)
            std::cout << "Restored " << runtime->achievements->completedCount() << " retail achievements.\n";
    } catch (const std::runtime_error& error) {
        std::cerr << "Could not restore saved achievements. " << error.what() << '\n';
    }
    const std::unique_ptr<IFileStream> file(assets.openResource(menuFile));
    if (!file || !file->size() || file->size() > 16 * 1024 * 1024)
        throw std::runtime_error("Could not open the retail SWF menu.");
    runtime->bytes.resize(static_cast<size_t>(file->size()));
    if (file->read(runtime->bytes.data(), runtime->bytes.size()) != runtime->bytes.size())
        throw std::runtime_error("The retail SWF menu is truncated.");
    const MenuFileOpener opener(runtime->bytes, assets, exports);
    installMenuFont(assets);
    gameswf::set_use_cache_files(false);
    runtime->root = runtime->player.load_file(menuFile);
    if (!runtime->root.get_ptr()) throw std::runtime_error("GameSWF could not load the retail menu.");
    if (opener.boundCount != exports.size())
        throw std::runtime_error("GameSWF did not bind every retail menu bitmap export.");

    runtime->root->set_display_viewport(0, 0, 480, 320);
    runtime->root->advance(1.f / 30);
    auto* movie = gameswf::cast_to<gameswf::sprite_instance>(runtime->root->get_root_movie());
    if (!movie) throw std::runtime_error("GameSWF did not initialize the retail menu timeline.");
    gameswf::as_value menuValue;
    if (!movie->get_member("widgetMainMenu", &menuValue) || !menuValue.to_object())
        throw std::runtime_error("GameSWF did not initialize the retail main menu widget.");
    auto* menu = menuValue.to_object();
    runtime->main = &clip(*movie, "widgetMainMenu");
    // The retail host selects a widget before drawing. The SWF itself places all screens together.
    for (int i = 0; auto* widget = movie->get_character(i); ++i)
        widget->set_visible(widget == menu);
    // MainMenu's retail constructor hides the Twitter and alternate purchase clips.
    for (const char* name : {"btnTwitter", "DoubleOption"}) {
        gameswf::as_value value;
        if (menu->get_member(name, &value)) {
            if (auto* clip = gameswf::cast_to<gameswf::character>(value.to_object()))
                clip->set_visible(false);
        }
    }
    // StringManager uses group 1 for menu.english. These indices come from MainMenu::GotFocus.
    constexpr std::array<unsigned, 4> labelIndices{0x25, 0x22, 0x2e, 0x1d};
    runtime->labels = readLocalizedStrings(assets, "menu", language);
    const auto& labels = runtime->labels;
    if (!labels.empty() && labels.size() <= *std::max_element(labelIndices.begin(), labelIndices.end()))
        throw std::runtime_error("The retail menu string table is missing primary button entries.");
    for (size_t i = 0; i < labelIndices.size(); ++i) {
        const auto name = "btnMain" + std::to_string(i);
        gameswf::as_value button;
        if (!menu->get_member(name.c_str(), &button) || !button.to_object())
            throw std::runtime_error(std::string("The retail SWF is missing menu button ") + name);
        auto* clip = gameswf::cast_to<gameswf::character>(button.to_object());
        if (!clip) throw std::runtime_error("The retail menu button is missing its timeline.");
        if (!labels.empty()) clip->set_member("_retailLabel", labels[labelIndices[i]].c_str());
        if (i == 0) runtime->continueButton = clip;
        if (i == 0 && !runtime->canContinue) {
            clip->set_member("enabled", false);
            clip->goto_labeled_frame("disabled");
            clip->set_play_state(gameswf::character::STOP);
        } else {
            bindButton(*clip);
            if (i == 1) runtime->newGameButton = clip;
            if (i == 2) runtime->optionsButton = clip;
            if (i == 3) runtime->exitButton = clip;
        }
        applyButtonLabel(*clip);
    }
    runtime->confirmExit = &clip(*movie, "widgetConfirm");
    runtime->yesButton = &clip(*runtime->confirmExit, "btnYes");
    runtime->noButton = &clip(*runtime->confirmExit, "btnNo");
    bindButton(*runtime->yesButton);
    bindButton(*runtime->noButton);
    if (!labels.empty()) {
        setText(clip(*runtime->confirmExit, "txtMessage"), labels.at(0x1e).c_str());
        runtime->yesButton->set_member("_retailLabel", labels.at(0x29).c_str());
        runtime->noButton->set_member("_retailLabel", labels.at(0x2a).c_str());
        applyButtonLabel(*runtime->yesButton);
        applyButtonLabel(*runtime->noButton);
    }

    runtime->options = &clip(*movie, "widgetIGMenu");
    runtime->volumes = &clip(*movie, "widgetVolumes");
    auto& group = clip(*runtime->options, "groupBoutons");
    constexpr std::array<unsigned, 4> optionLabels{0x11, 0x0d, 0, 0x17};
    for (size_t i = 0; i < optionLabels.size(); ++i) {
        auto& button = clip(group, ("btnMenu" + std::to_string(i)).c_str());
        if (!labels.empty()) button.set_member("_retailLabel", labels.at(optionLabels[i]).c_str());
        if (i == 0) {
            runtime->soundButton = &button;
            bindButton(button);
        } else if (i == 2) {
            runtime->languageButton = &button;
            bindButton(button);
        } else {
            button.set_member("enabled", false);
            button.goto_labeled_frame("disabled");
            button.set_play_state(gameswf::character::STOP);
        }
        applyButtonLabel(button);
    }
    runtime->backButton = &clip(*runtime->options, "btnBack");
    if (!labels.empty()) {
        runtime->backButton->set_member("_retailLabel", labels.at(0x27).c_str());
        applyButtonLabel(*runtime->backButton);
        setText(clip(*runtime->options, "txtTitle"), labels.at(0x36).c_str());
    }
    bindButton(*runtime->backButton);
    runtime->languagePage = &clip(*movie, "widgetLanguage");
    runtime->languageValue = &clip(*runtime->languagePage, "txtValue");
    runtime->previousLanguage = &clip(*runtime->languagePage, "btnLeft");
    runtime->nextLanguage = &clip(*runtime->languagePage, "btnRight");
    runtime->applyLanguage = &clip(*runtime->languagePage, "btnOk");
    for (auto* button : {runtime->previousLanguage, runtime->nextLanguage, runtime->applyLanguage})
        bindButton(*button);
    if (!labels.empty()) {
        setText(clip(*runtime->languagePage, "txtTitle"), labels.at(1).c_str());
        runtime->applyLanguage->set_member("_retailLabel", labels.at(0x2b).c_str());
        applyButtonLabel(*runtime->applyLanguage);
    }
    runtime->languageIndex = std::find(retailLanguagePacks.begin(), retailLanguagePacks.end(), language)
        - retailLanguagePacks.begin();
    runtime->stepLanguage(0);
    clip(*runtime->volumes, "playlist").set_visible(false);
    if (!labels.empty()) {
        setText(clip(*runtime->volumes, "txtSFX"), labels.at(0x13).c_str());
        setText(clip(*runtime->volumes, "txtMusic"), labels.at(0x12).c_str());
    }
    for (size_t i = 0; i < runtime->sliders.size(); ++i) {
        auto& bar = clip(*runtime->volumes, i == 0 ? "barSFX" : "barMusic");
        runtime->sliders[i] = &clip(bar, "btnVolume");
        runtime->mutes[i] = &clip(bar, "btnMute");
        runtime->values[i] = &clip(bar, "txtVolume");
        runtime->sliderBounds[i] = screenBounds(*runtime->sliders[i]);
    }
    try {
        const std::unique_ptr<IFileStream> preferences(assets.openSavedFile(retailPreferencesFilename));
        if (preferences) runtime->preferences = readRetailAudioPreferences(*preferences);
    } catch (const std::runtime_error& error) {
        std::cerr << "Could not load volume preferences: " << error.what() << '\n';
    }
    runtime->refreshVolumes();
    runtime->show(startInOptions ? Runtime::Page::options : Runtime::Page::main);


    std::cout << "GameSWF bound " << opener.boundCount << " retail menu bitmaps.\n";
    std::cout << "GameSWF advanced the retail main menu and found its four primary buttons.\n" << std::flush;
}

MenuRuntime::~MenuRuntime() = default;

void MenuRuntime::advance(double milliseconds)
{
    runtime->root->advance(static_cast<float>(milliseconds / 1000));
    if (runtime->page == Runtime::Page::sound) {
        try {
            runtime->preview.update();
        } catch (const std::runtime_error& error) {
            std::cerr << "Could not update menu volume preview: " << error.what() << '\n';
        }
    }
}

void MenuRuntime::pointer(int x, int y, bool down)
{
    const auto press = down && !runtime->pointerDown;
    const auto release = !down && runtime->pointerDown;
    if (runtime->page == Runtime::Page::sound) {
        const auto hit = [x, y](const gameswf::rect& bounds) {
            return x * 20 >= bounds.m_x_min && x * 20 <= bounds.m_x_max &&
                y * 20 >= bounds.m_y_min && y * 20 <= bounds.m_y_max;
        };
        if (press) {
            for (size_t i = 0; i < runtime->sliders.size(); ++i) {
                if (hit(screenBounds(*runtime->mutes[i]))) runtime->changeVolume(i, 0);
                else if (hit(runtime->sliderBounds[i])) runtime->slider = i;
            }
        }
        if ((down || release) && runtime->slider && (x != -1 || y != -1)) {
            const auto& bar = runtime->sliderBounds[*runtime->slider];
            const auto ratio = std::clamp((x * 20 - bar.m_x_min) / (bar.m_x_max - bar.m_x_min), 0.f, 1.f);
            runtime->changeVolume(*runtime->slider, float(std::lround(ratio * 100)) / 100);
        }
        if (release) {
            if (runtime->slider == 0 && x >= 0 && y >= 0)
                runtime->playPreview(volumePreviewEffect, false);
            runtime->slider.reset();
            runtime->saveVolumes();
        }
    }
    // GameSWF establishes hover before dispatching a press to the active clip.
    if (down && !runtime->pointerDown) {
        runtime->root->notify_mouse_state(x, y, 0);
        runtime->root->advance(0);
    }
    runtime->pointerDown = down;
    runtime->root->notify_mouse_state(x, y, down ? 1 : 0);
    // Process each edge immediately, even when a click fits inside one timer tick.
    runtime->root->advance(0);
    if (runtime->page == Runtime::Page::confirmExit) {
        if (takeRelease(*runtime->noButton)) runtime->show(Runtime::Page::main);
        else if (takeRelease(*runtime->yesButton)) runtime->exitRequested = true;
    } else if (runtime->page == Runtime::Page::main && takeRelease(*runtime->exitButton))
        runtime->show(Runtime::Page::confirmExit);
    else if (runtime->page == Runtime::Page::main && takeRelease(*runtime->optionsButton))
        runtime->show(Runtime::Page::options);
    else if (runtime->page != Runtime::Page::main && takeRelease(*runtime->backButton))
        runtime->show(runtime->page == Runtime::Page::options ? Runtime::Page::main : Runtime::Page::options);
    else if (runtime->page == Runtime::Page::options && takeRelease(*runtime->soundButton))
        runtime->show(Runtime::Page::sound);
    else if (runtime->page == Runtime::Page::options && takeRelease(*runtime->languageButton))
        runtime->show(Runtime::Page::language);
    else if (runtime->page == Runtime::Page::language) {
        if (takeRelease(*runtime->previousLanguage)) runtime->stepLanguage(retailLanguagePacks.size() - 1);
        else if (takeRelease(*runtime->nextLanguage)) runtime->stepLanguage(1);
        else if (takeRelease(*runtime->applyLanguage))
            runtime->languageRequested = retailLanguagePacks[runtime->languageIndex];
    }
}

void MenuRuntime::navigate(const MenuNavigation& navigation, bool persistPreferences)
{
    if (runtime->pointerDown ||
        (!navigation.step && !navigation.horizontal && !navigation.accept && !navigation.back)) return;
    using Page = Runtime::Page;
    if (navigation.back) {
        if (runtime->page == Page::confirmExit || runtime->page == Page::options)
            runtime->show(Page::main);
        else if (runtime->page == Page::sound || runtime->page == Page::language)
            runtime->show(Page::options);
        return;
    }

    std::vector<gameswf::character*> buttons;
    switch (runtime->page) {
    case Page::main:
        buttons = {runtime->newGameButton.get_ptr(), runtime->optionsButton, runtime->exitButton};
        if (runtime->canContinue) buttons.insert(buttons.begin(), runtime->continueButton);
        break;
    case Page::options: buttons = {runtime->soundButton, runtime->languageButton, runtime->backButton}; break;
    case Page::sound: buttons = {runtime->mutes[0], runtime->mutes[1], runtime->backButton}; break;
    case Page::language:
        buttons = {runtime->previousLanguage, runtime->nextLanguage, runtime->applyLanguage, runtime->backButton};
        break;
    case Page::confirmExit: buttons = {runtime->noButton, runtime->yesButton}; break;
    }
    const auto count = static_cast<int>(buttons.size());
    const bool sound = runtime->page == Page::sound;
    const auto step = navigation.step + (sound ? 0 : navigation.horizontal);
    const auto initial = runtime->menuFocus.value_or(step > 0 ? -1 : 0);
    const auto index = ((initial + step) % count + count) % count;
    runtime->menuFocus = index;
    const auto bounds = screenBounds(*buttons[index]);
    const auto x = static_cast<int>((bounds.m_x_min + bounds.m_x_max) / 40);
    const auto y = static_cast<int>((bounds.m_y_min + bounds.m_y_max) / 40);
    pointer(x, y, false);
    if (sound && index < static_cast<int>(runtime->sliders.size()) && (navigation.horizontal || navigation.accept)) {
        const float previous = index == 0 ? runtime->preferences.effects : runtime->preferences.music;
        const auto percent = std::lround(previous * 100);
        const auto next = navigation.horizontal ? std::clamp(percent + 10L * navigation.horizontal, 0L, 100L) : 0L;
        runtime->changeVolume(index, static_cast<float>(next) / 100);
        if (index == 0 && runtime->preferences.effects != previous)
            runtime->playPreview(volumePreviewEffect, false);
        if (persistPreferences) runtime->saveVolumes();
        return;
    }
    if (navigation.accept) {
        pointer(x, y, true);
        pointer(x, y, false);
    }
}

bool MenuRuntime::takeNewGameRequest()
{
    return runtime->page == Runtime::Page::main && takeRelease(*runtime->newGameButton);
}

bool MenuRuntime::takeContinueRequest()
{
    return runtime->page == Runtime::Page::main && runtime->canContinue && takeRelease(*runtime->continueButton);
}

bool MenuRuntime::canContinue() const
{
    return runtime->canContinue;
}

bool MenuRuntime::takeExitRequest()
{
    return std::exchange(runtime->exitRequested, false);
}

std::optional<RetailLanguage> MenuRuntime::takeLanguageRequest()
{
    return std::exchange(runtime->languageRequested, std::nullopt);
}

Image MenuRuntime::draw(const Image& background)
{
    return runtime->renderer.draw(*runtime->root.get_ptr(), background);
}
