#ifndef _included_hd_intro_h
#define _included_hd_intro_h

#include "hd_screens.h"

namespace HDUI {

// GOLD HD_Intro (sub_472F30): both layout trees belong to ownerRoot. Borrowed
// controls stay alive until manager synchronization, outside event callbacks.
class HDIntro : public HDScreen {
public:
  explicit HDIntro(UIObject &root);
  ~HDIntro() override;
  void Create() override;
  void Remove() override;
  void Update(float dt) override;
  bool HandleInput(const ALLEGRO_EVENT &event);

private:
  void SetStep(int next);
  void BindStep(const char *path, int from, int to);
  void Connect(int from, int to, const char *caption);
  void SubmitRegistration();
  void SelectLocation();
  void ShowError(const char *message, UIObject *field);
  void CloseError();
  void UpdateProgress(float dt);
  void Finish();

  UIObject *popup;
  UIObject *steps[8];
  UIObject *connectTo;
  UIObject *errorFocus;
  UIButtonTextField *input;
  UIButtonTextField *username;
  UIButtonTextField *password;
  UIButtonTextField *confirmation;
  UIObject *progressFill;
  UITextPoint *progressText;
  int step;
  int pendingStep;
  int progressPercent;
  float elapsed;
  float stepAge;
};

} // namespace HDUI
#endif
