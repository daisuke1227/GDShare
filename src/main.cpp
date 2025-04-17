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

template <class L>
static Task<Result<std::filesystem::path>> promptExportLevel(L* level) {
    auto opts = IMPORT_PICK_OPTIONS;
    if constexpr (std::is_same_v<L, GJLevelList>) {
        opts.defaultPath = std::filesystem::path(std::string(level->m_listName) + ".gmdl");
    } else {
        opts.defaultPath = std::filesystem::path(std::string(level->m_levelName) + ".gmd");
    }
    return file::pick(file::PickMode::SaveFile, opts);
}

template <class L>
static void onExportFilePick(L* level, typename Task<Result<std::filesystem::path>>::Event* event) {
    if (auto result = event->getValue(); result && result->isOk()) {
        auto path = result->unwrap();
        std::optional<std::string> err;
        if constexpr (std::is_same_v<L, GJLevelList>) {
            err = exportListAsGmd(level, path).err();
        } else {
            err = exportLevelAsGmd(level, path).err();
        }
        if (err) {
            FLAlertLayer::create("Error", "Unable to export: " + *err, "OK")->show();
            return;
        }

#if GEODE_IOS
        auto director = CCDirector::sharedDirector();
        auto view = director->getOpenGLView();
        auto uiView = view->getEAGLView();
        auto nsPath = __String::create(path.string().c_str())->getCString();
        NSURL* fileURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:nsPath]];
        UIDocumentInteractionController* ctrl =
            [UIDocumentInteractionController interactionControllerWithURL:fileURL];
        ctrl.delegate = (id<UIDocumentInteractionControllerDelegate>)view;
        CGRect frame = CGRectMake(0, 0, uiView.bounds.size.width, uiView.bounds.size.height);
        [ctrl presentOptionsMenuFromRect:frame inView:uiView animated:YES];
#elif GEODE_ANDROID
        {
            cocos2d::JniMethodInfo methodInfo;
            if (cocos2d::JniHelper::getStaticMethodInfo(methodInfo,
                "org/cocos2dx/lib/Cocos2dxActivity",
                "shareGmdFile",
                "(Ljava/lang/String;)V"))
            {
                jstring jPath = methodInfo.env->NewStringUTF(path.string().c_str());
                methodInfo.env->CallStaticVoidMethod(methodInfo.classID, methodInfo.methodID, jPath);
                methodInfo.env->DeleteLocalRef(jPath);
                methodInfo.env->DeleteLocalRef(methodInfo.classID);
            }
        }
#else
        createQuickPopup(
            "Exported",
            "Successfully exported",
            "OK", "Open Folder",
            [path](auto, bool openIt) {
                if (openIt) file::openFolder(path);
            }
        );
#endif
    } else if (result) {
        FLAlertLayer::create("Error Exporting", result->unwrapErr(), "OK")->show();
    }
}

struct $modify(ExportMyLevelLayer, EditLevelLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };

    $override
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        if (auto menu = this->getChildByID("level-actions-menu")) {
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
        m_fields->pickListener.bind([level = m_level](auto* ev) { onExportFilePick(level, ev); });
        m_fields->pickListener.setFilter(promptExportLevel(m_level));
    }
};

struct $modify(ExportOnlineLevelLayer, LevelInfoLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };

    $override
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (auto menu = this->getChildByID("left-side-menu")) {
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
        m_fields->pickListener.bind([level = m_level](auto* ev) { onExportFilePick(level, ev); });
        m_fields->pickListener.setFilter(promptExportLevel(m_level));
    }
};

struct $modify(ImportLayer, LevelBrowserLayer) {
    struct Fields { EventListener<Task<Result<std::vector<std::filesystem::path>>>> pickListener; };

    static void importFiles(const std::vector<std::filesystem::path>& paths) {
        for (auto const& path : paths) {
            switch (getGmdFileKind(path)) {
                case GmdFileKind::List: {
                    auto res = importGmdAsList(path);
                    if (res) LocalLevelManager::get()->m_localLists->insertObject(*res, 0);
                    else { FLAlertLayer::create("Error Importing", res.unwrapErr(), "OK")->show(); return; }
                } break;
                case GmdFileKind::Level: {
                    auto res = importGmdAsLevel(path);
                    if (res) LocalLevelManager::get()->m_localLevels->insertObject(*res, 0);
                    else { FLAlertLayer::create("Error Importing", res.unwrapErr(), "OK")->show(); return; }
                } break;
                case GmdFileKind::None:
                    FLAlertLayer::create("Error Importing", fmt::format("Selected file '<cp>{}</c>' is not a GMD file!", path.string()), "OK")->show();
                    return;
            }
        }
        auto scene = CCScene::create();
        auto layer = LevelBrowserLayer::create(GJSearchObject::create(SearchType::MyLevels));
        scene->addChild(layer);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(.5f, scene));
    }

    void onImport(CCObject*) {
        m_fields->pickListener.bind([](auto* ev) {
            if (auto result = ev->getValue(); result && result->isOk()) importFiles(result->unwrap());
            else if (result) FLAlertLayer::create("Error Importing", result->unwrapErr(), "OK")->show();
        });
        m_fields->pickListener.setFilter(file::pickMany(IMPORT_PICK_OPTIONS));
    }

    $override
    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search)) return false;
        if (search->m_searchType == SearchType::MyLevels || search->m_searchType == SearchType::MyLists) {
            auto btnMenu = this->getChildByID("new-level-menu");
            auto importBtn = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .85f,
                    CircleBaseColor::Pink, CircleBaseSize::Big),
                this, menu_selector(ImportLayer::onImport)
            );
            importBtn->setID("import-level-button"_spr);
            if (search->m_searchType == SearchType::MyLists && search->m_searchIsOverlay)
                btnMenu->addChildAtPosition(importBtn, Anchor::BottomLeft, ccp(0, 60), false);
            else { btnMenu->addChild(importBtn); btnMenu->updateLayout(); }
        }
        return true;
    }
};

struct $modify(ExportListLayer, LevelListLayer) {
    struct Fields { EventListener<Task<Result<std::filesystem::path>>> pickListener; };

    $override
    bool init(GJLevelList* level) {
        if (!LevelListLayer::init(level)) return false;
        if (auto menu = this->getChildByID("left-side-menu")) {
            auto btn = CCMenuItemSpriteExtra::create(
                CircleButtonSprite::createWithSpriteFrameName("file.png"_spr, .8f,
                    CircleBaseColor::Green, CircleBaseSize::Medium),
                this, menu_selector(ExportListLayer::onExport)
            );
            btn->setID("export-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }

    void onExport(CCObject*) {
        m_fields->pickListener.bind([list = m_levelList](auto* ev) { onExportFilePick(list, ev); });
        m_fields->pickListener.setFilter(promptExportLevel(m_levelList));
    }
};