
#ifndef _included_passwordbreaker_h
#define _included_passwordbreaker_h


#include "vanbakel.h"

#include "interface/taskmanager/uplinktask.h"
#include "world/date.h"


class PasswordBreaker : public UplinkTask  
{

protected:

	int length, difficulty, currentchar;
	char *caption;
	char *username;
	char *password;
	bool *found;

	int numticksrequired;						// For next letter
	int progress;
	bool completed;

protected:

	static void BackgroundDraw ( Button *button, bool highlighted, bool clicked );

	static void PasswordDraw ( Button *button, bool highlighted, bool clicked );
	static void PasswordClick ( Button *button );

	static void CloseClick ( Button *button );

public:

	PasswordBreaker();

	virtual ~PasswordBreaker();

	int GetLength () const { return length; }
	int GetCurrentCharacter () const { return currentchar; }
	int GetProgress () const { return length > 0 ? progress : 0; }
	int GetTicksRequired () const { return length > 0 ? numticksrequired : 0; }
	const char *GetCaption () const { return caption ? caption : ""; }
	bool HasCompleted () const { return completed; }

	void SetTarget ( UplinkObject *uo, char *uos, int uoi );				

	void Initialise ();     // Called at creation time, neccisary
	void Tick ( int n );           // n ticks occur
	
	void CreateInterface ();       
	void RemoveInterface ();
	void ShowInterface ();

	bool IsInterfaceVisible ();     

};

#endif

