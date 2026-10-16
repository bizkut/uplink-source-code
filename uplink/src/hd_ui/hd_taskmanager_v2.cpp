#include "hd_taskmanager_v2.h"
#include "hd_layout_parser.h"
#include "hd_legacy_bridge.h"
#include "vanbakel.h"
#include "app/app.h"
#include "game/game.h"
#include "interface/interface.h"
#include "interface/taskmanager/securitybypass.h"
#include "interface/taskmanager/taskmanager.h"
#include "interface/taskmanager/uplinktask.h"
#include "mainmenu/mainmenu.h"
#include "world/world.h"
#include "world/player.h"
#include "world/computer/gateway.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace HDUI {
namespace {
bool InGame() {
    // Match the HD session guard: pause retains the HUD, startup does not.
    return app && !app->Closed() && game && game->GameSpeed() >= GAMESPEED_PAUSED &&
        !app->GetMainMenu()->IsVisible() && LegacyHasButton("hud_mainmenu");
}

std::string AssetPath(const char* relative) {
    std::string path = app ? app->path : "";
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += '/';
    return path + relative;
}


std::string PriorityColor(unsigned low, unsigned high, float priority) {
    char color[7];
    unsigned rgb = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const unsigned first = (low >> shift) & 255u;
        const unsigned last = (high >> shift) & 255u;
        const unsigned value = static_cast<unsigned>(
            std::min(first, last) + (std::max(first, last) - std::min(first, last)) * priority);
        rgb |= value << shift;
    }
    std::snprintf(color, sizeof(color), "%06X", rgb);
    return color;
}

class TitleDrag final : public UIInteractive {
public:
    explicit TitleDrag(float width) : offsetX(0), offsetY(0), startX(0), startY(0) {
        name = "taskManagerTitleDrag";
        w = width;
        h = 45;
    }
    void OnPointerDown(float px, float py, int button) override {
        if (button != 1 || !parent)
            return;
        UIObject::OnMouseDown();
        offsetX = px;
        offsetY = py;
        startX = parent->x;
        startY = parent->y;
        parent->BringToFront();
    }
    void OnPointerMove(float px, float py) override {
        if (!isPressed || !parent)
            return;
        const Geometry geometry = parent->parent ? parent->parent->GetGeometry() : parent->GetGeometry();
        if (geometry.sx != 0) parent->x = startX + (px - offsetX) / geometry.sx;
        if (geometry.sy != 0) parent->y = startY + (py - offsetY) / geometry.sy;
    }
private:
    float offsetX, offsetY, startX, startY;
};
} // namespace

class HDTaskManagerV2::Impl {
public:
    explicit Impl(UIObject& root)
        : ownerRoot(root), window(nullptr), list(nullptr), error(nullptr),
          advanced(false), dirty(true), windowX(0), windowY(26) {}
    ~Impl() { Hide(); }

    struct RowView {
        UIObject* tree = nullptr;
        UITextPoint* name = nullptr;
        UITextPoint* percentage = nullptr;
        UIRectangle* bar = nullptr;
        UIObject* close = nullptr;
        UIObject* unpinnedIcon = nullptr;
        UIObject* pinnedIcon = nullptr;
        float barWidth = 0;
        unsigned lowColor = 0, highColor = 0;
        std::string nameColor, percentageColor;
    };
    struct TaskState {
        std::string name, caption, orderKey;
        double priority = 0;
        float minimum = 0;
        float version = 0;
        bool versioned = false;
        bool visualDirty = true;
        bool pinned = false;
        bool seen = false;
        RowView view;
    };

    bool Show() {
        if (!InGame())
            return false;
        const bool requestedAdvanced =
            game->GetWorld()->GetPlayer()->gateway.HasHUDUpgrade(
                HUDUPGRADE_TASKMANAGERV2);
        if (window && requestedAdvanced != advanced)
            Hide();
        if (window) {
            window->BringToFront();
            return true;
        }
        advanced = requestedAdvanced;
        Update();
        const char* windowName =
            advanced ? "HUD/windows/WinTaskManagerV2"
                     : "HUD/windows/WinTaskManager";
        std::unique_ptr<UIObject> layout =
            LayoutManager::GetInstance().TakeLayout(windowName);
        if (!layout)
            return false;
        auto* rowList = dynamic_cast<UIScrollableList*>(layout->FindChild("list"));
        UIObject* noApplications = layout->FindChild("txtError");
        UIObject* close = layout->FindChild("btnClose");
        UIObject* balance = layout->FindChild("btnBalanceAll");
        if (!rowList || !noApplications || (advanced && (!close || !balance))) {
            std::fprintf(stderr, "HD_UI: Incomplete %s layout\n", windowName);
            return false;
        }
        layout->x = windowX;
        layout->y = windowY;
        layout->interactive = true;
        if (advanced) {
            layout->AddChild(std::unique_ptr<UIObject>(new TitleDrag(close->x)));
            close->onClickCallback = [this](UIObject*) { Hide(); };
            balance->onClickCallback = [this](UIObject*) { BalanceAll(); };
        }
        rowParser.SetAssetBasePath(AssetPath("uplinkHD/graphics"));
        rowLayoutPath = AssetPath(
            advanced
                ? "uplinkHD/layouts/HUD/windows/WinTaskManagerElementV2.xml"
                : "uplinkHD/layouts/HUD/windows/WinTaskManagerElement.xml");
        window = layout.get();
        list = rowList;
        error = noApplications;
        ownerRoot.AddChild(std::move(layout));
        window->BringToFront();
        dirty = true;
        Update();
        return window != nullptr;
    }

    void Hide() {
        if (!window)
            return;
        UIObject* removed = window;
        windowX = window->x;
        windowY = window->y;
        window = nullptr;
        list = nullptr;
        error = nullptr;
        for (auto& item : tasks)
            item.second.view = RowView();
        dirty = true;
        ownerRoot.DetachChild(removed);
    }

    bool IsVisible() const { return window && window->IsVisible(); }

    void Update() {
        if (!InGame()) {
            Hide();
            return;
        }
        const bool requestedAdvanced =
            game->GetWorld()->GetPlayer()->gateway.HasHUDUpgrade(
                HUDUPGRADE_TASKMANAGERV2);
        if (window && requestedAdvanced != advanced) {
            Hide();
            Show();
            return;
        }
        for (auto& item : tasks)
            item.second.seen = false;
        const int count = SvbNumTasks();
        for (int index = 0; index < count; ++index) {
            TaskWrapper* wrapper = SvbGetTaskWrapperAtIndex(index);
            if (!wrapper)
                continue;
            const int pid = wrapper->pid;
            auto result = tasks.try_emplace(pid);
            TaskState& state = result.first->second;
            const char* liveName = wrapper->name ? wrapper->name : "";
            const auto* task = dynamic_cast<UplinkTask*>(wrapper->task);
            const float version = task ? task->version : 1.0f;
            const bool renamed = result.second || state.name != liveName;
            if (renamed) {
                state.name = liveName;
                // The target enumerates an ordered map keyed by app name + ID.
                state.orderKey = state.name + std::to_string(pid);
                dirty = true;
            }
            if (renamed || state.version != version || state.versioned != (task != nullptr)) {
                state.version = version;
                state.versioned = task != nullptr;
                state.caption = state.name;
                if (task && state.name != "File Download" && state.name != "File Upload" &&
                    state.name != "File Delete Remote" && state.name != "File Delete Local") {
                    char suffix[32];
                    std::snprintf(suffix, sizeof(suffix), " v%d.0", static_cast<int>(version));
                    state.caption += suffix;
                }
                state.visualDirty = true;
            }
            if (state.priority != wrapper->priority) {
                state.priority = wrapper->priority;
                state.visualDirty = true;
            }
            state.seen = true;
            const float minimum = static_cast<float>(SvbGetMinimumCPUShare(pid));
            if (state.minimum != minimum) {
                state.minimum = minimum;
                state.visualDirty = true;
            }
        }
        for (auto it = tasks.begin(); it != tasks.end();) {
            if (it->second.seen) {
                ++it;
                continue;
            }
            UIObject* removed = it->second.view.tree;
            it->second.view = RowView();
            if (removed && list)
                list->DetachChild(removed);
            it = tasks.erase(it);
            dirty = true;
        }
        if (!window)
            return;
        for (auto& item : tasks) {
            if (!item.second.view.tree && !AttachRow(item.first, item.second)) {
                Hide();
                return;
            }
        }
        if (dirty) {
            order.clear();
            for (const auto& item : tasks)
                order.push_back(item.first);
            std::sort(order.begin(), order.end(), [this](int left, int right) {
                const std::string& first = tasks.at(left).orderKey;
                const std::string& second = tasks.at(right).orderKey;
                return first != second ? first < second : left < right;
            });
            for (size_t i = 0; i < order.size(); ++i)
                tasks.at(order[i]).view.tree->zOrder = static_cast<int>(i);
            std::sort(list->children.begin(), list->children.end(),
                [](const std::unique_ptr<UIObject>& left, const std::unique_ptr<UIObject>& right) {
                    return left->zOrder < right->zOrder;
                });
            list->ArrangeChildren();
            list->ClampScroll();
            dirty = false;
        }
        error->visible = tasks.empty();
        list->visible = !tasks.empty();
        for (auto& item : tasks)
            RefreshRow(item.second);
    }

private:
    bool AttachRow(int pid, TaskState& state) {
        std::unique_ptr<UIObject> tree = rowParser.LoadLayout(rowLayoutPath);
        if (!tree) {
            std::fprintf(stderr, "HD_UI: %s\n", rowParser.GetError().c_str());
            return false;
        }
        RowView view;
        view.tree = tree.get();
        view.name = dynamic_cast<UITextPoint*>(tree->FindChild("txtName"));
        view.percentage = dynamic_cast<UITextPoint*>(tree->FindChild("txtPrio"));
        view.bar = dynamic_cast<UIRectangle*>(tree->FindChild("priority"));
        view.close = tree->FindChild("btnClose");
        view.unpinnedIcon = tree->FindChildRecursive("btnPinPrio/icon");
        view.pinnedIcon = tree->FindChildRecursive("btnPinPrio/icon2");
        UIObject* pin = tree->FindChild("btnPinPrio");
        UIObject* increase = tree->FindChild("btnIncrease");
        UIObject* decrease = tree->FindChild("btnDecrease");
        auto* low = dynamic_cast<UIRectangle*>(tree->FindChild("cPrioLow"));
        auto* high = dynamic_cast<UIRectangle*>(tree->FindChild("cPrioHigh"));
        if (!view.name || !view.percentage || !view.bar || !view.close ||
            !increase || !decrease || !low || !high ||
            (advanced && (!view.unpinnedIcon || !view.pinnedIcon || !pin))) {
            std::fprintf(stderr, "HD_UI: Incomplete task manager row layout\n");
            return false;
        }
        view.barWidth = view.bar->w;
        view.lowColor = static_cast<unsigned>(std::strtoul(low->fillColor.c_str(), nullptr, 16));
        view.highColor = static_cast<unsigned>(std::strtoul(high->fillColor.c_str(), nullptr, 16));
        view.nameColor = view.name->fillColor;
        view.percentageColor = view.percentage->fillColor;
        low->visible = false;
        high->visible = false;
        tree->name = "taskPID" + std::to_string(pid);
        increase->onClickCallback = [this, pid](UIObject*) { ChangePriority(pid, 0.05); };
        decrease->onClickCallback = [this, pid](UIObject*) { ChangePriority(pid, -0.05); };
        if (pin)
            pin->onClickCallback = [this, pid](UIObject*) { TogglePin(pid); };
        view.close->onClickCallback = [this, pid](UIObject*) { Terminate(pid); };
        state.view = std::move(view);
        state.visualDirty = true;
        list->AddChild(std::move(tree));
        dirty = true;
        return true;
    }

    void RefreshRow(TaskState& state) {
        if (!state.visualDirty)
            return;
        state.visualDirty = false;
        RowView& view = state.view;
        const float priority = static_cast<float>(std::clamp(state.priority, 0.0, 1.0));
        const bool lowCPU = state.minimum > priority;
        // 0x43DD00: warning color (0.82, 0.1, 0.1, 1).
        static const char warning[] = "D11919";
        view.name->text = state.caption;
        char percentage[16];
        std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(priority * 100.0f));
        view.percentage->text = percentage;
        view.bar->w = view.barWidth * priority;
        view.bar->fillColor = lowCPU ? warning : PriorityColor(view.lowColor, view.highColor, priority);
        view.name->fillColor = lowCPU ? warning : view.nameColor;
        view.percentage->fillColor = lowCPU ? warning : view.percentageColor;
        if (view.unpinnedIcon) view.unpinnedIcon->visible = !state.pinned;
        if (view.pinnedIcon) view.pinnedIcon->visible = state.pinned;
        view.close->visible = state.name != "Revelation";
    }

    bool IsPinned(int pid) const {
        const auto found = tasks.find(pid);
        return found != tasks.end() && found->second.pinned;
    }

    static bool PriorityForPID(int pid, double& priority) {
        const int index = SvbLookupIndex(pid);
        if (index < 0)
            return false;
        TaskWrapper* wrapper = SvbGetTaskWrapperAtIndex(index);
        if (!wrapper || wrapper->pid != pid)
            return false;
        priority = wrapper->priority;
        return true;
    }

    static void ChangeLocked(int pid, double amount) {
        const int index = SvbLookupIndex(pid);
        if (index >= 0)
            SvbChangePriority(index, amount, true);
    }

    void TogglePin(int pid) {
        if (SvbLookupIndex(pid) < 0)
            return;
        const auto found = tasks.find(pid);
        if (found == tasks.end())
            return;
        found->second.pinned = !found->second.pinned;
        found->second.visualDirty = true;
        dirty = true;
    }

    void ChangePriority(int pid, double amount) {
        double current = 0;
        if (!PriorityForPID(pid, current))
            return;
        priorityPeers.clear();
        double pinnedTotal = 0;
        double total = 0;
        double peerTotal = 0;
        bool anyPinned = false;
        for (int i = 0; i < SvbNumTasks(); ++i) {
            TaskWrapper* wrapper = SvbGetTaskWrapperAtIndex(i);
            if (!wrapper)
                continue;
            total += wrapper->priority;
            if (IsPinned(wrapper->pid)) {
                pinnedTotal += wrapper->priority;
                anyPinned = true;
            } else if (wrapper->pid != pid) {
                priorityPeers.emplace_back(wrapper->pid, wrapper->priority);
                peerTotal += wrapper->priority;
            }
        }
        dirty = true;
        if (!anyPinned) {
            const int index = SvbLookupIndex(pid);
            if (index >= 0)
                SvbChangePriority(index, amount);
            return;
        }
        // Pins exclude peers from redistribution; explicit changes to a pin
        // still work, but cannot take CPU reserved by another pinned task.
        const double maximum = IsPinned(pid) ? 1.0 : std::max(0.0, 1.0 - pinnedTotal);
        double actual = std::clamp(current + amount, 0.0, maximum) - current;
        if (actual > 0)
            actual = std::min(actual, std::max(0.0, 1.0 - total) + peerTotal);
        ChangeLocked(pid, actual);
        if (actual <= 0)
            return;
        // Fill idle CPU first, then subtract exactly the excess in stable
        // scheduler order. Carry the remainder so clamping cannot mint CPU.
        double excess = std::max(0.0, total + actual - 1.0);
        for (const auto& peer : priorityPeers) {
            if (excess <= 0)
                break;
            if (peer.second <= 0)
                continue;
            const double reduction = std::min(peer.second, std::min(excess,
                peerTotal > peer.second ? excess * (peer.second / peerTotal) : excess));
            ChangeLocked(peer.first, -reduction);
            peerTotal -= peer.second;
            excess -= reduction;
        }
        if (excess > 0)
            ChangeLocked(pid, -excess);
    }

    void BalanceAll() {
        priorityPeers.clear();
        double pinnedTotal = 0;
        bool anyPinned = false;
        for (int i = 0; i < SvbNumTasks(); ++i) {
            TaskWrapper* wrapper = SvbGetTaskWrapperAtIndex(i);
            if (!wrapper)
                continue;
            if (IsPinned(wrapper->pid)) {
                pinnedTotal += wrapper->priority;
                anyPinned = true;
            } else {
                priorityPeers.emplace_back(wrapper->pid, wrapper->priority);
            }
        }
        dirty = true;
        if (!anyPinned) {
            SvbCalculatePriorities();
            return;
        }
        if (priorityPeers.empty())
            return;
        // GOLD 0x4AD170 balances only the CPU budget left by pinned tasks.
        const double priority = std::max(0.0, 1.0 - pinnedTotal) / priorityPeers.size();
        for (const auto& peer : priorityPeers) {
            double current = 0;
            if (PriorityForPID(peer.first, current))
                ChangeLocked(peer.first, priority - current);
        }
    }

    void Terminate(int pid) {
        if (!InGame())
            return;
        const int index = SvbLookupIndex(pid);
        if (index < 0)
            return;
        {
            TaskWrapper* wrapper = SvbGetTaskWrapperAtIndex(index);
            if (!wrapper || wrapper->pid != pid ||
                (wrapper->name && std::strcmp(wrapper->name, "Revelation") == 0))
                return;
            if (auto* bypass = dynamic_cast<SecurityBypass*>(wrapper->task))
                bypass->EndCurrentBypass();
            TaskManager* manager = game->GetInterface()->GetTaskManager();
            if (manager->GetTargetProgram() == wrapper->task)
                manager->SetTargetProgram(-1);
        }
        // SvbRemoveTask removes the task's interface, deletes the task and
        // wrapper, and rebalances. Never remove/reuse this callback's row here.
        dirty = true;
        SvbRemoveTask(pid);
    }

    UIObject& ownerRoot;
    UIObject* window;
    UIScrollableList* list;
    UIObject* error;
    LayoutParser rowParser;
    std::string rowLayoutPath;
    std::map<int, TaskState> tasks;
    std::vector<int> order;
    std::vector<std::pair<int, double>> priorityPeers;
    bool advanced;
    bool dirty;
    float windowX, windowY;
};

HDTaskManagerV2::HDTaskManagerV2(UIObject& ownerRoot) : impl(new Impl(ownerRoot)) {}
HDTaskManagerV2::~HDTaskManagerV2() = default;
bool HDTaskManagerV2::Show() { return impl->Show(); }
void HDTaskManagerV2::Hide() { impl->Hide(); }
bool HDTaskManagerV2::IsVisible() const { return impl->IsVisible(); }
void HDTaskManagerV2::Update() { impl->Update(); }
} // namespace HDUI
