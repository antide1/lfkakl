// Click Frames - shows how many frames each click is held for.
//
// Inspired by the idea of frame-window counters (like FB Counter), but written
// from scratch: it does not record macros and does not talk to any server.
// Every click is measured live, in update steps of the running level.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

// Tag used to find our marker circles inside the level's object layer.
constexpr int kMarkerTag = 0x434C4B46;

struct Config {
    bool enabled = true;
    std::string ranges = "1,2,3,4,5-6,7-8,9-12,13-16,17+";
    std::string colors = "1:fe4f50,2:ffb355,3:fdfe78,4:fefefe,5:8bfe8b,7:8dc7fe,9:7877fe,13:b586fc,17:fc8bff";
    bool markers = true;
    bool lastClick = true;
    int hudX = 2;
    int hudY = 98;
    double hudScale = 0.6;
    double markerTextScale = 0.45;
    double markerFade = 0.0;
};

struct Row {
    std::string name;
    int lo = 1;
    int hi = -1;  // -1 means "and up"
    ccColor3B color = ccc3(255, 255, 255);
    int count = 0;
    CCLabelBMFont* label = nullptr;
    float scale = 1.f;
};

struct State {
    int frame = 0;
    int round = 0;
    std::vector<Row> rows;
    CCNode* hud = nullptr;
    CCLabelBMFont* lastLabel = nullptr;
    std::unordered_map<PlayerObject*, int> pressFrame;
};

Config g_cfg;
State g_state;

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::vector<std::string> split(std::string const& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

int toInt(std::string const& text, int fallback) {
    auto value = trim(text);
    if (value.empty()) return fallback;

    bool negative = false;
    size_t index = 0;
    if (value[0] == '-') {
        negative = true;
        index = 1;
    }
    if (index >= value.size()) return fallback;

    int result = 0;
    for (; index < value.size(); ++index) {
        auto c = static_cast<unsigned char>(value[index]);
        if (!std::isdigit(c)) return fallback;
        result = result * 10 + (value[index] - '0');
        if (result > 100000) return fallback;
    }
    return negative ? -result : result;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ccColor3B parseColor(std::string text) {
    text = trim(text);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 6) return ccc3(255, 255, 255);

    int digits[6];
    for (int i = 0; i < 6; ++i) {
        digits[i] = hexDigit(text[i]);
        if (digits[i] < 0) return ccc3(255, 255, 255);
    }
    return ccc3(
        static_cast<GLubyte>(digits[0] * 16 + digits[1]),
        static_cast<GLubyte>(digits[2] * 16 + digits[3]),
        static_cast<GLubyte>(digits[4] * 16 + digits[5])
    );
}

void loadConfig() {
    auto mod = Mod::get();
    g_cfg.enabled = mod->getSettingValue<bool>("enabled");
    g_cfg.ranges = mod->getSettingValue<std::string>("counter-ranges");
    g_cfg.colors = mod->getSettingValue<std::string>("counter-colors");
    g_cfg.markers = mod->getSettingValue<bool>("show-markers");
    g_cfg.lastClick = mod->getSettingValue<bool>("last-click");
    g_cfg.hudX = static_cast<int>(mod->getSettingValue<int64_t>("hud-x"));
    g_cfg.hudY = static_cast<int>(mod->getSettingValue<int64_t>("hud-y"));
    g_cfg.hudScale = mod->getSettingValue<double>("hud-scale");
    g_cfg.markerTextScale = mod->getSettingValue<double>("marker-text-scale");
    g_cfg.markerFade = mod->getSettingValue<double>("marker-fade");

    if (g_cfg.hudScale <= 0.0) g_cfg.hudScale = 0.6;
    if (g_cfg.markerTextScale <= 0.0) g_cfg.markerTextScale = 0.45;
}

// ---------------------------------------------------------------------------
// counter rows
// ---------------------------------------------------------------------------

void rebuildRows() {
    g_state.rows.clear();

    std::vector<std::pair<int, ccColor3B>> palette;
    for (auto const& part : split(g_cfg.colors, ',')) {
        auto item = trim(part);
        if (item.empty()) continue;
        auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        auto key = toInt(item.substr(0, colon), 1);
        palette.emplace_back(key, parseColor(item.substr(colon + 1)));
    }
    std::sort(palette.begin(), palette.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    auto colorFor = [&palette](int frames) {
        ccColor3B result = ccc3(255, 255, 255);
        for (auto const& entry : palette) {
            if (entry.first <= frames) result = entry.second;
        }
        return result;
    };

    for (auto const& raw : split(g_cfg.ranges, ',')) {
        auto token = trim(raw);
        if (token.empty()) continue;

        Row row;
        row.name = token;
        if (token.back() == '+') {
            row.lo = toInt(token.substr(0, token.size() - 1), 1);
            row.hi = -1;
        } else {
            auto dash = token.find('-');
            if (dash == std::string::npos) {
                row.lo = row.hi = toInt(token, 1);
            } else {
                row.lo = toInt(token.substr(0, dash), 1);
                row.hi = toInt(token.substr(dash + 1), row.lo);
            }
        }
        if (row.lo < 1) row.lo = 1;
        if (row.hi >= 0 && row.hi < row.lo) row.hi = row.lo;

        row.color = colorFor(row.lo);
        g_state.rows.push_back(std::move(row));
    }

    std::sort(g_state.rows.begin(), g_state.rows.end(),
        [](Row const& a, Row const& b) { return a.lo < b.lo; });
}

Row* rowFor(int frames) {
    for (auto& row : g_state.rows) {
        if (frames >= row.lo && (row.hi < 0 || frames <= row.hi)) {
            return &row;
        }
    }
    return nullptr;
}

std::string rowText(Row const& row, int count) {
    return row.name + "   " + std::to_string(count);
}

// ---------------------------------------------------------------------------
// markers in the level
// ---------------------------------------------------------------------------

class MarkerNode : public cocos2d::CCNode {
public:
    static MarkerNode* create() {
        auto node = new MarkerNode();
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        delete node;
        return nullptr;
    }

    void onFadeDone() {
        this->removeFromParentAndCleanup(true);
    }
};

void addMarker(int frames, PlayerObject* player, ccColor3B color) {
    auto layer = PlayLayer::get();
    if (!layer || !layer->m_objectLayer || !player) return;

    auto parent = player->getParent();
    if (!parent) return;

    auto worldPos = parent->convertToWorldSpace(player->getPosition());
    auto localPos = layer->m_objectLayer->convertToNodeSpace(worldPos);

    auto node = MarkerNode::create();
    if (!node) return;

    node->setPosition(localPos);
    node->setTag(kMarkerTag);

    constexpr float radius = 13.f;

    auto dot = CCDrawNode::create();
    if (dot) {
        dot->drawDot(ccp(0.f, 0.f), radius + 2.f, ccc4f(0.f, 0.f, 0.f, 0.7f));
        dot->drawDot(
            ccp(0.f, 0.f),
            radius,
            ccc4f(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f)
        );
        node->addChild(dot);
    }

    auto label = CCLabelBMFont::create(std::to_string(frames).c_str(), "bigFont.fnt");
    if (label) {
        label->setPosition(ccp(0.f, radius + 10.f));
        label->setScale(static_cast<float>(g_cfg.markerTextScale));
        label->setColor(ccc3(255, 255, 255));
        node->addChild(label);
    }

    if (g_cfg.markerFade > 0.0) {
        node->runAction(CCSequence::create(
            CCDelayTime::create(static_cast<float>(g_cfg.markerFade)),
            CCFadeOut::create(0.35f),
            CCCallFunc::create(node, callfunc_selector(MarkerNode::onFadeDone)),
            nullptr
        ));
    }

    layer->m_objectLayer->addChild(node, 1000);
}

void clearMarkers() {
    auto layer = PlayLayer::get();
    if (!layer || !layer->m_objectLayer) return;

    auto children = layer->m_objectLayer->getChildren();
    if (!children) return;

    std::vector<CCNode*> doomed;
    for (unsigned int i = 0; i < children->count(); ++i) {
        auto node = static_cast<CCNode*>(children->objectAtIndex(i));
        if (node && node->getTag() == kMarkerTag) {
            doomed.push_back(node);
        }
    }
    for (auto node : doomed) {
        node->removeFromParentAndCleanup(true);
    }
}

// ---------------------------------------------------------------------------
// hud
// ---------------------------------------------------------------------------

void forgetHud() {
    // Do not touch the old nodes here: the layer that owned them may already
    // be gone. We only drop our pointers and let cocos2d clean up its own tree.
    g_state.hud = nullptr;
    g_state.lastLabel = nullptr;
    g_state.rows.clear();
}

void buildHud(PlayLayer* layer) {
    forgetHud();
    if (!g_cfg.enabled || !layer) return;

    rebuildRows();

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto root = CCNode::create();
    if (!root) return;

    root->setPosition(ccp(
        winSize.width * static_cast<float>(g_cfg.hudX) / 100.f,
        winSize.height * static_cast<float>(g_cfg.hudY) / 100.f
    ));
    layer->addChild(root, 1000);
    g_state.hud = root;

    float scale = static_cast<float>(g_cfg.hudScale);
    float line = 26.f * scale;
    float y = 0.f;

    for (auto& row : g_state.rows) {
        auto label = CCLabelBMFont::create(rowText(row, 0).c_str(), "bigFont.fnt");
        if (!label) continue;

        label->setAnchorPoint(ccp(0.f, 1.f));
        label->setScale(scale);
        label->setPosition(ccp(0.f, y));
        label->setColor(row.color);
        root->addChild(label);

        row.label = label;
        row.scale = scale;
        y -= line;
    }

    if (g_cfg.lastClick) {
        auto label = CCLabelBMFont::create("last: --", "bigFont.fnt");
        if (label) {
            label->setAnchorPoint(ccp(0.f, 1.f));
            label->setScale(scale * 0.75f);
            label->setPosition(ccp(0.f, y - 8.f * scale));
            label->setColor(ccc3(225, 225, 225));
            root->addChild(label);
            g_state.lastLabel = label;
        }
    }
}

void bumpRow(Row& row) {
    ++row.count;
    if (!row.label) return;

    row.label->setString(rowText(row, row.count).c_str());

    auto scale = row.scale;
    row.label->stopAllActions();
    row.label->setScale(scale);
    row.label->runAction(CCSequence::create(
        CCScaleTo::create(0.06f, scale * 1.35f),
        CCScaleTo::create(0.14f, scale),
        nullptr
    ));
}

void addClick(int frames, PlayerObject* player) {
    ccColor3B color = ccc3(255, 255, 255);

    if (auto row = rowFor(frames)) {
        color = row->color;
        bumpRow(*row);
    }

    if (g_state.lastLabel) {
        auto text = "last click: " + std::to_string(frames) + " frame" + (frames == 1 ? "" : "s");
        g_state.lastLabel->setString(text.c_str());
    }

    if (g_cfg.markers) {
        addMarker(frames, player, color);
    }
}

void resetSession() {
    g_state.frame = 0;
    ++g_state.round;
    g_state.pressFrame.clear();

    for (auto& row : g_state.rows) {
        row.count = 0;
        if (row.label) row.label->setString(rowText(row, 0).c_str());
    }
    if (g_state.lastLabel) {
        g_state.lastLabel->setString("last: --");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// hooks
// ---------------------------------------------------------------------------

class $modify(ClickFramesBaseLayer, GJBaseGameLayer) {
    void update(float dt) {
        if (g_cfg.enabled) {
            ++g_state.frame;
        }
        GJBaseGameLayer::update(dt);
    }
};

class $modify(ClickFramesPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        loadConfig();
        buildHud(this);
        resetSession();
        return true;
    }

    void resetLevel() {
        resetSession();
        clearMarkers();
        PlayLayer::resetLevel();
    }

    void onExit() {
        PlayLayer::onExit();

        // The layer (and everything below it) is going away, drop the pointers.
        forgetHud();
        g_state.pressFrame.clear();
    }
};

class $modify(ClickFramesPlayer, PlayerObject) {
    bool pushButton(PlayerButton button) {
        bool pressed = PlayerObject::pushButton(button);

        if (button == PlayerButton::Jump && g_cfg.enabled) {
            g_state.pressFrame[this] = g_state.frame;
        }
        return pressed;
    }

    bool releaseButton(PlayerButton button) {
        bool released = PlayerObject::releaseButton(button);

        if (button == PlayerButton::Jump && g_cfg.enabled) {
            auto found = g_state.pressFrame.find(this);
            if (found != g_state.pressFrame.end()) {
                int frames = g_state.frame - found->second;
                if (frames < 1) frames = 1;
                g_state.pressFrame.erase(found);
                addClick(frames, this);
            }
        }
        return released;
    }
};
