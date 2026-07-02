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
        CCLabelBMFont* lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        if (!lbl) lbl = CCLabelBMFont::create(text, "chatFont.fnt");
        if (!lbl) return nullptr;
        lbl->setAnchorPoint({0.f, 0.5f});
        addChild(lbl);
        return lbl;
    }

    void buildLabels() {
        auto rem = [](CCLabelBMFont*& l) { if (l) { l->removeFromParent(); l = nullptr; } };
        rem(fpsLbl); rem(cpsLbl); rem(attLbl); rem(bstLbl); rem(runLbl);
        rem(pctLbl); rem(timeLbl); rem(idLbl); rem(songLbl); rem(objLbl);

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

        float startX = S_float((entries.front().key + "-x").c_str());
        float startY = S_float((entries.front().key + "-y").c_str());
        float curY   = win.height - startY;

        for (auto& e : entries) {
            if (!e.lbl || !e.lbl->isVisible()) continue;
            float lineH = 26.f * e.lbl->getScale();
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
            int tot = (int)sessionTime;
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
            h->bstLbl->setString(fmt::format("Best: {}%", (int)m_level->m_normalPercent).c_str());
        if (h->attLbl)
            h->attLbl->setString(fmt::format("Attempts: {}", (int)m_level->m_attempts - m_fields->startAttempts).c_str());
        if (h->idLbl)
            h->idLbl->setString(fmt::format("ID: {}", m_level->m_levelID.value()).c_str());
        if (h->songLbl)
            h->songLbl->setString(fmt::format("Song: {}", m_level->getSongName()).c_str());
        if (h->objLbl)
            h->objLbl->setString(fmt::format("Objects: {}", m_level->m_objectCount.value()).c_str());
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

static CCSprite* createBgSprite(float w, float h) {
    auto* bg = CCSprite::create("labels_bg-uhd.png"_spr);
    if (!bg) bg = CCSprite::create("labels_bg.png"_spr);
    if (!bg) return nullptr;
    auto sz = bg->getContentSize();
    if (sz.width > 0 && sz.height > 0) {
        bg->setScaleX(w / sz.width);
        bg->setScaleY(h / sz.height);
    }
    return bg;
}

class LabelSettingPopup : public CCLayerColor {
protected:
    std::string    m_key;
    CCMenu*        m_menu     = nullptr;
    CCLabelBMFont* m_scaleLbl = nullptr;
    CCLabelBMFont* m_opacLbl  = nullptr;
    CCLabelBMFont* m_rLbl     = nullptr;
    CCLabelBMFont* m_gLbl     = nullptr;
    CCLabelBMFont* m_bLbl     = nullptr;

    bool init(const std::string& key, const std::string& labelName) {
        float pw = 260.f, ph = 300.f;
        auto win = CCDirector::sharedDirector()->getWinSize();
        if (!CCLayerColor::initWithColor({0, 0, 0, 180}, win.width, win.height)) return false;
        m_key = key;

        auto* bgSpr = createBgSprite(pw, ph);
        if (bgSpr) {
            bgSpr->setPosition({win.width / 2.f, win.height / 2.f});
            addChild(bgSpr, 2);
        }

        float cx = win.width / 2.f;
        float cy = win.height / 2.f;

        auto* titleLbl = CCLabelBMFont::create(labelName.c_str(), "goldFont.fnt");
        titleLbl->setScale(0.7f);
        titleLbl->setPosition({cx, cy + ph / 2.f - 20.f});
        addChild(titleLbl, 3);

        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        addChild(m_menu, 3);

        auto* showLbl = CCLabelBMFont::create("Show", "bigFont.fnt");
        showLbl->setScale(0.45f);
        showLbl->setPosition({cx - 60.f, cy + 60.f});
        addChild(showLbl, 3);

        auto* tog = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(LabelSettingPopup::onToggle), 0.7f);
        tog->toggle(S_bool((m_key + "-show").c_str()));
        tog->setPosition({cx + 40.f, cy + 60.f});
        m_menu->addChild(tog);

        float y = cy + 30.f;
        auto makeRow = [&](const char* label, CCLabelBMFont*& valLbl,
                            SEL_MenuHandler minSel, SEL_MenuHandler plusSel,
                            const std::string& val) {
            auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt");
            lbl->setScale(0.42f);
            lbl->setPosition({cx - 75.f, y});
            addChild(lbl, 3);

            auto* minBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("-", "bigFont.fnt"), this, minSel);
            minBtn->setPosition({cx + 10.f, y});
            m_menu->addChild(minBtn);

            valLbl = CCLabelBMFont::create(val.c_str(), "bigFont.fnt");
            valLbl->setScale(0.4f);
            valLbl->setPosition({cx + 40.f, y});
            addChild(valLbl, 3);

            auto* plusBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("+", "bigFont.fnt"), this, plusSel);
            plusBtn->setPosition({cx + 70.f, y});
            m_menu->addChild(plusBtn);

            y -= 28.f;
        };

        makeRow("Scale",   m_scaleLbl,
            menu_selector(LabelSettingPopup::onScaleMinus),
            menu_selector(LabelSettingPopup::onScalePlus),
            fmt::format("{:.2f}", S_float((m_key + "-scale").c_str())));
        makeRow("Opacity", m_opacLbl,
            menu_selector(LabelSettingPopup::onOpacMinus),
            menu_selector(LabelSettingPopup::onOpacPlus),
            fmt::format("{:.2f}", S_float((m_key + "-opacity").c_str())));
        makeRow("R", m_rLbl,
            menu_selector(LabelSettingPopup::onRMinus),
            menu_selector(LabelSettingPopup::onRPlus),
            fmt::format("{}", S_int((m_key + "-r").c_str())));
        makeRow("G", m_gLbl,
            menu_selector(LabelSettingPopup::onGMinus),
            menu_selector(LabelSettingPopup::onGPlus),
            fmt::format("{}", S_int((m_key + "-g").c_str())));
        makeRow("B", m_bLbl,
            menu_selector(LabelSettingPopup::onBMinus),
            menu_selector(LabelSettingPopup::onBPlus),
            fmt::format("{}", S_int((m_key + "-b").c_str())));

        auto* closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(LabelSettingPopup::onClose));
        closeBtn->setPosition({cx - pw / 2.f + 15.f, cy + ph / 2.f - 15.f});
        m_menu->addChild(closeBtn);

        setTouchEnabled(true);
        setKeypadEnabled(true);
        return true;
    }

    void keyBackClicked() override { onClose(nullptr); }
    void onClose(CCObject*) {
        removeFromParentAndCleanup(true);
    }

    void onToggle(CCObject* sender) {
        auto* tog = static_cast<CCMenuItemToggler*>(sender);
        (void)Mod::get()->setSettingValue<bool>(m_key + "-show", !tog->isToggled());
    }

    void adjScale(float d) {
        float v = std::clamp(S_float((m_key + "-scale").c_str()) + d, 0.1f, 2.f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-scale", (double)v);
        if (m_scaleLbl) m_scaleLbl->setString(fmt::format("{:.2f}", v).c_str());
    }
    void adjOpac(float d) {
        float v = std::clamp(S_float((m_key + "-opacity").c_str()) + d, 0.f, 1.f);
        (void)Mod::get()->setSettingValue<double>(m_key + "-opacity", (double)v);
        if (m_opacLbl) m_opacLbl->setString(fmt::format("{:.2f}", v).c_str());
    }
    void adjColor(const std::string& ch, int d) {
        int v = std::clamp(S_int((m_key + "-" + ch).c_str()) + d, 0, 255);
        (void)Mod::get()->setSettingValue<int64_t>(m_key + "-" + ch, (int64_t)v);
        auto* lbl = ch == "r" ? m_rLbl : ch == "g" ? m_gLbl : m_bLbl;
        if (lbl) lbl->setString(fmt::format("{}", v).c_str());
    }

    void onScaleMinus(CCObject*) { adjScale(-0.05f); }
    void onScalePlus (CCObject*) { adjScale( 0.05f); }
    void onOpacMinus (CCObject*) { adjOpac (-0.05f); }
    void onOpacPlus  (CCObject*) { adjOpac ( 0.05f); }
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

    void show(CCNode* parent) {
        parent->addChild(this, 9999);
    }
};

class LabelsPopup : public CCLayerColor {
protected:
    CCMenu* m_menu = nullptr;

    static constexpr float PW = 320.f;
    static constexpr float PH = 280.f;

    struct Row { std::string key; std::string name; };

    bool init() override {
        auto win = CCDirector::sharedDirector()->getWinSize();
        if (!CCLayerColor::initWithColor({0, 0, 0, 180}, win.width, win.height)) return false;

        auto* bgSpr = createBgSprite(PW, PH);
        if (bgSpr) {
            bgSpr->setPosition({win.width / 2.f, win.height / 2.f});
            addChild(bgSpr, 2);
        }

        float cx = win.width / 2.f;
        float cy = win.height / 2.f;

        auto* titleLbl = CCLabelBMFont::create("Labels", "goldFont.fnt");
        titleLbl->setScale(0.7f);
        titleLbl->setPosition({cx, cy + PH / 2.f - 20.f});
        addChild(titleLbl, 3);

        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        addChild(m_menu, 3);

        auto* closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(LabelsPopup::onClose));
        closeBtn->setPosition({cx - PW / 2.f + 15.f, cy + PH / 2.f - 15.f});
        m_menu->addChild(closeBtn);

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

        float rowH   = 26.f;
        float totalH = rowH * (float)rows.size();
        float listW  = PW - 20.f;
        float listH  = PH - 55.f;
        float listX  = cx - listW / 2.f;
        float listY  = cy - PH / 2.f + 10.f;

        auto* scroll = ScrollLayer::create({listW, listH});
        scroll->setPosition({listX, listY});
        scroll->setZOrder(3);
        addChild(scroll);

        auto* content = CCNode::create();
        content->setContentSize({listW, totalH});

        for (int i = 0; i < (int)rows.size(); i++) {
            auto& row = rows[i];
            float y = totalH - rowH * (float)i - rowH / 2.f;

            auto* nameLbl = CCLabelBMFont::create(row.name.c_str(), "bigFont.fnt");
            nameLbl->setScale(0.38f);
            auto* nameBtn = CCMenuItemSpriteExtra::create(
                nameLbl, this, menu_selector(LabelsPopup::onLabelBtn));
            nameBtn->setTag(i);

            auto* tog = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(LabelsPopup::onToggle), 0.6f);
            tog->toggle(S_bool((row.key + "-show").c_str()));
            tog->setTag(i);

            auto* editLbl = CCLabelBMFont::create("Edit", "bigFont.fnt");
            editLbl->setScale(0.35f);
            auto* editBtn = CCMenuItemSpriteExtra::create(
                editLbl, this, menu_selector(LabelsPopup::onLabelBtn));
            editBtn->setTag(i);

            auto* rowMenu = CCMenu::create();
            rowMenu->setPosition({0.f, 0.f});
            nameBtn->setPosition({listW * 0.25f, y});
            tog->setPosition({listW - 52.f, y});
            editBtn->setPosition({listW - 22.f, y});
            rowMenu->addChild(nameBtn);
            rowMenu->addChild(tog);
            rowMenu->addChild(editBtn);
            content->addChild(rowMenu);
        }

        scroll->m_contentLayer->addChild(content);
        scroll->m_contentLayer->setContentSize({listW, std::max(totalH, listH)});
        scroll->moveToTop();

        setTouchEnabled(true);
        setKeypadEnabled(true);
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

    void keyBackClicked() override { onClose(nullptr); }
    void onClose(CCObject*) { removeFromParentAndCleanup(true); }

    void onToggle(CCObject* sender) {
        auto* tog = static_cast<CCMenuItemToggler*>(sender);
        int idx = tog->getTag();
        if (idx < 0 || idx >= (int)getKeys().size()) return;
        (void)Mod::get()->setSettingValue<bool>(getKeys()[idx] + "-show", !tog->isToggled());
    }

    void onLabelBtn(CCObject* sender) {
        int idx = static_cast<CCNode*>(sender)->getTag();
        if (idx < 0 || idx >= (int)getKeys().size()) return;
        auto* popup = LabelSettingPopup::create(getKeys()[idx], getNames()[idx]);
        if (popup) popup->show(getParent());
    }

public:
    static LabelsPopup* create() {
        auto* r = new LabelsPopup();
        if (r && r->init()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }

    void show(CCNode* parent) {
        parent->addChild(this, 9999);
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
        auto* popup = LabelsPopup::create();
        if (popup) popup->show(this);
    }
};

$on_mod(Loaded) {
    log::info("Labels v1.0.1 loaded");
}
