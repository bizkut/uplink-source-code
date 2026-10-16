#include "hd_hotkeys.h"
#include <tinyxml2.h>
#include <cstdio>
#include <utility>

namespace HDUI {

HDHotkeyManager::HDHotkeyManager() : consumedKeys{} {}

unsigned HDHotkeyManager::WithoutLocks(unsigned modifiers) {
  // GOLD sub_4985C0 ignores Allegro's scroll/num/caps-lock bits (0x700).
  return modifiers & ~(ALLEGRO_KEYMOD_SCROLLLOCK | ALLEGRO_KEYMOD_NUMLOCK |
                       ALLEGRO_KEYMOD_CAPSLOCK);
}

bool HDHotkeyManager::ModifiersOverlap(unsigned first, unsigned second) {
  first = WithoutLocks(first);
  second = WithoutLocks(second);
  // Preserve GOLD's modifier matching: an overlapping modifier, or neither.
  return (first & second) != 0 || (first == 0 && second == 0);
}

bool HDHotkeyManager::ReadBindings(const std::string &path, const char *rootName,
                                  std::vector<Binding> &result, bool optional) {
  tinyxml2::XMLDocument document;
  const tinyxml2::XMLError error = document.LoadFile(path.c_str());
  if (optional && error == tinyxml2::XML_ERROR_FILE_NOT_FOUND)
    return true;
  const tinyxml2::XMLElement *root = document.FirstChildElement(rootName);
  if (error != tinyxml2::XML_SUCCESS || !root) {
    std::fprintf(stderr, "HD_UI: Cannot load hotkeys %s: %s\n", path.c_str(),
                 document.ErrorStr() ? document.ErrorStr() : "Unexpected XML root");
    return false;
  }
  for (const tinyxml2::XMLElement *element = root->FirstChildElement("hotkey");
       element; element = element->NextSiblingElement("hotkey")) {
    const char *command = element->Attribute("commandName");
    int key = 0, overwrite = 0;
    unsigned modifiers = 0;
    if (!command || !*command ||
        element->QueryIntAttribute("key", &key) != tinyxml2::XML_SUCCESS ||
        element->QueryUnsignedAttribute("modifier", &modifiers) != tinyxml2::XML_SUCCESS ||
        key < 0 || key >= ALLEGRO_KEY_MAX) {
      std::fprintf(stderr, "HD_UI: Invalid hotkey entry in %s\n", path.c_str());
      return false;
    }
    element->QueryIntAttribute("canOverwrite", &overwrite);
    result.push_back(Binding{command, key, modifiers, overwrite != 0});
  }
  return true;
}

bool HDHotkeyManager::Insert(const Binding &binding) {
  if (binding.command.empty() || binding.key < 0 || binding.key >= ALLEGRO_KEY_MAX)
    return false;
  size_t commandIndex = bindings.size();
  for (size_t i = 0; i < bindings.size(); ++i) {
    const Binding &existing = bindings[i];
    if (existing.command == binding.command) {
      if (!existing.canOverwrite ||
          (existing.key == binding.key && existing.modifiers == binding.modifiers))
        return false;
      commandIndex = i;
    }
    if (existing.key == binding.key &&
        ModifiersOverlap(existing.modifiers, binding.modifiers) && !existing.canOverwrite)
      return false;
  }
  // Check all immutable records before removing any replaceable collision.
  for (size_t i = bindings.size(); i > 0; --i) {
    const size_t index = i - 1;
    const Binding &existing = bindings[index];
    if (index != commandIndex && existing.key == binding.key &&
        ModifiersOverlap(existing.modifiers, binding.modifiers)) {
      bindings.erase(bindings.begin() + index);
      if (commandIndex > index) --commandIndex;
    }
  }
  if (commandIndex < bindings.size()) {
    bindings[commandIndex].key = binding.key;
    bindings[commandIndex].modifiers = binding.modifiers;
  } else {
    bindings.push_back(binding);
  }
  return true;
}

bool HDHotkeyManager::LoadDefaults(const std::string &path) {
  std::vector<Binding> loaded;
  if (!ReadBindings(path, "defaultHotkeys", loaded, false))
    return false;
  bindings.clear();
  bindings.reserve(loaded.size());
  for (const Binding &binding : loaded)
    Insert(binding);
  defaults = bindings;
  savedBindings = bindings;
  userPath.clear();
  consumedKeys.fill(false);
  return true;
}

bool HDHotkeyManager::LoadUser(const std::string &path) {
  bindings = defaults;
  userPath = path;
  std::vector<Binding> loaded;
  const bool loadedFile = path.empty() || ReadBindings(path, "userHotkeys", loaded, true);
  if (loadedFile) {
    // sub_4981B0 protects the EXISTING record's canOverwrite flag, not the
    // incoming row's. Bundled immutable defaults cannot be displaced by a user.
    for (const Binding &binding : loaded)
      Insert(binding);
  }
  savedBindings = bindings;
  return loadedFile;
}

void HDHotkeyManager::DiscardUser(const std::string &path) {
  if (userPath != path)
    return;
  bindings = defaults;
  savedBindings = defaults;
  consumedKeys.fill(false);
  userPath.clear();
}

void HDHotkeyManager::RegisterAction(const std::string &command, Action action) {
  if (action)
    actions[command] = std::move(action);
  else
    actions.erase(command);
}

bool HDHotkeyManager::Invoke(const std::string &command) {
  const auto action = actions.find(command);
  return action != actions.end() && action->second();
}

bool HDHotkeyManager::DispatchEvent(const ALLEGRO_EVENT &event,
                                    ActionFilter filter) {
  if (event.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
    consumedKeys.fill(false);
    return false;
  }
  if (event.type != ALLEGRO_EVENT_KEY_DOWN && event.type != ALLEGRO_EVENT_KEY_CHAR &&
      event.type != ALLEGRO_EVENT_KEY_UP)
    return false;
  const int key = event.keyboard.keycode;
  if (key < 0 || key >= ALLEGRO_KEY_MAX)
    return false;
  if (event.type == ALLEGRO_EVENT_KEY_UP) {
    const bool consumed = consumedKeys[key];
    consumedKeys[key] = false;
    return consumed;
  }
  if (consumedKeys[key])
    return true;
  // One activation per physical press. Consume its KEY_CHAR/repeats and UP too,
  // including after an action switches away from its game/menu context.
  if (event.type != ALLEGRO_EVENT_KEY_DOWN)
    return false;
  for (const Binding &binding : bindings) {
    if (binding.key == key && ModifiersOverlap(binding.modifiers, event.keyboard.modifiers) &&
        (!filter || filter(binding.command)) && Invoke(binding.command)) {
      consumedKeys[key] = true;
      return true;
    }
  }
  return false;
}

void HDHotkeyManager::ReleaseConsumedKey(int key) {
  if (key >= 0 && key < ALLEGRO_KEY_MAX)
    consumedKeys[key] = false;
}

bool HDHotkeyManager::SetBinding(const std::string &command, int key, unsigned modifiers,
                                 bool canOverwrite) {
  return Insert(Binding{command, key, modifiers, canOverwrite});
}

bool HDHotkeyManager::HasChanges() const {
  if (bindings.size() != savedBindings.size())
    return true;
  for (size_t i = 0; i < bindings.size(); ++i) {
    const Binding &first = bindings[i], &second = savedBindings[i];
    if (first.command != second.command || first.key != second.key ||
        first.modifiers != second.modifiers || first.canOverwrite != second.canOverwrite)
      return true;
  }
  return false;
}

bool HDHotkeyManager::SaveIfChanged() {
  if (!HasChanges())
    return true;
  if (userPath.empty())
    return false;
  tinyxml2::XMLDocument document;
  tinyxml2::XMLElement *root = document.NewElement("userHotkeys");
  document.InsertEndChild(root);
  for (const Binding &binding : bindings) {
    tinyxml2::XMLElement *element = document.NewElement("hotkey");
    element->SetAttribute("commandName", binding.command.c_str());
    element->SetAttribute("key", binding.key);
    element->SetAttribute("modifier", binding.modifiers);
    element->SetAttribute("canOverwrite", binding.canOverwrite ? 1 : 0);
    root->InsertEndChild(element);
  }
  if (document.SaveFile(userPath.c_str()) != tinyxml2::XML_SUCCESS) {
    std::fprintf(stderr, "HD_UI: Cannot save hotkeys %s\n", userPath.c_str());
    return false;
  }
  savedBindings = bindings;
  return true;
}

void HDHotkeyManager::Clear() {
  consumedKeys.fill(false);
  bindings.clear();
  defaults.clear();
  savedBindings.clear();
  actions.clear();
  userPath.clear();
}

} // namespace HDUI
