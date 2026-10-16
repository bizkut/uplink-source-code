#ifndef _included_hd_taskmanager_v2_h
#define _included_hd_taskmanager_v2_h

#include <memory>

namespace HDUI {
class UIObject;

// The owner root must outlive the window. Model state survives Hide/Show;
// attached layouts and independently parsed rows belong exclusively to its tree.
class HDTaskManagerV2 {
public:
    explicit HDTaskManagerV2(UIObject& ownerRoot);
    ~HDTaskManagerV2();

    HDTaskManagerV2(const HDTaskManagerV2&) = delete;
    HDTaskManagerV2& operator=(const HDTaskManagerV2&) = delete;

    bool Show();
    void Hide();
    bool IsVisible() const;
    // Call every game frame, including while hidden, to observe PID lifetimes.
    void Update();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace HDUI

#endif
