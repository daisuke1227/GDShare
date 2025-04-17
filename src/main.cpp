#include <Geode/Loader.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/IDManager.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <GMD.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <vector>
#include <optional>
#include <type_traits>
#include <string>

#if GEODE_IOS
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#endif

#if GEODE_ANDROID
#include "platform/android/jni/JniHelper.h"
#include <jni.h>
#endif

using namespace geode::prelude;
using namespace gmd;

static const auto IMPORT_PICK_OPTIONS = file::FilePickOptions{
    std::nullopt,
    {{"GD Level Files", {"*.gmd", "*.gmdl"}}}
};

// Helper to get app writable directory + filename
static std::filesystem::path getOriginalPath(const std::string& name) {
#if GEODE_IOS || GEODE_ANDROID
    std::string dir = cocos2d::FileUtils::getInstance()->getWritablePath();
#else
    auto dir = std::filesystem::current_path().string();
#endif
    return std::filesystem::path(dir) / name;
}

// Prompt user for export filename
template <class L>
static Task<Result<std::filesystem::path>> promptExportLevel(L* level) {
    auto opts = IMPORT_PICK_OPTIONS;
    std::filesystem::path base;
    if constexpr (std::is_same_v<L, GJLevelList>) {
        base = std::string(level->m_listName) + ".gmdl";
    } else {
        base = std::string(level->m_levelName) + ".gmd";
    }
    opts.defaultPath = base;
    return file::pick(file::PickMode::SaveFile, opts);
}

// Handle actual export + copy + share/open UI
template <class L>
static void onExportFilePick(L* level, typename Task<Result<std::filesystem::path>>::Event* event) {
    if (auto result = event->getValue(); result && result->isOk()) {
        auto userPath = result->unwrap();
        std::string filename = userPath.filename().string();
        auto originalPath = getOriginalPath(filename);
        std::optional<std::string> err;
        if constexpr (std::is_same_v<L, GJLevelList>)
            err = exportListAsGmd(level, originalPath).err();
        else
            err = exportLevelAsGmd(level, originalPath).err();
        if (err) {
            FLAlertLayer::create("Error", "Unable to export original: " + *err, "OK")->show();
            return;
        }
        try {
            std::filesystem::copy_file(
                originalPath,
                userPath,
                std::filesystem::copy_options::overwrite_existing
            );
        } catch (std::exception& e) {
            FLAlertLayer::create("Error", "Copy failed: " + std::string(e.what()), "OK")->show();
            return;
        }
#if GEODE_IOS
        auto director = CCDirector::sharedDirector();
        auto view = director->getOpenGLView();
        auto uiView = view->getEAGLView();
        auto ns = __String::create(userPath.string().c_str())->getCString();
        NSURL* fileURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:ns]];
        UIDocumentInteractionController* ctrl =
            [UIDocumentInteractionController interactionControllerWithURL:fileURL];
        ctrl.delegate = (id<UIDocumentInteractionControllerDelegate>)view;
        CGRect frame = CGRectMake(0, 0, uiView.bounds.size.width, uiView.bounds.size.height);
        [ctrl presentOptionsMenuFromRect:frame inView:uiView animated:YES];
#elif GEODE_ANDROID
        cocos2d::JniMethodInfo info;
        if (JniHelper::getStaticMethodInfo(
            info,
            "org/cocos2dx/lib/Cocos2dxActivity",
            "shareGmdFile",
            "(Ljava/lang/String;)V"
        )) {
            jstring jPath = info.env->NewStringUTF(userPath.string().c_str());
            info.env->CallStaticVoidMethod(info.classID, info.methodID, jPath);
            info.env->DeleteLocalRef(jPath);
            info.env->DeleteLocalRef(info.classID);
        }
#else
        createQuickPopup(
            "Exported",
            "Saved to: " + userPath.string(),
            "OK", "Open Folder",
            [userPath](auto, bool openIt) {
                if (openIt) file::openFolder(userPath);
            }
        );
#endif
    } else if (result) {
        FLAlertLayer::create("Error Exporting", result->unwrapErr(), "OK")->show();
    }
}

// Modify layers to add Export button
struct $modify(ExportMyLevelLayer, EditLevelLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };
    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (auto menu = getChildByID("level-actions-menu")) {
            auto btn = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .8f,
                    CircleBaseColor::Green, CircleBaseSize::MediumAlt),
                this, menu_selector(ExportMyLevelLayer::onExport)
            );
            btn->setID("export-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }
    void onExport(CCObject*) {
        m_fields->pickListener.bind([lvl = m_level](auto* e) { onExportFilePick(lvl, e); });
        m_fields->pickListener.setFilter(promptExportLevel(m_level));
    }
};

struct $modify(ExportOnlineLevelLayer, LevelInfoLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };
    $override
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (auto menu = getChildByID("left-side-menu")) {
            auto btn = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .8f,
                    CircleBaseColor::Green, CircleBaseSize::Medium),
                this, menu_selector(ExportOnlineLevelLayer::onExport)
            );
            btn->setID("export-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }
    void onExport(CCObject*) {
        m_fields->pickListener.bind([lvl = m_level](auto* e) { onExportFilePick(lvl, e); });
        m_fields->pickListener.setFilter(promptExportLevel(m_level));
    }
};

struct $modify(ImportLayer, LevelBrowserLayer) {
    struct Fields { EventListener<Task<Result<std::vector<std::filesystem::path>>>> pickListener; };
    static void importFiles(const std::vector<std::filesystem::path>& paths) {
        for (auto& f : paths) {
            switch (getGmdFileKind(f)) {
                case GmdFileKind::List: {
                    auto r = importGmdAsList(f);
                    if (r) LocalLevelManager::get()->m_localLists->insertObject(*r, 0);
                    else { FLAlertLayer::create("Error Importing", r.unwrapErr(), "OK")->show(); return; }
                } break;
                case GmdFileKind::Level: {
                    auto r = importGmdAsLevel(f);
                    if (r) LocalLevelManager::get()->m_localLevels->insertObject(*r, 0);
                    else { FLAlertLayer::create("Error Importing", r.unwrapErr(), "OK")->show(); return; }
                } break;
                case GmdFileKind::None:
                    FLAlertLayer::create("Error Importing", fmt::format("Selected '<cp>{}</c>' not GMD!", f.string()), "OK")->show();
                    return;
            }
        }
        auto sc = CCScene::create();
        auto ly = LevelBrowserLayer::create(GJSearchObject::create(SearchType::MyLevels));
        sc->addChild(ly);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(.5f, sc));
    }
    void onImport(CCObject*) {
        m_fields->pickListener.bind([](auto* e) {
            if (auto r = e->getValue(); r && r->isOk()) importFiles(r->unwrap());
            else if (r) FLAlertLayer::create("Error Importing", r->unwrapErr(), "OK")->show();
        });
        m_fields->pickListener.setFilter(file::pickMany(IMPORT_PICK_OPTIONS));
    }
    $override
    bool init(GJSearchObject* s) {
        if (!LevelBrowserLayer::init(s)) return false;
        if (s->m_searchType == SearchType::MyLevels || s->m_searchType == SearchType::MyLists) {
            auto m = getChildByID("new-level-menu");
            auto ib = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .85f,
                    CircleBaseColor::Pink, CircleBaseSize::Big),
                this, menu_selector(ImportLayer::onImport)
            );
            ib->setID("import-level-button"_spr);
            if (s->m_searchType == SearchType::MyLists && s->m_searchIsOverlay) m->addChildAtPosition(ib, Anchor::BottomLeft, ccp(0, 60), false);
            else { m->addChild(ib); m->updateLayout(); }
        }
        return true;
    }
};

struct $modify(ExportListLayer, LevelListLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };
    $override
    bool init(GJLevelList* l) {
        if (!LevelListLayer::init(l)) return false;
        if (auto m = getChildByID("left-side-menu")) {
            auto b = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .8f,
                    CircleBaseColor::Green, CircleBaseSize::Medium),
                this, menu_selector(ExportListLayer::onExport)
            );
            b->setID("export-button"_spr);
            m->addChild(b);
            m->updateLayout();
        }
        return true;
    }
    void onExport(CCObject*) {
        m_fields->pickListener.bind([lst = m_levelList](auto* e) { onExportFilePick(lst, e); });
        m_fields->pickListener.setFilter(promptExportLevel(m_levelList));
    }
};