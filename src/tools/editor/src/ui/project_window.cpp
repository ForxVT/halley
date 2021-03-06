#include "project_window.h"


#include "console_window.h"
#include "ecs_window.h"
#include "editor_settings_window.h"
#include "game_properties_window.h"
#include "taskbar.h"
#include "halley/tools/project/project.h"
#include "halley/file_formats/yaml_convert.h"
#include "src/editor_root_stage.h"
#include "src/halley_editor.h"
#include "src/assets/assets_browser.h"
#include "src/scene/choose_asset_window.h"
#include "src/scene/scene_editor_window.h"
#include "src/ui/editor_ui_factory.h"

using namespace Halley;

ProjectWindow::ProjectWindow(EditorUIFactory& factory, HalleyEditor& editor, Project& project, Resources& resources, const HalleyAPI& api)
	: UIWidget("project_window", {}, UISizer(UISizerType::Vertical))
	, factory(factory)
	, editor(editor)
	, project(project)
	, resources(resources)
	, api(api)
{
	debugConsoleController = std::make_shared<UIDebugConsoleController>();

	project.withDLL([&] (DynamicLibrary& dll)
	{
		dll.addReloadListener(*this);
		hasDLL = dll.isLoaded();
	});
	project.addAssetLoadedListener(this);

	tasks = std::make_unique<TaskSet>();
	tasks->addTask(std::make_unique<CheckAssetsTask>(project, false));

	makeUI();
}

ProjectWindow::~ProjectWindow()
{
	project.withDLL([&] (DynamicLibrary& dll)
	{
		dll.removeReloadListener(*this);
	});
	project.removeAssetLoadedListener(this);
}

void ProjectWindow::onRemovedFromRoot(UIRoot& root)
{
	if (debugConsole) {
		debugConsole->destroy();
	}
	debugConsole.reset();
	debugConsoleController.reset();
	debugConsoleCommands.reset();
}

void ProjectWindow::makeUI()
{
	uiTop = std::make_shared<UIWidget>("uiTop", Vector2f(), UISizer(UISizerType::Horizontal));
	uiMid = std::make_shared<UIWidget>("uiMid", Vector2f(), UISizer(UISizerType::Horizontal));
	uiBottom = std::make_shared<UIWidget>("uiBottom", Vector2f(), UISizer(UISizerType::Horizontal));
	add(uiTop);
	add(uiMid, 1);
	add(uiBottom);

	makeToolbar();
	makePagedPane();
	uiBottom->add(std::make_shared<TaskBar>(factory, *tasks, api), 1);

	setHandle(UIEventType::NavigateTo, [=] (const UIEvent& event)
	{
		auto uri = event.getStringData();
		if (uri.startsWith("asset:")) {
			auto splitURI = uri.split(':');
			if (splitURI.size() == 3) {
				openAsset(fromString<AssetType>(splitURI.at(1)), splitURI.at(2));
			}
		}
	});
}

void ProjectWindow::makeToolbar()
{
	if (toolbar) {
		toolbar->destroy();
		uiTop->clear();
	}
	
	toolbar = std::make_shared<Toolbar>(factory, *this, project);
	
	uiTop->add(toolbar, 1, Vector4f(0, 16, 0, 8));
}

void ProjectWindow::makePagedPane()
{
	if (pagedPane) {
		pagedPane->destroy();
		uiMid->clear();
	}

	assetEditorWindow = std::make_shared<AssetsBrowser>(factory, project, *this);
	consoleWindow = std::make_shared<ConsoleWindow>(factory);
	auto settings = std::make_shared<EditorSettingsWindow>(factory, editor.getPreferences(), project, editor.getProjectLoader(), *this);
	auto properties = std::make_shared<GamePropertiesWindow>(factory, project);
	auto ecs = std::make_shared<ECSWindow>(factory, project);
	
	pagedPane = std::make_shared<UIPagedPane>("pages", numOfStandardTools);
	pagedPane->getPage(static_cast<int>(EditorTabs::Assets))->add(assetEditorWindow, 1, Vector4f(8, 8, 8, 8));
	pagedPane->getPage(static_cast<int>(EditorTabs::ECS))->add(ecs, 1, Vector4f(8, 8, 8, 8));
	pagedPane->getPage(static_cast<int>(EditorTabs::Remotes))->add(consoleWindow, 1, Vector4f(8, 8, 8, 8));
	pagedPane->getPage(static_cast<int>(EditorTabs::Properties))->add(properties, 1, Vector4f(8, 8, 8, 8));
	pagedPane->getPage(static_cast<int>(EditorTabs::Settings))->add(settings, 1, Vector4f(8, 8, 8, 8));

	uiMid->add(pagedPane, 1);
}

void ProjectWindow::tryLoadCustomUI()
{
	if (waitingToLoadCustomUI && hasAssets && hasDLL) {
		if (loadCustomUI()) {
			waitingToLoadCustomUI = false;
		}
	}
}

bool ProjectWindow::loadCustomUI()
{
	destroyCustomUI();

	auto game = project.createGameInstance(api);
	if (!game) {
		return false;
	}
	
	auto customToolsInterface = game->createEditorCustomToolsInterface();
	if (!customToolsInterface) {
		return false;
	}
	
	try {		
		customTools = customToolsInterface->makeTools(IEditorCustomTools::MakeToolArgs(factory, resources, project.getGameResources(), api, project));
	} catch (const std::exception& e) {
		Logger::logException(e);
	} catch (...) {
		return false;
	}

	if (!customTools.empty()) {
		toolbar->getList()->add(std::make_shared<UIImage>(Sprite().setImage(resources, "ui/slant_capsule_short.png").setColour(factory.getColourScheme()->getColour("toolbarNormal"))), 0, Vector4f(0, 3, 0, 3));
		
		for (auto& tool: customTools) {
			const auto img = std::make_shared<UIImage>(tool.icon);
			toolbar->getList()->addImage(tool.id, img, 1, {}, UISizerAlignFlags::Centre);
			toolbar->getList()->getItem(tool.id)->setToolTip(tool.tooltip);
			pagedPane->addPage()->add(tool.widget, 1, Vector4f(8, 8, 8, 8));
		}
	}

	debugConsoleCommands = std::make_shared<UIDebugConsoleCommands>();
	game->attachToEditorDebugConsole(*debugConsoleCommands, project.getGameResources(), project);
	debugConsoleController->addCommands(*debugConsoleCommands);

	return true;
}

void ProjectWindow::destroyCustomUI()
{
	if (!customTools.empty()) {
		makeToolbar();
		customTools.clear();
	}
	pagedPane->resizePages(numOfStandardTools);
	debugConsoleCommands.reset();
}

void ProjectWindow::onLoadDLL()
{
	hasDLL = true;
	waitingToLoadCustomUI = true;
	tryLoadCustomUI();
}

void ProjectWindow::onUnloadDLL()
{
	destroyCustomUI();
	for (const auto& ss: resources.enumerate<SpriteSheet>()) {
		resources.get<SpriteSheet>(ss)->clearMaterialCache();
	}
	for (const auto& ss: project.getGameResources().enumerate<SpriteSheet>()) {
		project.getGameResources().get<SpriteSheet>(ss)->clearMaterialCache();
	}
}

void ProjectWindow::onAssetsLoaded()
{
	hasAssets = true;
	tryLoadCustomUI();
	for (auto& c: customTools) {
		c.widget->sendEvent(UIEvent(UIEventType::AssetsReloaded, getId()));
	}
}

void ProjectWindow::update(Time t, bool moved)
{
	if (tasks) {
		tasks->update(t);
	}

	const auto size = api.video->getWindow().getDefinition().getSize();
	setMinSize(Vector2f(size));
}

bool ProjectWindow::onKeyPress(KeyboardKeyPress key)
{
	if (key.is(KeyCode::P, KeyMods::Ctrl)) {
		openAssetFinder();
		return true;
	}

	if (key.is(KeyCode::F1)) {
		toggleDebugConsole();
		return true;
	}
	
	return false;
}

void ProjectWindow::setPage(EditorTabs tab)
{
	pagedPane->setPage(static_cast<int>(tab));
}

LocalisedString ProjectWindow::setCustomPage(const String& pageId)
{
	int i = 0;
	for (auto& custom: customTools) {
		if (custom.id == pageId) {
			pagedPane->setPage(numOfStandardTools + i);
			return custom.text;
		}
		++i;
	}
	return LocalisedString::fromHardcodedString("???");
}

void ProjectWindow::openFile(const String& assetId)
{
	toolbar->getList()->setSelectedOptionId(toString(EditorTabs::Assets));
	assetEditorWindow->openFile(assetId);
}

void ProjectWindow::openAsset(AssetType type, const String& assetId)
{
	toolbar->getList()->setSelectedOptionId(toString(EditorTabs::Assets));
	assetEditorWindow->openAsset(type, assetId);
}

const HalleyAPI& ProjectWindow::getAPI() const
{
	return api;
}

TaskSet& ProjectWindow::getTasks() const
{
	return *tasks;
}

void ProjectWindow::openAssetFinder()
{
	if (!assetFinder) {
		assetFinder = std::make_shared<ChooseImportAssetWindow>(factory, project, [=] (std::optional<String> result)
		{
			if (result) {
				openFile(result.value());
			}
			assetFinder.reset();
		});
		getRoot()->addChild(assetFinder);
	}
}

void ProjectWindow::toggleDebugConsole()
{
	if (debugConsole && debugConsole->isActive()) {
		debugConsole->hide();
	} else {
		if (!debugConsole) {
			debugConsole = std::make_shared<UIDebugConsole>("debugConsole", factory, debugConsoleController);
			debugConsole->setChildLayerAdjustment(50);
			debugConsole->setMinSize(Vector2f(640, 320));
			debugConsole->setAnchor(UIAnchor(Vector2f(1.0f, 1.0f), Vector2f(1.0f, 1.0f), Vector2f(-10.0f, -10.0f)));
			getRoot()->addChild(debugConsole);
		}
		debugConsole->show();
	}
}

void ProjectWindow::reloadProject()
{
	destroy();
}

void ProjectWindow::addTask(std::unique_ptr<Task> task)
{
	tasks->addTask(std::move(task));
}

ConfigNode ProjectWindow::getSetting(EditorSettingType type, std::string_view id) const
{
	// TODO: handle different types
	const auto iter = tempSettings.find(id);
	if (iter != tempSettings.end()) {
		return ConfigNode(iter->second);
	}
	return ConfigNode();
}

void ProjectWindow::setSetting(EditorSettingType type, std::string_view id, ConfigNode data)
{
	// TODO: handle different types
	tempSettings[id] = std::move(data);
}
