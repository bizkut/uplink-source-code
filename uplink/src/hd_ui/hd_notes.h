#ifndef _included_hd_notes_h
#define _included_hd_notes_h

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace HDUI {
class UIObject;

// GOLD's notes are independent windows, not pages in a single editor. The
// owner root must outlive this manager; it owns each tree while attached.
class HDNotes {
public:
  explicit HDNotes(UIObject &ownerRoot);
  ~HDNotes() noexcept;
  HDNotes(const HDNotes &) = delete;
  HDNotes &operator=(const HDNotes &) = delete;

  // A missing file starts an empty set. A failed load leaves the current set
  // intact. Showing the same file preserves edits and reopens closed windows.
  bool Show(const std::string &userFile);
  // Commit and detach every window, retaining its model until Show/destruction.
  void Hide();
  bool IsVisible() const;
  // Commit all live editors and atomically replace the current user's file.
  bool Save();
  // Forget one retired profile without writing its cached model back to disk.
  void Discard(const std::string &userFile);

private:
  struct Note;
  bool ReadNotes(const std::string &path,
                 std::vector<std::unique_ptr<Note>> &result);
  bool PrepareWindow(Note &note, std::size_t ordinal);
  void AttachWindow(Note &note);
  void DetachWindow(Note &note);
  void Commit(Note &note);
  void CommitAll();
  void NewNote();
  void RemoveNote(Note &note);
  void CloseNote(Note &note);

  UIObject &ownerRoot;
  std::string userFile;
  bool loaded;
  std::vector<std::unique_ptr<Note>> notes;
  // Keep a removed callback's tree alive until the next independent operation.
  // Each button collects the previous retirement before starting its action.
  std::vector<std::unique_ptr<Note>> retiredNotes;
};
} // namespace HDUI
#endif
