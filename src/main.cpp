#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/TextInput.hpp>
#include <chrono>

using namespace geode::prelude;
using Clock = std::chrono::steady_clock;

static bool  S_bool (const char* k) { return Mod::get()->getSettingValue<bool>(k); }
static float S_float(const char* k) { return (float)Mod::get()->getSettingValue<double>(k); }
static int   S_int  (const char* k) { return (int)Mod::get()->getSettingValue<int64_t>(k); }


class HUDLayer : public CCLayer {
public:
    CCLabelBMFont* fpsLbl  = nullptr;
    CCLabelBMFont* cpsLbl  = nullptr;
    CCLabelBMFont* attLbl  = nullptr;
    CCLabelBMFont* bstLbl  = nullptr;
    CCLabelBMFont* runLbl  = nullptr;
    CCLabelBMFont* pctLbl  = nullptr;
    CCLabelBMFont* timeLbl = nullptr;
    CCLabelBMFont* idLbl   = nullptr;
    CCLabelBMFont* songLbl = nullptr;
    CCLabelBMFont* objLbl  = nullptr;

    static constexpr int MAX_CLICKS = 512;
    float clickTimes[MAX_CLICKS] = {};
    int   clickHead   = 0;
    int   clickCount  = 0;
    int   totalClicks = 0;
    int   curCPS      = 0;
    int   maxCPS      = 0;
    float elapsed     = 0.f;
    float clickFlash  = 0.f;

    float sessionTime = 0.f;

    static constexpr int FPS_WINDOW = 30;
    float fpsSamples[FPS_WINDOW] = {};
    int   fpsHead    = 0;
    int   fpsFilled  = 0;

    Clock::time_point prevTime;
    bool              firstFrame = true;

    static HUDLayer* create() {
        auto* r = new HUDLayer();
        if (r && r->init()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        buildLabels();
        scheduleUpdate();
        return true;
    }

    CCLabelBMFont* makeLabel(const char* text) {
        const char* fonts[] = {"bigFont.fnt", "chatFont.fnt"};
        CCLabelBMFont* lbl = nullptr;
        for (auto* font : fonts) {
            lbl = CCLabelBMFont::create(text, font);
            if (lbl) break;
        }
        if (!lbl) return nullptr;
        lbl->setAnchorPoint({0.f, 0.5f});
        addChild(lbl);
        return lbl;
    }

    void buildLabels() {
        auto rem = [](CCLabelBMFont*& l) { if (l) { l->removeFromParent(); l = nullptr; } };
        rem(fpsLbl); rem(cpsLbl); rem(attLbl); rem(bstLbl); rem(runLbl);
        rem(pctLbl); rem(timeLbl); rem(idLbl);  rem(songLbl); rem(objLbl);

        fpsLbl  = makeLabel("FPS: 0");
        cpsLbl  = makeLabel("Clicks: 0 | CPS: 0 | Max: 0");
        attLbl  = makeLabel("Attempts: 0");
        bstLbl  = makeLabel("Best: 0%");
        runLbl  = makeLabel("From: 0%");
        pctLbl  = makeLabel("Now: 0%");
        timeLbl = makeLabel("Time: 0:00");
        idLbl   = makeLabel("ID: 0");
        songLbl = makeLabel("Song: -");
        objLbl  = makeLabel("Objects: 0");

        applySettings();
    }

    void applySettings() {
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto* pl = PlayLayer::get();
        bool isPlatformer = pl && pl->m_level && pl->m_level->isPlatformer();

        struct Entry { CCLabelBMFont* lbl; std::string key; bool hidePlatformer; };
        std::vector<Entry> entries = {
            {fpsLbl,  "fps",      false},
            {cpsLbl,  "cps",      false},
            {attLbl,  "attempts", false},
            {bstLbl,  "best",     false},
            {runLbl,  "runfrom",  true },
            {pctLbl,  "curpct",   true },
            {timeLbl, "time",     false},
            {idLbl,   "levelid",  false},
            {songLbl, "song",     false},
            {objLbl,  "objects",  false},
        };

        for (auto& e : entries) {
            if (!e.lbl) continue;
            bool show = S_bool((e.key + "-show").c_str());
            if (e.hidePlatformer && isPlatformer) show = false;
            e.lbl->setVisible(show);
            e.lbl->setScale  (S_float((e.key + "-scale").c_str()));
            e.lbl->setOpacity((GLubyte)(S_float((e.key + "-opacity").c_str()) * 255.f));
            e.lbl->setColor  ({(GLubyte)S_int((e.key + "-r").c_str()),
                               (GLubyte)S_int((e.key + "-g").c_str()),
                               (GLubyte)S_int((e.key + "-b").c_str())});
        }

        float startX = 5.f;
        float startY = -1.f;
        float curY   = 0.f;

        for (auto& e : entries) {
            if (!e.lbl || !e.lbl->isVisible()) continue;
            float lineH = 26.f * e.lbl->getScale();
            if (startY < 0.f) {
                startX = S_float((e.key + "-x").c_str());
                startY = S_float((e.key + "-y").c_str());
                curY   = win.height - startY;
            }
            e.lbl->setPosition({startX, curY});
            curY -= lineH + 2.f;
        }
    }

    void registerClick() {
        clickTimes[clickHead % MAX_CLICKS] = elapsed;
        clickHead++;
        if (clickCount < MAX_CLICKS) clickCount++;
        totalClicks++;
        clickFlash = 0.15f;
    }

    void resetCPS() {
        for (auto& t : clickTimes) t = 0.f;
        clickHead = clickCount = totalClicks = curCPS = maxCPS = 0;
        elapsed = clickFlash = 0.f;
        if (cpsLbl) cpsLbl->setString("Clicks: 0 | CPS: 0 | Max: 0");
    }

    void resetTime() {
        sessionTime = 0.f;
        if (timeLbl) timeLbl->setString("Time: 0:00");
    }

    void resetFPS() {
        firstFrame = true;
        fpsHead = fpsFilled = 0;
        for (auto& s : fpsSamples) s = 0.f;
    }

    void update(float) override {
        auto  now = Clock::now();
        float dt  = firstFrame ? 0.016f
                               : std::chrono::duration<float>(now - prevTime).count();
        prevTime   = now;
        firstFrame = false;
        if (dt <= 0.f || dt > 0.5f) dt = 0.016f;

        elapsed     += dt;
        sessionTime += dt;

        if (fpsLbl && fpsLbl->isVisible()) {
            fpsSamples[fpsHead] = dt;
            fpsHead = (fpsHead + 1) % FPS_WINDOW;
            if (fpsFilled < FPS_WINDOW) fpsFilled++;
            float sum = 0.f;
            for (int i = 0; i < fpsFilled; i++) sum += fpsSamples[i];
            if (sum > 0.f)
                fpsLbl->setString(fmt::format("FPS: {}", (int)(fpsFilled / sum + 0.5f)).c_str());
        }

        if (cpsLbl && cpsLbl->isVisible()) {
            clickFlash = std::max(0.f, clickFlash - dt);
            int cps = 0;
            float windowStart = elapsed - 1.f;
            int total = std::min(clickCount, MAX_CLICKS);
            for (int i = 0; i < total; i++)
                if (clickTimes[i] >= windowStart) cps++;
            curCPS = cps;
            if (curCPS > maxCPS) maxCPS = curCPS;
            cpsLbl->setString(
                fmt::format("Clicks: {} | CPS: {} | Max: {}", totalClicks, curCPS, maxCPS).c_str());
            if (clickFlash > 0.f)
                cpsLbl->setColor({0, 255, 0});
            else
                cpsLbl->setColor({(GLubyte)S_int("cps-r"),
                                  (GLubyte)S_int("cps-g"),
                                  (GLubyte)S_int("cps-b")});
        }

        if (timeLbl && timeLbl->isVisible()) {
            int tot  = (int)sessionTime;
            timeLbl->setString(fmt::format("Time: {}:{:02d}", tot / 60, tot % 60).c_str());
        }

        if (pctLbl && pctLbl->isVisible()) {
            auto* pl = PlayLayer::get();
            if (pl) {
                int pct = std::clamp((int)pl->getCurrentPercent(), 0, 100);
                pctLbl->setString(fmt::format("Now: {}%", pct).c_str());
            }
        }
    }
};


class $modify(MyPlayLayer, PlayLayer) {
public:
    struct Fields {
        HUDLayer* hud           = nullptr;
        int       startAttempts = 0;
        int       runFromPct    = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->hud = HUDLayer::create();
        if (!m_fields->hud) return true;
        addChild(m_fields->hud, 9999);
        m_fields->startAttempts = (int)m_level->m_attempts - 1;
        updateHUDStatic();
        listenForAllSettingChanges([this](std::string_view, std::shared_ptr<SettingV3>) {
            if (m_fields->hud) m_fields->hud->applySettings();
        });
        log::info("Labels: init done");
        return true;
    }

    void updateHUDStatic() {
        auto* h = m_fields->hud;
        if (!h) return;
        if (h->bstLbl)
            h->bstLbl->setString(
                fmt::format("Best: {}%", (int)m_level->m_normalPercent).c_str());
        if (h->attLbl)
            h->attLbl->setString(
                fmt::format("Attempts: {}", (int)m_level->m_attempts - m_fields->startAttempts).c_str());
        if (h->idLbl)
            h->idLbl->setString(
                fmt::format("ID: {}", m_level->m_levelID.value()).c_str());
        if (h->songLbl)
            h->songLbl->setString(fmt::format("Song: {}", m_level->getSongName()).c_str());
        if (h->objLbl)
            h->objLbl->setString(
                fmt::format("Objects: {}", m_level->m_objectCount.value()).c_str());
    }

    void updateRunFrom() {
        auto* h = m_fields->hud;
        if (h && h->runLbl)
            h->runLbl->setString(fmt::format("From: {}%", m_fields->runFromPct).c_str());
    }

    void onQuit() {
        if (m_fields->hud) {
            m_fields->hud->resetCPS();
            m_fields->hud->resetFPS();
            m_fields->hud->resetTime();
        }
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);
        updateHUDStatic();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        updateHUDStatic();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->hud) m_fields->hud->resetCPS();
        m_fields->runFromPct = std::clamp((int)getCurrentPercent(), 0, 100);
        updateRunFrom();
    }

    void pauseGame(bool unfocused) {
        if (m_fields->hud) m_fields->hud->setVisible(false);
        PlayLayer::pauseGame(unfocused);
    }

    void resume() {
        PlayLayer::resume();
        if (m_fields->hud) { m_fields->hud->setVisible(true); m_fields->hud->resetFPS(); }
    }
};


struct MyPlayerObject : geode::Modify<MyPlayerObject, PlayerObject> {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::pushButton", 999);
    }
    bool pushButton(PlayerButton btn) {
        bool result = PlayerObject::pushButton(btn);
        if (btn != PlayerButton::Jump) return result;
        auto* pl = PlayLayer::get();
        if (!pl || this != pl->m_player1) return result;
        auto* myPl = static_cast<MyPlayLayer*>(pl);
        if (myPl && myPl->m_fields->hud)
            myPl->m_fields->hud->registerClick();
        return result;
    }
};


class LabelSettingPopup : public geode::Popup {
protected:
    static constexpr float PW = 360.f;
    static constexpr float PH = 380.f;

    std::string m_key;
    std::string m_labelName;

    CCLabelBMFont* m_preview = nullptr;
    TextInput* m_scaleInput = nullptr;
    TextInput* m_opacInput  = nullptr;
    TextInput* m_xInput     = nullptr;
    TextInput* m_yInput     = nullptr;
    TextInput* m_rInput     = nullptr;
    TextInput* m_gInput     = nullptr;
    TextInput* m_bInput     = nullptr;

    bool init(std::string const& key, std::string const& labelName) {
        if (!Popup::init(PW, PH))
            return false;

        m_key       = key;
        m_labelName = labelName;
        setTitle(labelName.c_str());

        float cx = PW / 2.f;
        float panelW = PW - 40.f;
        float panelH = PH - 90.f;

        auto* border = CCLayerColor::create({0, 0, 0, 120}, panelW + 4.f, panelH + 4.f);
        border->setPosition({cx - panelW / 2.f - 2.f, 14.f});
        m_mainLayer->addChild(border, -2);

        auto* panel = CCLayerColor::create({26, 18, 12, 200}, panelW, panelH);
        panel->setPosition({cx - panelW / 2.f, 16.f});
        m_mainLayer->addChild(panel, -1);

        float topY = PH - 60.f;

        auto* previewBg = CCLayerColor::create({0, 0, 0, 200}, panelW - 16.f, 32.f);
        previewBg->setPosition({cx - (panelW - 16.f) / 2.f, topY - 16.f});
        m_mainLayer->addChild(previewBg);

        m_preview = CCLabelBMFont::create(labelName.c_str(), "bigFont.fnt");
        m_preview->setScale(0.5f);
        m_preview->setOpacity((GLubyte)(S_float((m_key + "-opacity").c_str()) * 255.f));
        m_preview->setColor({(GLubyte)S_int((m_key + "-r").c_str()),
                             (GLubyte)S_int((m_key + "-g").c_str()),
                             (GLubyte)S_int((m_key + "-b").c_str())});
        m_preview->setPosition({cx, topY});
        m_mainLayer->addChild(m_preview);

        float rowGap = 36.f;
        float y = topY - 44.f;
        float labelX  = cx - panelW / 2.f + 14.f;
        float minusX  = cx + panelW / 2.f - 130.f;
        float inputX  = cx + panelW / 2.f - 70.f;
        float plusX   = cx + panelW / 2.f - 14.f;
        float toggleX = cx + panelW / 2.f - 30.f;

        addRowLabel("Show", labelX, y);
        auto* tog = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(LabelSettingPopup::onToggle), 0.65f);
        tog->toggle(S_bool((m_key + "-show").c_str()));
        tog->setPosition({toggleX, y});
        m_buttonMenu->addChild(tog);
        y -= rowGap;

        addRowLabel("Scale", labelX, y);
        m_scaleInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{:.2f}", S_float((m_key + "-scale").c_str())),
            menu_selector(LabelSettingPopup::onScaleMinus),
            menu_selector(LabelSettingPopup::onScalePlus),
            [this](const std::string& s) { setScaleFromText(s); });
        y -= rowGap;

        addRowLabel("Opacity", labelX, y);
        m_opacInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{:.2f}", S_float((m_key + "-opacity").c_str())),
            menu_selector(LabelSettingPopup::onOpacMinus),
            menu_selector(LabelSettingPopup::onOpacPlus),
            [this](const std::string& s) { setOpacFromText(s); });
        y -= rowGap;

        addRowLabel("Pos X", labelX, y);
        m_xInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{:.0f}", S_float((m_key + "-x").c_str())),
            menu_selector(LabelSettingPopup::onXMinus),
            menu_selector(LabelSettingPopup::onXPlus),
            [this](const std::string& s) { setPosFromText("x", s); });
        y -= rowGap;

        addRowLabel("Pos Y", labelX, y);
        m_yInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{:.0f}", S_float((m_key + "-y").c_str())),
            menu_selector(LabelSettingPopup::onYMinus),
            menu_selector(LabelSettingPopup::onYPlus),
            [this](const std::string& s) { setPosFromText("y", s); });
        y -= rowGap;

        addRowLabel("Red", labelX, y);
        m_rInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{}", S_int((m_key + "-r").c_str())),
            menu_selector(LabelSettingPopup::onRMinus),
            menu_selector(LabelSettingPopup::onRPlus),
            [this](const std::string& s) { setColorFromText("r", s); });
        y -= rowGap;

        addRowLabel("Green", labelX, y);
        m_gInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{}", S_int((m_key + "-g").c_str())),
            menu_selector(LabelSettingPopup::onGMinus),
            menu_selector(LabelSettingPopup::onGPlus),
            [this](const std::string& s) { setColorFromText("g", s); });
        y -= rowGap;

        addRowLabel("Blue", labelX, y);
        m_bInput = addInputRow(y, minusX, inputX, plusX,
            fmt::format("{}", S_int((m_key + "-b").c_str())),
            menu_selector(LabelSettingPopup::onBMinus),
            menu_selector(LabelSettingPopup::onBPlus),
            [this](const std::string& s) { setColorFromText("b", s); });

        return true;
    }

    void addRowLabel(const char* text, float x, float y) {
        auto* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setScale(0.4f);
        lbl->setPosition({x, y});
        m_mainLayer->addChild(lbl);
    }

    TextInput* addInputRow(float y, float minusX, float inputX, float plusX,
                            const std::string& valueText,
                            SEL_MenuHandler minusSel, SEL_MenuHandler plusSel,
                            std::function<void(const std::string&)> onType) {
        auto* minusBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("-", "bigFont.fnt", "GJ_button_01.png", 0.5f),
            this, minusSel);
        minusBtn->setScale(0.55f);
        minusBtn->setPosition({minusX, y});

        auto* input = TextInput::create(64.f, "0", "bigFont.fnt");
        input->setScale(0.85f);
        input->setString(valueText);
        input->setFilter("-.0123456789");
        input->setPosition({inputX, y});
        input->setCallback(onType);
        m_mainLayer->addChild(input);

        auto* plusBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("+", "bigFont.fnt", "GJ_button_01.png", 0.5f),
            this, plusSel);
        plusBtn->setScale(0.55f);
        plusBtn->setPosition({plusX, y});

        m_buttonMenu->addChild(minusBtn);
        m_buttonMenu->addChild(plusBtn);

        return input;
    }

    void refreshPreview() {
        if (!m_preview) return;
        m_preview->setOpacity((GLubyte)(S_float((m_key + "-opacity").c_str()) * 255.f));
        m_preview->setColor({(GLubyte)S_int((m_key + "-r").c_str()),
                             (GLubyte)S_int((m_key + "-g").c_str()),
                             (GLubyte)S_int((m_key + "-b").c_str())});
    }

    void onToggle(CCObject* sender) {
        auto* tog = static_cast<CCMenuItemToggler*>(sender);
        (void)Mod::get()->setSettingValue<bool>(m_key + "-show", !tog->isToggled());
    }

    void setScaleFromText(const std::string& s) {
        float v = std::clamp(strtof(s.c_str(), nullptr), 0.1f, 2.0f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-scale", (double)v);
        refreshPreview();
    }

    void setOpacFromText(const std::string& s) {
        float v = std::clamp(strtof(s.c_str(), nullptr), 0.0f, 1.0f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-opacity", (double)v);
        refreshPreview();
    }

    void setPosFromText(const std::string& axis, const std::string& s) {
        float v = strtof(s.c_str(), nullptr);
        (void)Mod::get()->setSettingValue<double>(m_key + "-" + axis, (double)v);
    }

    void setColorFromText(const std::string& ch, const std::string& s) {
        int v = std::clamp(atoi(s.c_str()), 0, 255);
        (void)Mod::get()->setSettingValue<int64_t>(m_key + "-" + ch, (int64_t)v);
        refreshPreview();
    }

    void adjScale(float delta) {
        float v = std::clamp(S_float((m_key + "-scale").c_str()) + delta, 0.1f, 2.0f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-scale", (double)v);
        if (m_scaleInput) m_scaleInput->setString(fmt::format("{:.2f}", v), false);
        refreshPreview();
    }

    void adjOpac(float delta) {
        float v = std::clamp(S_float((m_key + "-opacity").c_str()) + delta, 0.0f, 1.0f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-opacity", (double)v);
        if (m_opacInput) m_opacInput->setString(fmt::format("{:.2f}", v), false);
        refreshPreview();
    }

    void adjPos(const std::string& axis, float delta) {
        float v = S_float((m_key + "-" + axis).c_str()) + delta;
        (void)Mod::get()->setSettingValue<double>(m_key + "-" + axis, (double)v);
        auto* input = axis == "x" ? m_xInput : m_yInput;
        if (input) input->setString(fmt::format("{:.0f}", v), false);
    }

    void adjColor(const std::string& ch, int delta) {
        int v = std::clamp(S_int((m_key + "-" + ch).c_str()) + delta, 0, 255);
        (void)Mod::get()->setSettingValue<int64_t>(m_key + "-" + ch, (int64_t)v);
        auto* input = ch == "r" ? m_rInput : ch == "g" ? m_gInput : m_bInput;
        if (input) input->setString(fmt::format("{}", v), false);
        refreshPreview();
    }

    void onScaleMinus(CCObject*) { adjScale(-0.05f); }
    void onScalePlus (CCObject*) { adjScale( 0.05f); }
    void onOpacMinus (CCObject*) { adjOpac (-0.05f); }
    void onOpacPlus  (CCObject*) { adjOpac ( 0.05f); }
    void onXMinus(CCObject*) { adjPos("x", -5.f); }
    void onXPlus (CCObject*) { adjPos("x",  5.f); }
    void onYMinus(CCObject*) { adjPos("y", -5.f); }
    void onYPlus (CCObject*) { adjPos("y",  5.f); }
    void onRMinus(CCObject*) { adjColor("r", -5); }
    void onRPlus (CCObject*) { adjColor("r",  5); }
    void onGMinus(CCObject*) { adjColor("g", -5); }
    void onGPlus (CCObject*) { adjColor("g",  5); }
    void onBMinus(CCObject*) { adjColor("b", -5); }
    void onBPlus (CCObject*) { adjColor("b",  5); }

public:
    static LabelSettingPopup* create(const std::string& key, const std::string& name) {
        auto* r = new LabelSettingPopup();
        if (r && r->init(key, name)) {
            r->autorelease();
            return r;
        }
        delete r;
        return nullptr;
    }
};


class LabelsPopup : public geode::Popup {
protected:
    struct Row { std::string key; std::string name; };

    static constexpr float PW = 360.f;
    static constexpr float PH = 420.f;

    bool init() {
        if (!Popup::init(PW, PH))
            return false;
        setTitle("Labels");

        std::vector<Row> rows = {
            {"fps",      "FPS"},
            {"cps",      "CPS"},
            {"attempts", "Attempts"},
            {"best",     "Best %"},
            {"runfrom",  "Run From %"},
            {"curpct",   "Current %"},
            {"time",     "Session Time"},
            {"levelid",  "Level ID"},
            {"song",     "Song Name"},
            {"objects",  "Objects"},
        };

        float rowH  = 30.f;
        float listW = PW - 36.f;
        float listH = rowH * (float)rows.size();
        float listX = 18.f;
        float listY = PH - 56.f - listH;

        auto* border = CCLayerColor::create({0, 0, 0, 120}, listW + 4.f, listH + 4.f);
        border->setPosition({listX - 2.f, listY - 2.f});
        m_mainLayer->addChild(border, -2);

        auto* panel = CCLayerColor::create({26, 18, 12, 200}, listW, listH);
        panel->setPosition({listX, listY});
        m_mainLayer->addChild(panel, -1);

        for (int i = 0; i < (int)rows.size(); i++) {
            auto& row = rows[i];
            float rowBottom = listY + listH - rowH * (float)(i + 1);
            float y = rowBottom + rowH / 2.f;

            if (i % 2 == 0) {
                auto* stripe = CCLayerColor::create({255, 255, 255, 14}, listW, rowH);
                stripe->setPosition({listX, rowBottom});
                m_mainLayer->addChild(stripe);
            }
            if (i > 0) {
                auto* divider = CCLayerColor::create({0, 0, 0, 60}, listW, 1.f);
                divider->setPosition({listX, rowBottom + rowH});
                m_mainLayer->addChild(divider);
            }

            auto* nameLbl = CCLabelBMFont::create(row.name.c_str(), "bigFont.fnt");
            nameLbl->setAnchorPoint({0.f, 0.5f});
            nameLbl->setScale(0.4f);
            nameLbl->setPosition({listX + 10.f, y});
            m_mainLayer->addChild(nameLbl);

            auto* tog = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(LabelsPopup::onToggle), 0.5f);
            tog->toggle(S_bool((row.key + "-show").c_str()));
            tog->setTag(i);
            tog->setPosition({listX + listW - 76.f, y});
            m_buttonMenu->addChild(tog);

            auto* cfgSpr = ButtonSprite::create("Edit", "bigFont.fnt", "GJ_button_04.png", 0.4f);
            auto* cfgBtn = CCMenuItemSpriteExtra::create(
                cfgSpr, this, menu_selector(LabelsPopup::onLabelBtn));
            cfgBtn->setScale(0.65f);
            cfgBtn->setTag(i);
            cfgBtn->setPosition({listX + listW - 30.f, y});
            m_buttonMenu->addChild(cfgBtn);
        }

        return true;
    }

    static const std::vector<std::string>& getKeys() {
        static std::vector<std::string> keys = {
            "fps","cps","attempts","best","runfrom",
            "curpct","time","levelid","song","objects"
        };
        return keys;
    }

    static const std::vector<std::string>& getNames() {
        static std::vector<std::string> names = {
            "FPS","CPS","Attempts","Best %","Run From %",
            "Current %","Session Time","Level ID","Song Name","Objects"
        };
        return names;
    }

    void onToggle(CCObject* sender) {
        auto* tog = static_cast<CCMenuItemToggler*>(sender);
        int idx = tog->getTag();
        if (idx < 0 || idx >= (int)getKeys().size()) return;
        (void)Mod::get()->setSettingValue<bool>(getKeys()[idx] + "-show", !tog->isToggled());
    }

    void onLabelBtn(CCObject* sender) {
        int idx = static_cast<CCNode*>(sender)->getTag();
        if (idx < 0 || idx >= (int)getKeys().size()) return;
        LabelSettingPopup::create(getKeys()[idx], getNames()[idx])->show();
    }

public:
    static LabelsPopup* create() {
        auto* r = new LabelsPopup();
        if (r && r->init()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }
};


class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        if (!PlayLayer::get()) return;

        auto* spr = ButtonSprite::create("Labels", "goldFont.fnt", "GJ_button_05.png", 0.6f);
        if (!spr) return;

        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(MyPauseLayer::onLabels));

        auto* bg = getChildByID("background");
        CCRect bgRect;
        if (bg) {
            bgRect = bg->boundingBox();
        } else {
            auto win = CCDirector::sharedDirector()->getWinSize();
            bgRect = CCRect{win.width * 0.125f, win.height * 0.05f,
                            win.width * 0.75f,  win.height * 0.9f};
        }

        CCSize bs = spr->getContentSize();
        float x = bgRect.getMinX() + bs.width  / 2.f + 14.f;
        float y = bgRect.getMaxY() - bs.height / 2.f - 14.f;

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->addChild(btn);
        btn->setPosition({x, y});
        addChild(menu, 10);
    }

    void onLabels(CCObject*) {
        LabelsPopup::create()->show();
    }
};

$on_mod(Loaded) {
    log::info("Labels v2.2.0 loaded");
}
