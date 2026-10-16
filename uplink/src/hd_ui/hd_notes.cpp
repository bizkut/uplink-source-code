#include "hd_notes.h"
#include "hd_layout_parser.h"
#include <tinyxml2.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <utility>
#include <unistd.h>

namespace HDUI {
namespace {
void FileError(const char *operation, const std::string &path, const char *detail) {
  std::fprintf(stderr, "HD_UI: Cannot %s notes %s: %s\n", operation, path.c_str(),
               detail ? detail : "Unknown error");
}
} // namespace

struct HDNotes::Note {
  std::string title, contents;
  // Exactly one of tree and window owns/references the XML tree. All other UI
  // pointers are borrowed only while ownerRoot owns the attached window.
  std::unique_ptr<UIObject> tree;
  UIObject *window = nullptr;
  UIButtonTextField *titleField = nullptr;
  UIButtonTextFieldBox *contentsField = nullptr;
  UIButtonStandard *newButton = nullptr;
  UIButtonStandard *removeButton = nullptr;
  UIButtonStandard *closeButton = nullptr;
};

HDNotes::HDNotes(UIObject &root) : ownerRoot(root), loaded(false) {}

HDNotes::~HDNotes() noexcept {
  try {
    if (loaded)
      Save();
  } catch (const std::exception &error) {
    FileError("save", userFile, error.what());
  } catch (...) {
    FileError("save", userFile, "Unexpected exception during destruction");
  }
  for (const auto &note : notes) {
    if (!note->window)
      continue;
    // Even an exceptional focus-loss callback must not leave callbacks into a
    // destroyed manager on an owner-root child that could not be detached.
    note->window->visible = false;
    UIObject *controls[] = {note->window, note->titleField, note->contentsField,
                            note->newButton, note->removeButton, note->closeButton};
    for (UIObject *control : controls)
      if (control)
        control->onClickCallback = nullptr;
    try {
      DetachWindow(*note);
    } catch (...) {
      std::fprintf(stderr, "HD_UI: Cannot detach notes window during destruction\n");
    }
  }
}

bool HDNotes::ReadNotes(const std::string &path,
                        std::vector<std::unique_ptr<Note>> &result) {
  std::FILE *input = std::fopen(path.c_str(), "rb");
  if (!input) {
    const int error = errno;
    if (error == ENOENT)
      return true;
    FileError("load", path, std::strerror(error));
    return false;
  }
  tinyxml2::XMLDocument document;
  const tinyxml2::XMLError error = document.LoadFile(input);
  const bool readFailed = std::ferror(input) != 0;
  const int closeResult = std::fclose(input);
  if (error != tinyxml2::XML_SUCCESS || readFailed || closeResult != 0) {
    FileError("load", path, document.ErrorStr() ? document.ErrorStr() : "File read failed");
    return false;
  }
  const tinyxml2::XMLElement *root = document.RootElement();
  if (!root || std::strcmp(root->Name(), "userNotes") || root->NextSiblingElement()) {
    FileError("load", path, "Expected one <userNotes> root");
    return false;
  }
  // sub_4AB010 reads the two attributes into independent HD_Note windows.
  for (const tinyxml2::XMLElement *element = root->FirstChildElement("note");
       element; element = element->NextSiblingElement("note")) {
    std::unique_ptr<Note> note(new Note());
    if (const char *title = element->Attribute("title"))
      note->title = title;
    if (const char *contents = element->Attribute("contents"))
      note->contents = contents;
    result.push_back(std::move(note));
  }
  return true;
}

bool HDNotes::PrepareWindow(Note &note, std::size_t ordinal) {
  note.tree = LayoutManager::GetInstance().TakeLayout("HUD/windows/WinNote");
  if (!note.tree) {
    std::fprintf(stderr, "HD_UI: Cannot load notes layout HUD/windows/WinNote.xml\n");
    return false;
  }
  UIButtonTextField *title = dynamic_cast<UIButtonTextField *>(note.tree->FindChild("btnTitle"));
  UIButtonTextFieldBox *contents = dynamic_cast<UIButtonTextFieldBox *>(note.tree->FindChild("btnContents"));
  UIButtonStandard *create = dynamic_cast<UIButtonStandard *>(note.tree->FindChild("btnNew"));
  UIButtonStandard *remove = dynamic_cast<UIButtonStandard *>(note.tree->FindChild("btnRem"));
  UIButtonStandard *close = dynamic_cast<UIButtonStandard *>(note.tree->FindChild("btnClose"));
  if (!title || !contents || !create || !remove || !close) {
    std::fprintf(stderr, "HD_UI: Notes layout is missing its editable fields or window buttons\n");
    note.tree.reset();
    return false;
  }
  // Do not apply the generic field's 256-character default to stored notes.
  title->maxLength = -1;
  title->multiline = false;
  contents->maxLength = -1;
  contents->multiline = true;
  title->SetText(note.title);
  contents->SetText(note.contents);

  Note *model = &note;
  const auto raise = [model](UIObject *) {
    if (model->window)
      model->window->BringToFront();
  };
  note.tree->interactive = true;
  note.tree->onClickCallback = raise;
  title->onClickCallback = raise;
  contents->onClickCallback = raise;
  // These are the complete note actions in sub_4F57D0. There are no GOLD
  // previous/next actions: sub_4AAAF0 appends another independent window.
  create->onClickCallback = [this](UIObject *) {
    retiredNotes.clear();
    NewNote();
  };
  remove->onClickCallback = [this, model](UIObject *) {
    retiredNotes.clear();
    RemoveNote(*model);
  };
  close->onClickCallback = [this, model](UIObject *) {
    retiredNotes.clear();
    CloseNote(*model);
  };

  float width = ownerRoot.w;
  float height = ownerRoot.h;
  const Geometry rootGeometry = ownerRoot.GetGeometry();
  if (width <= 0 && rootGeometry.sx > 0)
    width = Allegro5System::GetWidth() / rootGeometry.sx;
  if (height <= 0 && rootGeometry.sy > 0)
    height = Allegro5System::GetHeight() / rootGeometry.sy;
  const float right = std::max(0.0f, width - note.tree->w - 16.0f);
  const float top = std::min(80.0f, std::max(0.0f, height - note.tree->h));
  const std::size_t rows = static_cast<std::size_t>(
      std::max(0.0f, height - top - note.tree->h) / 26.0f) + 1;
  const std::size_t columns = static_cast<std::size_t>(right / 26.0f) + 1;
  // Expose successive titlebars down the right edge, then cascade left when
  // the next complete window would run below the owner root.
  note.tree->x = right - static_cast<float>((ordinal / rows) % columns) * 26.0f;
  note.tree->y = top + static_cast<float>(ordinal % rows) * 26.0f;
  return true;
}

void HDNotes::AttachWindow(Note &note) {
  if (note.window) {
    note.window->BringToFront();
    return;
  }
  UIObject *window = note.tree.get();
  ownerRoot.AddChild(std::move(note.tree));
  note.window = window;
  note.titleField = static_cast<UIButtonTextField *>(window->FindChild("btnTitle"));
  note.contentsField = static_cast<UIButtonTextFieldBox *>(window->FindChild("btnContents"));
  note.newButton = static_cast<UIButtonStandard *>(window->FindChild("btnNew"));
  note.removeButton = static_cast<UIButtonStandard *>(window->FindChild("btnRem"));
  note.closeButton = static_cast<UIButtonStandard *>(window->FindChild("btnClose"));
  note.window->visible = true;
  note.window->BringToFront();
}

void HDNotes::DetachWindow(Note &note) {
  if (!note.window)
    return;
  note.tree = ownerRoot.DetachChild(note.window);
  note.window = nullptr;
  note.titleField = nullptr;
  note.contentsField = nullptr;
  note.newButton = nullptr;
  note.removeButton = nullptr;
  note.closeButton = nullptr;
}

void HDNotes::Commit(Note &note) {
  if (note.titleField && note.title != note.titleField->inputText)
    note.title = note.titleField->inputText;
  if (note.contentsField && note.contents != note.contentsField->inputText)
    note.contents = note.contentsField->inputText;
}

void HDNotes::CommitAll() {
  for (const auto &note : notes)
    Commit(*note);
}

bool HDNotes::Show(const std::string &path) {
  retiredNotes.clear();
  if (path.empty()) {
    FileError("load", path, "No user file specified");
    return false;
  }
  if (loaded && userFile == path) {
    for (const auto &note : notes)
      AttachWindow(*note);
    return true;
  }
  std::vector<std::unique_ptr<Note>> replacement;
  if (!ReadNotes(path, replacement))
    return false;
  if (replacement.empty())
    replacement.emplace_back(new Note());
  for (std::size_t i = 0; i < replacement.size(); ++i)
    if (!PrepareWindow(*replacement[i], i))
      return false;
  // Load and prepare the replacement first. Neither a bad new file/layout nor
  // an unsuccessful save may destroy the current user's edited note set.
  if (loaded && !Save())
    return false;
  Hide();
  notes = std::move(replacement);
  userFile = path;
  loaded = true;
  for (const auto &note : notes)
    AttachWindow(*note);
  return true;
}

void HDNotes::Hide() {
  retiredNotes.clear();
  CommitAll();
  for (const auto &note : notes)
    DetachWindow(*note);
}

void HDNotes::Discard(const std::string &path) {
  if (!loaded || userFile != path)
    return;
  retiredNotes.clear();
  for (const auto &note : notes)
    DetachWindow(*note);
  notes.clear();
  userFile.clear();
  loaded = false;
}

bool HDNotes::IsVisible() const {
  for (const auto &note : notes)
    if (note->window && note->window->IsVisible())
      return true;
  return false;
}

void HDNotes::NewNote() {
  CommitAll();
  std::unique_ptr<Note> note(new Note());
  if (!PrepareWindow(*note, notes.size()))
    return;
  Note *created = note.get();
  notes.push_back(std::move(note));
  AttachWindow(*created);
  ownerRoot.SetFocus(created->titleField);
}

void HDNotes::RemoveNote(Note &note) {
  const auto position = std::find_if(notes.begin(), notes.end(),
      [&note](const std::unique_ptr<Note> &candidate) { return candidate.get() == &note; });
  if (position == notes.end())
    return;
  std::unique_ptr<Note> blank;
  if (notes.size() == 1) {
    blank.reset(new Note());
    if (!PrepareWindow(*blank, 0))
      return;
  }
  // Reserve before detaching so allocation failure cannot retire only half a
  // model. The active button callback remains alive on the retired tree.
  retiredNotes.reserve(retiredNotes.size() + 1);
  DetachWindow(note);
  retiredNotes.push_back(std::move(*position));
  notes.erase(position);
  if (blank) {
    Note *created = blank.get();
    notes.push_back(std::move(blank));
    AttachWindow(*created);
    ownerRoot.SetFocus(created->titleField);
  }
}

void HDNotes::CloseNote(Note &note) {
  Save();
  // sub_4F6180 hides only this HD_Note; its model remains in the writer's set.
  DetachWindow(note);
}

bool HDNotes::Save() {
  retiredNotes.clear();
  if (!loaded || userFile.empty()) {
    FileError("save", userFile, "No user file loaded");
    return false;
  }
  CommitAll();
  tinyxml2::XMLDocument document;
  tinyxml2::XMLElement *root = document.NewElement("userNotes");
  document.InsertEndChild(root);
  for (const auto &note : notes) {
    // GOLD sub_4AB340 deliberately omits completely empty notes, but preserves
    // title-only and contents-only notes in their original model order.
    if (note->title.empty() && note->contents.empty())
      continue;
    tinyxml2::XMLElement *element = document.NewElement("note");
    element->SetAttribute("title", note->title.c_str());
    element->SetAttribute("contents", note->contents.c_str());
    element->InsertEndChild(document.NewText(""));
    root->InsertEndChild(element);
  }
  // Keep the final writer's paired root/note tags even for an empty note set.
  if (!root->FirstChild())
    root->InsertEndChild(document.NewText(""));

  std::string temporary = userFile + ".tmp.XXXXXX";
  const int descriptor = ::mkstemp(temporary.data());
  if (descriptor < 0) {
    FileError("save", userFile, std::strerror(errno));
    return false;
  }
  std::FILE *output = ::fdopen(descriptor, "wb");
  if (!output) {
    const int error = errno;
    ::close(descriptor);
    std::remove(temporary.c_str());
    FileError("save", userFile, std::strerror(error));
    return false;
  }
  errno = 0;
  const tinyxml2::XMLError result = document.SaveFile(output);
  bool written = result == tinyxml2::XML_SUCCESS && !std::ferror(output);
  int writeError = errno;
  if (std::fflush(output) != 0) {
    written = false;
    writeError = errno;
  }
  if (std::fclose(output) != 0) {
    written = false;
    writeError = errno;
  }
  if (!written) {
    std::remove(temporary.c_str());
    FileError("save", userFile, writeError ? std::strerror(writeError) : "XML write failed");
    return false;
  }
  if (std::rename(temporary.c_str(), userFile.c_str()) != 0) {
    const int error = errno;
    std::remove(temporary.c_str());
    FileError("save", userFile, std::strerror(error));
    return false;
  }
  return true;
}

} // namespace HDUI
