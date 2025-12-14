/*
 * HD_UI Screen Classes Implementation
 */

#include "hd_screens.h"
#include "hd_allegro5.h"
#include "hd_atlas.h"
#include <cstdio>


namespace HDUI {

// ============================================================================
// HDScreen base class
// ============================================================================

HDScreen::HDScreen()
    : visible(false)
{
}

HDScreen::~HDScreen() {
}

void HDScreen::Create() {
    if (!layoutName.empty()) {
        LoadLayout(layoutName);
    }
    visible = true;
}

void HDScreen::Remove() {
    layout.reset();
    visible = false;
}

void HDScreen::Update(float dt) {
    if (layout) {
        layout->Update(dt);
    }
}

void HDScreen::Draw() {
    if (layout && visible) {
        layout->Draw();
    }
}

bool HDScreen::HandleInput() {
    // TODO: Handle mouse/keyboard input for UI elements
    return false;
}

bool HDScreen::LoadLayout(const std::string& name) {
    LayoutManager& mgr = LayoutManager::GetInstance();
    UIObject* obj = mgr.GetLayout(name);
    if (!obj) {
        fprintf(stderr, "HD_UI: Failed to load layout: %s\n", name.c_str());
        return false;
    }
    // Note: LayoutManager owns the layout, we just get a pointer
    // For screens we want our own copy - actually we should improve this
    // For now just reference it
    printf("HD_UI: Loaded layout: %s\n", name.c_str());
    return true;
}

UIObject* HDScreen::FindElement(const std::string& path) {
    if (layout) {
        return layout->FindChildRecursive(path);
    }
    return nullptr;
}

// ============================================================================
// HD_MainMenu
// ============================================================================

HD_MainMenu::HD_MainMenu() {
    layoutName = "MainMenu/MainMenu";
}

HD_MainMenu::~HD_MainMenu() {
}

void HD_MainMenu::Create() {
    HDScreen::Create();
    SetupCallbacks();
    
    // Set version text
    SetVersion("Uplink HD v1.0");
}

void HD_MainMenu::Remove() {
    HDScreen::Remove();
}

void HD_MainMenu::Update(float dt) {
    HDScreen::Update(dt);
}

void HD_MainMenu::Draw() {
    // DEBUG: yellow rect to verify we reach here
    Allegro5System::DrawRectFilled(200, 200, 200, 200, 1.0f, 1.0f, 0.0f, 1.0f);
    if (!visible) return;
    
    // Draw dark gradient background
    Allegro5System::Clear(0.043f, 0.043f, 0.043f, 1.0f); // #0B0B0B
    
    // Draw vertical gradient from dark to teal
    Allegro5System::DrawGradientVertical(0, 0, 
        (float)Allegro5System::GetWidth(), 
        (float)Allegro5System::GetHeight(),
        0.043f, 0.043f, 0.043f, 1.0f,    // Top: #0B0B0B
        0.067f, 0.169f, 0.235f, 1.0f);   // Bottom: #112B3C
    
    // Draw layout elements
    HDScreen::Draw();
}

bool HD_MainMenu::HandleInput() {
    return HDScreen::HandleInput();
}

void HD_MainMenu::SetupCallbacks() {
    // Set up button callbacks
    UIObject* btnShutdown = FindElement("btnShutdown");
    if (btnShutdown) {
        btnShutdown->onClickCallback = [this](UIObject*) {
            OnShutdownClick();
        };
    }
    
    UIObject* btnOptions = FindElement("btnOptions");
    if (btnOptions) {
        btnOptions->onClickCallback = [this](UIObject*) {
            OnOptionsClick();
        };
    }
    
    UIObject* btnSubmit = FindElement("signIn/btnSubmit");
    if (btnSubmit) {
        btnSubmit->onClickCallback = [this](UIObject*) {
            OnPasswordSubmit();
        };
    }
}

void HD_MainMenu::OnShutdownClick() {
    printf("HD_UI: Shutdown clicked\n");
    // TODO: Trigger game exit
}

void HD_MainMenu::OnOptionsClick() {
    printf("HD_UI: Options clicked\n");
    // TODO: Open options menu
}

void HD_MainMenu::OnPasswordSubmit() {
    printf("HD_UI: Password submit clicked\n");
    // TODO: Verify password and proceed to game
}

void HD_MainMenu::SetUsername(const std::string& name) {
    username = name;
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("signIn/txtUsername"));
    if (txt) {
        txt->text = name;
    }
}

void HD_MainMenu::SetLastSignIn(const std::string& date) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("signIn/txtLastSignIn"));
    if (txt) {
        txt->text = "Last Sign In: " + date;
    }
}

void HD_MainMenu::SetVersion(const std::string& version) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("signIn/txtVersion"));
    if (txt) {
        txt->text = version;
    }
}

void HD_MainMenu::ShowError(const std::string& message) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("signIn/txtNotice"));
    if (txt) {
        txt->text = message;
        txt->visible = true;
    }
}

void HD_MainMenu::HideError() {
    UIObject* txt = FindElement("signIn/txtNotice");
    if (txt) {
        txt->visible = false;
    }
}

// ============================================================================
// HD_TopBar
// ============================================================================

HD_TopBar::HD_TopBar()
    : currentSpeed(1)
{
    layoutName = "HUD/TopBar";
}

HD_TopBar::~HD_TopBar() {
}

void HD_TopBar::Create() {
    HDScreen::Create();
    SetupCallbacks();
}

void HD_TopBar::Update(float dt) {
    HDScreen::Update(dt);
}

void HD_TopBar::SetupCallbacks() {
    // Speed buttons
    for (int i = 0; i <= 3; i++) {
        char btnName[32];
        snprintf(btnName, sizeof(btnName), "btnSpeed%d", i);
        UIObject* btn = FindElement(btnName);
        if (btn) {
            int speed = i;
            btn->onClickCallback = [this, speed](UIObject*) {
                SetSpeed(speed);
            };
        }
    }
}

void HD_TopBar::SetDate(int day, int month, int year) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("txtDay"));
    if (txt) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d-%d-%d", day, month, year);
        txt->text = buf;
    }
}

void HD_TopBar::SetTime(int hour, int minute) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("txtTime"));
    if (txt) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        txt->text = buf;
    }
}

void HD_TopBar::SetSpeed(int speed) {
    currentSpeed = speed;
    printf("HD_UI: Game speed set to %d\n", speed);
    // TODO: Update speed marker position
}

void HD_TopBar::SetAccount(const std::string& accountNum, int balance) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("btnAccount/txtCaption"));
    if (txt) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Acc no. %s: %dc", accountNum.c_str(), balance);
        txt->text = buf;
    }
}

void HD_TopBar::SetLocation(const std::string& name, const std::string& ip) {
    UITextPoint* txtName = dynamic_cast<UITextPoint*>(FindElement("txtLocName"));
    UITextPoint* txtIP = dynamic_cast<UITextPoint*>(FindElement("txtLocIP"));
    if (txtName) txtName->text = name;
    if (txtIP) txtIP->text = ip;
}

void HD_TopBar::SetCPU(float usage) {
    // TODO: Update CPU bar fill width based on usage (0.0 - 1.0)
}

void HD_TopBar::SetUsername(const std::string& name) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("btnUser/txtCaption"));
    if (txt) {
        txt->text = name;
    }
}

// ============================================================================
// HD_TaskBar
// ============================================================================

HD_TaskBar::HD_TaskBar() {
    layoutName = "HUD/TaskBar";
}

HD_TaskBar::~HD_TaskBar() {
}

void HD_TaskBar::Create() {
    HDScreen::Create();
    SetupCallbacks();
}

void HD_TaskBar::Update(float dt) {
    HDScreen::Update(dt);
}

void HD_TaskBar::SetupCallbacks() {
    // TODO: Set up app button callbacks
}

void HD_TaskBar::SetEmailCount(int count) {
    UITextPoint* txt = dynamic_cast<UITextPoint*>(FindElement("btnEmail/notification/txtNumber"));
    if (txt) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", count > 99 ? 99 : count);
        txt->text = buf;
    }
    
    UIObject* notif = FindElement("btnEmail/notification");
    if (notif) {
        notif->visible = (count > 0);
    }
}

// ============================================================================
// HDUIManager
// ============================================================================

HDUIManager& HDUIManager::GetInstance() {
    static HDUIManager instance;
    return instance;
}

HDUIManager::HDUIManager()
    : hdModeActive(false)
{
}

HDUIManager::~HDUIManager() {
    Shutdown();
}

bool HDUIManager::Initialize(int width, int height, bool fullscreen) {
    if (hdModeActive) return true;
    
    // Initialize Allegro5
    if (!Allegro5System::Initialize(width, height, fullscreen)) {
        fprintf(stderr, "HDUIManager: Failed to initialize Allegro5\n");
        return false;
    }
    
    // Set up layout paths
    LayoutManager& mgr = LayoutManager::GetInstance();
    mgr.SetLayoutDirectory("uplinkHD/layouts");
    mgr.SetAssetDirectory("uplinkHD/graphics");
    
    // Load texture atlases
    AtlasManager& atlas = AtlasManager::GetInstance();
    atlas.SetBasePath("uplinkHD/graphics/");
    atlas.LoadAtlas("uplinkHD_atlas_00.xml");
    
    // Set base resolution for scaling
    Allegro5System::SetBaseResolution(1920, 1080);
    
    // Create screen instances
    mainMenu = std::unique_ptr<HD_MainMenu>(new HD_MainMenu());
    topBar = std::unique_ptr<HD_TopBar>(new HD_TopBar());
    taskBar = std::unique_ptr<HD_TaskBar>(new HD_TaskBar());
    
    hdModeActive = true;
    printf("HDUIManager: Initialized at %dx%d\n", width, height);
    
    return true;
}


void HDUIManager::Shutdown() {
    // if (!hdModeActive) return; // Disabled for testing
    
    mainMenu.reset();
    topBar.reset();
    taskBar.reset();
    
    LayoutManager::GetInstance().ClearCache();
    Allegro5System::Shutdown();
    
    hdModeActive = false;
    printf("HDUIManager: Shutdown complete\n");
}

void HDUIManager::Update(float dt) {
    // if (!hdModeActive) return; // Disabled for testing
    
    if (mainMenu && mainMenu->IsVisible()) mainMenu->Update(dt);
    if (topBar && topBar->IsVisible()) topBar->Update(dt);
    if (taskBar && taskBar->IsVisible()) taskBar->Update(dt);
}

void HDUIManager::Draw() {
    // DEBUG: Draw green rect to verify HD UI Draw is called
    Allegro5System::DrawRectFilled(150, 150, 200, 200, 0.0f, 1.0f, 0.0f, 1.0f);
    // if (!hdModeActive) return; // Disabled for testing
    
    Allegro5System::BeginFrame();
    
    if (mainMenu) mainMenu->Draw(); // Force draw for testing
    if (topBar && topBar->IsVisible()) topBar->Draw();
    if (taskBar && taskBar->IsVisible()) taskBar->Draw();
    
    Allegro5System::EndFrame();
}

} // namespace HDUI
