#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <chrono>

using namespace geode::prelude;
using Clock = std::chrono::steady_clock;

static bool  S_bool (const char* k) { return Mod::get()->getSettingValue<bool>(k); }
static float S_float(const char* k) { return (float)Mod::get()->getSettingValue<double>(k); }
static int   S_int  (const char* k) { return (int)Mod::get()->getSettingValue<int64_t>(k); }

class HUDLayer : public CCLayer {
public:
    CCLabelBMFont* fpsLbl     = nullptr;
    CCLabelBMFont* cpsLbl     = nullptr;
    CCLabelBMFont* attLbl     = nullptr;
    CCLabelBMFont* bstLbl     = nullptr;
    CCLabelBMFont* runLbl     = nullptr;
    CCLabelBMFont* pctLbl     = nullptr;
    CCLabelBMFont* timeLbl    = nullptr;
    CCLabelBMFont* idLbl      = nullptr;
    CCLabelBMFont* songLbl    = nullptr;
    CCLabelBMFont* objLbl     = nullptr;

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
        auto remove = [](CCLabelBMFont*& l) {
            if (l) { l->removeFromParent(); l = nullptr; }
        };
        remove(fpsLbl); remove(cpsLbl); remove(attLbl); remove(bstLbl);
        remove(runLbl); remove(pctLbl); remove(timeLbl);
        remove(idLbl);  remove(songLbl); remove(objLbl);

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

        // Label order — defines stacking order top to bottom
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

        // Apply style (color, scale, opacity) to all labels first
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

        // Stack visible labels top-to-bottom with no gaps
        // X and start Y come from the first visible label's settings
        float startY = -1.f;
        float startX = 5.f;
        float curY   = 0.f;

        for (auto& e : entries) {
            if (!e.lbl || !e.lbl->isVisible()) continue;

            float scale  = e.lbl->getScale();
            // Height of one line at this scale (bigFont.fnt glyph height ~26px)
            float lineH  = 26.f * scale;

            if (startY < 0.f) {
                // First visible label — use its X/Y setting as anchor
                startX = S_float((e.key + "-x").c_str());
                startY = S_float((e.key + "-y").c_str());
                curY   = win.height - startY;
            }

            e.lbl->setPosition({startX, curY});
            curY -= lineH + 2.f; // 2px gap between lines
        }
    }

    void applyOne(CCLabelBMFont* lbl, const std::string& k, const CCSize& win) {
        // Kept for compatibility but applySettings handles everything now
        if (!lbl) return;
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
            for (int i = 0; i < total; i++) {
                if (clickTimes[i] >= windowStart) cps++;
            }
            curCPS = cps;
            if (curCPS > maxCPS) maxCPS = curCPS;

            cpsLbl->setString(fmt::format("Clicks: {} | CPS: {} | Max: {}", totalClicks, curCPS, maxCPS).c_str());

            if (clickFlash > 0.f) {
                cpsLbl->setColor({0, 255, 0});
            } else {
                cpsLbl->setColor({(GLubyte)S_int("cps-r"),
                                  (GLubyte)S_int("cps-g"),
                                  (GLubyte)S_int("cps-b")});
            }
        }

        if (timeLbl && timeLbl->isVisible()) {
            int total = (int)sessionTime;
            int mins  = total / 60;
            int secs  = total % 60;
            timeLbl->setString(fmt::format("Time: {}:{:02d}", mins, secs).c_str());
        }

        if (pctLbl) {
            auto* pl = PlayLayer::get();
            if (pl && pctLbl->isVisible()) {
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

        if (h->songLbl) {
            h->songLbl->setString(fmt::format("Song: {}", m_level->getSongName()).c_str());
        }

        if (h->objLbl)
            h->objLbl->setString(
                fmt::format("Objects: {}", m_level->m_objectCount.value()).c_str());
    }

    void updateRunFrom() {
        auto* h = m_fields->hud;
        if (!h || !h->runLbl) return;
        h->runLbl->setString(
            fmt::format("From: {}%", m_fields->runFromPct).c_str());
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

        // Capture where this attempt starts AFTER reset
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
            float bgW = win.width * 0.75f;
            float bgH = win.height * 0.9f;
            bgRect = CCRect{(win.width - bgW) / 2.f, (win.height - bgH) / 2.f, bgW, bgH};
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
        openSettingsPopup(Mod::get());
    }
};

$on_mod(Loaded) {
    log::info("Labels v2.1.2 loaded");
}
