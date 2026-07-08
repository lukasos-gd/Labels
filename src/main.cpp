#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <chrono>

using namespace geode::prelude;
using SteadyClock = std::chrono::steady_clock;

static bool  getSettingBool  (const char* key) { return Mod::get()->getSettingValue<bool>(key); }
static float getSettingFloat (const char* key) { return (float)Mod::get()->getSettingValue<double>(key); }
static int   getSettingInt   (const char* key) { return (int)Mod::get()->getSettingValue<int64_t>(key); }

static void setSettingBool  (const char* key, bool value)     { (void)Mod::get()->setSettingValue<bool>(key, value); }
static void setSettingFloat (const char* key, double value)   { (void)Mod::get()->setSettingValue<double>(key, value); }
static void setSettingInt   (const char* key, int64_t value)  { (void)Mod::get()->setSettingValue<int64_t>(key, value); }

class HUDLayer : public CCLayer {
public:
    CCLabelBMFont* fpsLabel     = nullptr;
    CCLabelBMFont* cpsLabel     = nullptr;
    CCLabelBMFont* attemptsLabel = nullptr;
    CCLabelBMFont* bestLabel    = nullptr;
    CCLabelBMFont* runFromLabel = nullptr;
    CCLabelBMFont* currentLabel = nullptr;
    CCLabelBMFont* timeLabel    = nullptr;
    CCLabelBMFont* levelIDLabel = nullptr;
    CCLabelBMFont* songLabel    = nullptr;
    CCLabelBMFont* objectsLabel = nullptr;

    static constexpr int CLICK_BUFFER_SIZE = 512;
    float clickTimestamps[CLICK_BUFFER_SIZE] = {};
    int   clickWriteHead  = 0;
    int   clickBufferFill = 0;
    int   totalClicks     = 0;
    int   currentCPS      = 0;
    int   maxCPS          = 0;
    float elapsedTime     = 0.f;
    float cpsFlashTimer   = 0.f;
    float sessionDuration = 0.f;

    static constexpr int FPS_SAMPLE_COUNT = 30;
    float frameDurations[FPS_SAMPLE_COUNT] = {};
    int   fpsSampleHead  = 0;
    int   fpsSampleFill  = 0;

    SteadyClock::time_point lastFrameTime;
    bool isFirstFrame = true;

    static HUDLayer* create() {
        auto* layer = new HUDLayer();
        if (layer && layer->init()) {
            layer->autorelease();
            return layer;
        }
        delete layer;
        return nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;
        buildAllLabels();
        scheduleUpdate();
        return true;
    }

    CCLabelBMFont* createLabel(const char* text) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        if (!label) label = CCLabelBMFont::create(text, "chatFont.fnt");
        if (!label) return nullptr;
        label->setAnchorPoint({0.f, 0.5f});
        addChild(label);
        return label;
    }

    void buildAllLabels() {
        auto removeLabel = [](CCLabelBMFont*& label) {
            if (label) { label->removeFromParent(); label = nullptr; }
        };

        removeLabel(fpsLabel);
        removeLabel(cpsLabel);
        removeLabel(attemptsLabel);
        removeLabel(bestLabel);
        removeLabel(runFromLabel);
        removeLabel(currentLabel);
        removeLabel(timeLabel);
        removeLabel(levelIDLabel);
        removeLabel(songLabel);
        removeLabel(objectsLabel);

        fpsLabel      = createLabel("FPS: 0");
        cpsLabel      = createLabel("Clicks: 0 | CPS: 0 | Max: 0");
        attemptsLabel = createLabel("Attempts: 0");
        bestLabel     = createLabel("Best: 0%");
        runFromLabel  = createLabel("From: 0%");
        currentLabel  = createLabel("Now: 0%");
        timeLabel     = createLabel("Time: 0:00");
        levelIDLabel  = createLabel("ID: 0");
        songLabel     = createLabel("Song: -");
        objectsLabel  = createLabel("Objects: 0");

        applyAllSettings();
    }

    void applyAllSettings() {
        auto windowSize   = CCDirector::sharedDirector()->getWinSize();
        auto* playLayer   = PlayLayer::get();
        bool isPlatformer = playLayer && playLayer->m_level && playLayer->m_level->isPlatformer();

        struct LabelEntry {
            CCLabelBMFont* label;
            std::string settingKey;
            bool hideInPlatformer;
        };

        std::vector<LabelEntry> entries = {
            {fpsLabel,      "fps",      false},
            {cpsLabel,      "cps",      false},
            {attemptsLabel, "attempts", false},
            {bestLabel,     "best",     false},
            {runFromLabel,  "runfrom",  true },
            {currentLabel,  "curpct",   true },
            {timeLabel,     "time",     false},
            {levelIDLabel,  "levelid",  false},
            {songLabel,     "song",     false},
            {objectsLabel,  "objects",  false},
        };

        for (auto& entry : entries) {
            if (!entry.label) continue;

            bool shouldShow = getSettingBool((entry.settingKey + "-show").c_str());
            if (entry.hideInPlatformer && isPlatformer) shouldShow = false;

            entry.label->setVisible(shouldShow);
            entry.label->setScale(getSettingFloat((entry.settingKey + "-scale").c_str()));
            entry.label->setOpacity((GLubyte)(getSettingFloat((entry.settingKey + "-opacity").c_str()) * 255.f));
            entry.label->setColor({
                (GLubyte)getSettingInt((entry.settingKey + "-r").c_str()),
                (GLubyte)getSettingInt((entry.settingKey + "-g").c_str()),
                (GLubyte)getSettingInt((entry.settingKey + "-b").c_str()),
            });
        }

        float startX = getSettingFloat((entries.front().settingKey + "-x").c_str());
        float startY = getSettingFloat((entries.front().settingKey + "-y").c_str());
        float currentY = windowSize.height - startY;

        for (auto& entry : entries) {
            if (!entry.label || !entry.label->isVisible()) continue;
            entry.label->setPosition({startX, currentY});
            currentY -= 26.f * entry.label->getScale() + 2.f;
        }
    }

    void registerClick() {
        clickTimestamps[clickWriteHead % CLICK_BUFFER_SIZE] = elapsedTime;
        clickWriteHead++;
        if (clickBufferFill < CLICK_BUFFER_SIZE) clickBufferFill++;
        totalClicks++;
        cpsFlashTimer = 0.15f;
    }

    void resetCPS() {
        memset(clickTimestamps, 0, sizeof(clickTimestamps));
        clickWriteHead = clickBufferFill = totalClicks = currentCPS = maxCPS = 0;
        elapsedTime = cpsFlashTimer = 0.f;
        if (cpsLabel) cpsLabel->setString("Clicks: 0 | CPS: 0 | Max: 0");
    }

    void resetSessionTime() {
        sessionDuration = 0.f;
        if (timeLabel) timeLabel->setString("Time: 0:00");
    }

    void resetFPSCounter() {
        isFirstFrame  = true;
        fpsSampleHead = fpsSampleFill = 0;
        memset(frameDurations, 0, sizeof(frameDurations));
    }

    void update(float) override {
        auto  now      = SteadyClock::now();
        float deltaTime = isFirstFrame ? 0.016f : std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        isFirstFrame  = false;

        if (deltaTime <= 0.f || deltaTime > 0.5f) deltaTime = 0.016f;

        elapsedTime     += deltaTime;
        sessionDuration += deltaTime;

        if (fpsLabel && fpsLabel->isVisible()) {
            frameDurations[fpsSampleHead] = deltaTime;
            fpsSampleHead = (fpsSampleHead + 1) % FPS_SAMPLE_COUNT;
            if (fpsSampleFill < FPS_SAMPLE_COUNT) fpsSampleFill++;

            float totalDuration = 0.f;
            for (int i = 0; i < fpsSampleFill; i++) totalDuration += frameDurations[i];

            if (totalDuration > 0.f) {
                int averageFPS = (int)(fpsSampleFill / totalDuration + 0.5f);
                fpsLabel->setString(fmt::format("FPS: {}", averageFPS).c_str());
            }
        }

        if (cpsLabel && cpsLabel->isVisible()) {
            cpsFlashTimer = std::max(0.f, cpsFlashTimer - deltaTime);

            int clicksInLastSecond = 0;
            float windowStart = elapsedTime - 1.f;
            for (int i = 0; i < std::min(clickBufferFill, CLICK_BUFFER_SIZE); i++) {
                if (clickTimestamps[i] >= windowStart) clicksInLastSecond++;
            }

            currentCPS = clicksInLastSecond;
            if (currentCPS > maxCPS) maxCPS = currentCPS;

            cpsLabel->setString(fmt::format("Clicks: {} | CPS: {} | Max: {}", totalClicks, currentCPS, maxCPS).c_str());

            if (cpsFlashTimer > 0.f) {
                cpsLabel->setColor({0, 255, 0});
            } else {
                cpsLabel->setColor({
                    (GLubyte)getSettingInt("cps-r"),
                    (GLubyte)getSettingInt("cps-g"),
                    (GLubyte)getSettingInt("cps-b"),
                });
            }
        }

        if (timeLabel && timeLabel->isVisible()) {
            int totalSeconds = (int)sessionDuration;
            timeLabel->setString(fmt::format("Time: {}:{:02d}", totalSeconds / 60, totalSeconds % 60).c_str());
        }

        if (currentLabel && currentLabel->isVisible()) {
            if (auto* playLayer = PlayLayer::get()) {
                int percent = std::clamp((int)playLayer->getCurrentPercent(), 0, 100);
                currentLabel->setString(fmt::format("Now: {}%", percent).c_str());
            }
        }
    }
};

class $modify(LabelsPlayLayer, PlayLayer) {
    struct Fields {
        HUDLayer* hudLayer      = nullptr;
        int       startAttempts = 0;
        int       runFromPercent = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreate) {
        if (!PlayLayer::init(level, useReplay, dontCreate)) return false;

        m_fields->hudLayer = HUDLayer::create();
        if (!m_fields->hudLayer) return true;

        addChild(m_fields->hudLayer, 9999);
        m_fields->startAttempts = (int)m_level->m_attempts - 1;

        updateStaticLabels();

        listenForAllSettingChanges([this](std::string_view, std::shared_ptr<SettingV3>) {
            if (m_fields->hudLayer) m_fields->hudLayer->applyAllSettings();
        });

        return true;
    }

    void updateStaticLabels() {
        auto* hud = m_fields->hudLayer;
        if (!hud) return;

        if (hud->bestLabel)
            hud->bestLabel->setString(fmt::format("Best: {}%", (int)m_level->m_normalPercent).c_str());

        if (hud->attemptsLabel)
            hud->attemptsLabel->setString(fmt::format("Attempts: {}", (int)m_level->m_attempts - m_fields->startAttempts).c_str());

        if (hud->levelIDLabel)
            hud->levelIDLabel->setString(fmt::format("ID: {}", m_level->m_levelID.value()).c_str());

        if (hud->songLabel)
            hud->songLabel->setString(fmt::format("Song: {}", m_level->m_songID).c_str());

        if (hud->objectsLabel)
            hud->objectsLabel->setString(fmt::format("Objects: {}", m_level->m_objectCount.value()).c_str());

        if (hud->runFromLabel)
            hud->runFromLabel->setString(fmt::format("From: {}%", m_fields->runFromPercent).c_str());
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        updateStaticLabels();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        updateStaticLabels();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->hudLayer) m_fields->hudLayer->resetCPS();
        m_fields->runFromPercent = std::clamp((int)getCurrentPercent(), 0, 100);
        updateStaticLabels();
    }

    void pauseGame(bool unfocused) {
        PlayLayer::pauseGame(unfocused);
        if (m_fields->hudLayer) m_fields->hudLayer->setVisible(false);
    }

    void resume() {
        PlayLayer::resume();
        if (m_fields->hudLayer) {
            m_fields->hudLayer->setVisible(true);
            m_fields->hudLayer->resetFPSCounter();
        }
    }

    void onQuit() {
        if (m_fields->hudLayer) {
            m_fields->hudLayer->resetCPS();
            m_fields->hudLayer->resetFPSCounter();
            m_fields->hudLayer->resetSessionTime();
        }
        PlayLayer::onQuit();
    }
};

struct LabelsPlayerObject : geode::Modify<LabelsPlayerObject, PlayerObject> {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::pushButton", 999);
    }

    bool pushButton(PlayerButton button) {
        bool result = PlayerObject::pushButton(button);

        if (button != PlayerButton::Jump) return result;

        auto* playLayer = PlayLayer::get();
        if (!playLayer || this != playLayer->m_player1) return result;

        auto* modifiedLayer = static_cast<LabelsPlayLayer*>(playLayer);
        if (modifiedLayer && modifiedLayer->m_fields->hudLayer)
            modifiedLayer->m_fields->hudLayer->registerClick();

        return result;
    }
};

class LabelSettingsPopup : public CCLayer {
    std::string m_labelKey;
    CCMenu* m_menu = nullptr;
    CCLabelBMFont* m_valueLabels[7] = {};

    static constexpr float PW = 250.f;
    static constexpr float PH = 310.f;

    bool init(const std::string& labelKey, const std::string& labelName) {
        if (!CCLayer::init()) return false;
        m_labelKey = labelKey;

        auto win = CCDirector::sharedDirector()->getWinSize();
        float cx = win.width / 2.f;
        float cy = win.height / 2.f;

        auto* bg = CCScale9Sprite::create("GJ_square02.png");
        bg->setContentSize({PW, PH});
        bg->setPosition({cx, cy});
        addChild(bg, 0);

        auto* title = CCLabelBMFont::create(labelName.c_str(), "goldFont.fnt");
        title->setScale(0.6f);
        title->setPosition({cx, cy + PH / 2.f - 18.f});
        addChild(title, 1);

        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        addChild(m_menu, 1);

        auto* closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(LabelSettingsPopup::onClose));
        closeBtn->setPosition({cx - PW / 2.f + 14.f, cy + PH / 2.f - 14.f});
        m_menu->addChild(closeBtn);

        float startY = cy + PH / 2.f - 44.f;
        float gap    = (PH - 58.f) / 8.f;
        float y      = startY;

        auto* showLabel = CCLabelBMFont::create("Show", "bigFont.fnt");
        showLabel->setScale(0.42f);
        showLabel->setAnchorPoint({0.f, 0.5f});
        showLabel->setPosition({cx - PW / 2.f + 14.f, y});
        addChild(showLabel, 1);

        auto* showToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(LabelSettingsPopup::onToggleShow), 0.65f);
        showToggle->toggle(getSettingBool((m_labelKey + "-show").c_str()));
        showToggle->setPosition({cx + PW / 2.f - 24.f, y});
        m_menu->addChild(showToggle);
        y -= gap;

        struct Row { const char* name; int idx; };
        Row rows[] = {
            {"Scale", 0}, {"Opacity", 1}, {"Pos X", 2}, {"Pos Y", 3},
            {"Red", 4},   {"Green",  5},  {"Blue",  6},
        };

        for (auto& row : rows) {
            auto* lbl = CCLabelBMFont::create(row.name, "bigFont.fnt");
            lbl->setScale(0.4f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({cx - PW / 2.f + 14.f, y});
            addChild(lbl, 1);

            auto* minBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("-", "bigFont.fnt"),
                this, menu_selector(LabelSettingsPopup::onMinusButton));
            minBtn->setTag(row.idx);
            minBtn->setPosition({cx + 8.f, y});
            m_menu->addChild(minBtn);

            m_valueLabels[row.idx] = CCLabelBMFont::create(getValueStr(row.idx).c_str(), "bigFont.fnt");
            m_valueLabels[row.idx]->setScale(0.4f);
            m_valueLabels[row.idx]->setPosition({cx + 38.f, y});
            addChild(m_valueLabels[row.idx], 1);

            auto* plusBtn = CCMenuItemSpriteExtra::create(
                CCLabelBMFont::create("+", "bigFont.fnt"),
                this, menu_selector(LabelSettingsPopup::onPlusButton));
            plusBtn->setTag(row.idx);
            plusBtn->setPosition({cx + 70.f, y});
            m_menu->addChild(plusBtn);

            y -= gap;
        }

        setTouchEnabled(true);
        setKeypadEnabled(true);
        return true;
    }

    std::string getValueStr(int i) {
        switch (i) {
            case 0: return fmt::format("{:.2f}", getSettingFloat((m_labelKey + "-scale").c_str()));
            case 1: return fmt::format("{:.2f}", getSettingFloat((m_labelKey + "-opacity").c_str()));
            case 2: return fmt::format("{:.0f}", getSettingFloat((m_labelKey + "-x").c_str()));
            case 3: return fmt::format("{:.0f}", getSettingFloat((m_labelKey + "-y").c_str()));
            case 4: return fmt::format("{}", getSettingInt((m_labelKey + "-r").c_str()));
            case 5: return fmt::format("{}", getSettingInt((m_labelKey + "-g").c_str()));
            case 6: return fmt::format("{}", getSettingInt((m_labelKey + "-b").c_str()));
            default: return "";
        }
    }

    void stepValue(int i, float dir) {
        switch (i) {
            case 0: setSettingFloat((m_labelKey+"-scale").c_str(),   std::clamp((double)getSettingFloat((m_labelKey+"-scale").c_str())   + dir*0.05, 0.1, 2.0)); break;
            case 1: setSettingFloat((m_labelKey+"-opacity").c_str(), std::clamp((double)getSettingFloat((m_labelKey+"-opacity").c_str()) + dir*0.05, 0.0, 1.0)); break;
            case 2: setSettingFloat((m_labelKey+"-x").c_str(), (double)(getSettingFloat((m_labelKey+"-x").c_str()) + dir * 5.f)); break;
            case 3: setSettingFloat((m_labelKey+"-y").c_str(), (double)(getSettingFloat((m_labelKey+"-y").c_str()) + dir * 5.f)); break;
            case 4: setSettingInt((m_labelKey+"-r").c_str(), (int64_t)std::clamp(getSettingInt((m_labelKey+"-r").c_str()) + (int)(dir*5), 0, 255)); break;
            case 5: setSettingInt((m_labelKey+"-g").c_str(), (int64_t)std::clamp(getSettingInt((m_labelKey+"-g").c_str()) + (int)(dir*5), 0, 255)); break;
            case 6: setSettingInt((m_labelKey+"-b").c_str(), (int64_t)std::clamp(getSettingInt((m_labelKey+"-b").c_str()) + (int)(dir*5), 0, 255)); break;
        }
        if (m_valueLabels[i]) m_valueLabels[i]->setString(getValueStr(i).c_str());
    }

    void onToggleShow(CCObject* sender) {
        auto* t = static_cast<CCMenuItemToggler*>(sender);
        setSettingBool((m_labelKey + "-show").c_str(), !t->isToggled());
    }
    void onMinusButton(CCObject* s) { stepValue(static_cast<CCNode*>(s)->getTag(), -1.f); }
    void onPlusButton (CCObject* s) { stepValue(static_cast<CCNode*>(s)->getTag(),  1.f); }
    void keyBackClicked() override  { onClose(nullptr); }
    void onClose(CCObject*)         { removeFromParentAndCleanup(true); }

public:
    static LabelSettingsPopup* create(const std::string& key, const std::string& name) {
        auto* p = new LabelSettingsPopup();
        if (p && p->init(key, name)) { p->autorelease(); return p; }
        delete p;
        return nullptr;
    }

    void show() {
        CCDirector::sharedDirector()->getRunningScene()->addChild(this, 1000);
    }
};

class LabelsMenuPopup : public CCLayer {
    CCMenu* m_menu = nullptr;

    static constexpr float PW = 280.f;
    static constexpr float PH = 260.f;

    static const char* rowKey(int i) {
        static const char* keys[] = {
            "fps","cps","attempts","best","runfrom","curpct",
            "time","levelid","song","objects"
        };
        return (i >= 0 && i < 10) ? keys[i] : "";
    }

    static const char* rowName(int i) {
        static const char* names[] = {
            "FPS","CPS","Attempts","Best %","Run From %","Current %",
            "Session Time","Level ID","Song Name","Objects"
        };
        return (i >= 0 && i < 10) ? names[i] : "";
    }

    bool init() {
        if (!CCLayer::init()) return false;

        auto win = CCDirector::sharedDirector()->getWinSize();
        float cx = win.width / 2.f;
        float cy = win.height / 2.f;

        auto* bg = CCScale9Sprite::create("GJ_square02.png");
        bg->setContentSize({PW, PH});
        bg->setPosition({cx, cy});
        addChild(bg, 0);

        auto* title = CCLabelBMFont::create("Labels", "goldFont.fnt");
        title->setScale(0.7f);
        title->setPosition({cx, cy + PH / 2.f - 18.f});
        addChild(title, 1);

        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        addChild(m_menu, 1);

        auto* closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(LabelsMenuPopup::onClose));
        closeBtn->setPosition({cx - PW / 2.f + 14.f, cy + PH / 2.f - 14.f});
        m_menu->addChild(closeBtn);

        float rowHeight   = 26.f;
        float totalHeight = rowHeight * 10.f;
        float listWidth   = PW - 20.f;
        float listHeight  = PH - 46.f;
        float listX       = cx - listWidth / 2.f;
        float listY       = cy - PH / 2.f + 4.f;

        auto* scrollLayer = ScrollLayer::create({listWidth, listHeight});
        scrollLayer->setPosition({listX, listY});
        addChild(scrollLayer, 1);

        auto* contentNode = CCNode::create();
        contentNode->setContentSize({listWidth, totalHeight});

        auto* itemsMenu = CCMenu::create();
        itemsMenu->setPosition({0.f, 0.f});

        for (int i = 0; i < 10; i++) {
            float rowY = totalHeight - rowHeight * i - rowHeight / 2.f;

            auto* nameLabel = CCLabelBMFont::create(rowName(i), "bigFont.fnt");
            nameLabel->setScale(0.38f);
            nameLabel->setAnchorPoint({0.f, 0.5f});
            nameLabel->setPosition({6.f, rowY});
            contentNode->addChild(nameLabel);

            auto* tog = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(LabelsMenuPopup::onToggleLabel), 0.5f);
            tog->toggle(getSettingBool((std::string(rowKey(i)) + "-show").c_str()));
            tog->setTag(i);
            tog->setPosition({listWidth - 42.f, rowY});
            itemsMenu->addChild(tog);

            auto* editBtn = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("Edit", "bigFont.fnt", "GJ_button_04.png", 0.4f),
                this, menu_selector(LabelsMenuPopup::onEditLabel));
            editBtn->setTag(i);
            editBtn->setPosition({listWidth - 16.f, rowY});
            itemsMenu->addChild(editBtn);
        }

        scrollLayer->m_contentLayer->addChild(contentNode);
        scrollLayer->m_contentLayer->addChild(itemsMenu);
        scrollLayer->m_contentLayer->setContentSize({listWidth, totalHeight});
        scrollLayer->m_contentLayer->setPositionY(listHeight - totalHeight);
        scrollLayer->moveToTop();

        setTouchEnabled(true);
        setKeypadEnabled(true);
        return true;
    }

    void onToggleLabel(CCObject* sender) {
        auto* t = static_cast<CCMenuItemToggler*>(sender);
        setSettingBool((std::string(rowKey(t->getTag())) + "-show").c_str(), !t->isToggled());
    }

    void onEditLabel(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        auto* p = LabelSettingsPopup::create(rowKey(i), rowName(i));
        if (p) p->show();
    }

    void keyBackClicked() override { onClose(nullptr); }
    void onClose(CCObject*)        { removeFromParentAndCleanup(true); }

public:
    static LabelsMenuPopup* create() {
        auto* p = new LabelsMenuPopup();
        if (p && p->init()) { p->autorelease(); return p; }
        delete p;
        return nullptr;
    }

    void show() {
        CCDirector::sharedDirector()->getRunningScene()->addChild(this, 1000);
    }
};

class $modify(LabelsPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!PlayLayer::get()) return;

        auto* buttonSprite = ButtonSprite::create("Labels", "goldFont.fnt", "GJ_button_05.png", 0.6f);
        if (!buttonSprite) return;

        auto* labelsButton = CCMenuItemSpriteExtra::create(
            buttonSprite, this, menu_selector(LabelsPauseLayer::onOpenLabels));

        auto* backgroundNode = getChildByID("background");
        CCRect backgroundRect;

        if (backgroundNode) {
            backgroundRect = backgroundNode->boundingBox();
        } else {
            auto windowSize = CCDirector::sharedDirector()->getWinSize();
            backgroundRect  = {windowSize.width * 0.125f, windowSize.height * 0.05f,
                               windowSize.width * 0.75f,  windowSize.height * 0.9f};
        }

        float buttonX = backgroundRect.getMinX() + buttonSprite->getContentWidth()  / 2.f + 14.f;
        float buttonY = backgroundRect.getMaxY() - buttonSprite->getContentHeight() / 2.f - 14.f;

        auto* buttonMenu = CCMenu::create();
        buttonMenu->setPosition({0.f, 0.f});
        labelsButton->setPosition({buttonX, buttonY});
        buttonMenu->addChild(labelsButton);
        addChild(buttonMenu, 10);
    }

    void onOpenLabels(CCObject*) {
        LabelsMenuPopup::create()->show();
    }
};

$on_mod(Loaded) {
    log::info("Labels v1.0.1 loaded");
}
