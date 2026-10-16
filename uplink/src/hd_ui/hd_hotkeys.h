#ifndef _included_hd_hotkeys_h
#define _included_hd_hotkeys_h

#include <allegro5/allegro.h>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace HDUI {

class HDHotkeyManager {
public:
  struct Binding {
    std::string command;
    int key;
    unsigned modifiers;
    bool canOverwrite;
  };
  typedef std::function<bool()> Action;
  typedef bool (*ActionFilter)(const std::string &command);

  HDHotkeyManager();
  bool LoadDefaults(const std::string &path);
  bool LoadUser(const std::string &path);
  void RegisterAction(const std::string &command, Action action);
  bool Invoke(const std::string &command);
  bool DispatchEvent(const ALLEGRO_EVENT &event,
                     ActionFilter filter = nullptr);
  void ReleaseConsumedKey(int key);
  bool SetBinding(const std::string &command, int key, unsigned modifiers,
                  bool canOverwrite = false);
  bool SaveIfChanged();
  void DiscardUser(const std::string &path);
  void Clear();
  const std::vector<Binding> &GetBindings() const { return bindings; }

private:
  static unsigned WithoutLocks(unsigned modifiers);
  static bool ModifiersOverlap(unsigned first, unsigned second);
  static bool ReadBindings(const std::string &path, const char *root,
                           std::vector<Binding> &result, bool optional);
  bool Insert(const Binding &binding);
  bool HasChanges() const;
  std::vector<Binding> defaults, bindings, savedBindings;
  std::map<std::string, Action> actions;
  std::array<bool, ALLEGRO_KEY_MAX> consumedKeys;
  std::string userPath;
};

} // namespace HDUI
#endif
