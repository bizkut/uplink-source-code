/*
 * HD_UI Screen Classes
 * HD versions of game screens that use XML layouts
 */

#ifndef _included_hd_screens_h
#define _included_hd_screens_h

#include "hd_ui_object.h"
#include "hd_layout_parser.h"
#include <string>
#include <memory>

namespace HDUI {

// Base class for HD screens
class HDScreen {
public:
    HDScreen();
    virtual ~HDScreen();
    
    virtual void Create();
    virtual void Remove();
    virtual void Update(float dt);
    virtual void Draw();
    virtual bool HandleInput();
    
    bool IsVisible() const { return visible; }
    
protected:
    std::unique_ptr<UIObject> layout;
    bool visible;
    std::string layoutName;
    
    // Helper to load layout
    bool LoadLayout(const std::string& name);
    
    // Find element in layout by path
    UIObject* FindElement(const std::string& path);
};

// HD Main Menu screen
class HD_MainMenu : public HDScreen {
public:
    HD_MainMenu();
    ~HD_MainMenu() override;
    
    void Create() override;
    void Remove() override;
    void Update(float dt) override;
    void Draw() override;
    bool HandleInput() override;
    
    // Callbacks
    void OnShutdownClick();
    void OnOptionsClick();
    void OnPasswordSubmit();
    
    // Set user info
    void SetUsername(const std::string& name);
    void SetLastSignIn(const std::string& date);
    void SetVersion(const std::string& version);
    void ShowError(const std::string& message);
    void HideError();
    
private:
    void SetupCallbacks();
    
    std::string username;
    std::string password;
};

// HD TopBar (status bar at top of HUD)
class HD_TopBar : public HDScreen {
public:
    HD_TopBar();
    ~HD_TopBar() override;
    
    void Create() override;
    void Update(float dt) override;
    
    // Update display
    void SetDate(int day, int month, int year);
    void SetTime(int hour, int minute);
    void SetSpeed(int speed);
    void SetAccount(const std::string& accountNum, int balance);
    void SetLocation(const std::string& name, const std::string& ip);
    void SetCPU(float usage);
    void SetUsername(const std::string& name);
    
private:
    void SetupCallbacks();
    int currentSpeed;
};

// HD TaskBar (bottom bar with app icons)
class HD_TaskBar : public HDScreen {
public:
    HD_TaskBar();
    ~HD_TaskBar() override;
    
    void Create() override;
    void Update(float dt) override;
    
    // Email notification
    void SetEmailCount(int count);
    
private:
    void SetupCallbacks();
};

// Global HD UI manager - switches between SDL/OpenGL and HD mode
class HDUIManager {
public:
    static HDUIManager& GetInstance();
    
    // Initialize HD UI mode
    bool Initialize(int width, int height, bool fullscreen);
    void Shutdown();
    
    // Check if HD mode is active
    bool IsHDModeActive() const { return hdModeActive; }
    
    // Get screens
    HD_MainMenu* GetMainMenu() { return mainMenu.get(); }
    HD_TopBar* GetTopBar() { return topBar.get(); }
    HD_TaskBar* GetTaskBar() { return taskBar.get(); }
    
    // Frame update/draw
    void Update(float dt);
    void Draw();
    
private:
    HDUIManager();
    ~HDUIManager();
    
    bool hdModeActive;
    std::unique_ptr<HD_MainMenu> mainMenu;
    std::unique_ptr<HD_TopBar> topBar;
    std::unique_ptr<HD_TaskBar> taskBar;
};

} // namespace HDUI

#endif // _included_hd_screens_h
