
#ifndef _included_dictionaryhacker_h
#define _included_dictionaryhacker_h


#include "vanbakel.h"

#include "interface/taskmanager/uplinktask.h"


#define DICTIONARYHACKER_WAITING		0
#define DICTIONARYHACKER_INPROGRESS		1


class DictionaryHacker : public UplinkTask  
{

protected:

	int status;
	int currentindex;
	char *username;
	char *password;

	float numticksrequired;
	int progress;
	bool completed, failed;

protected:

	static void DictionaryHackerDraw ( Button *button, bool highlighted, bool clicked );	
	static void DictionaryHackerClick ( Button *button );

	static void CloseClick ( Button *button );

public:

	DictionaryHacker();

	virtual ~DictionaryHacker();

	int GetStatus () const { return status; }
	int GetCurrentIndex () const { return currentindex; }
	int GetProgress () const { return status ? progress : 0; }
	float GetTicksRequired () const { return status ? numticksrequired : 0; }
	bool HasCompleted () const { return completed; }
	bool HasFailed () const { return failed; }

	void SetTarget ( UplinkObject *uo, char *uos, int uoi );				

	void Initialise ();     // Called at creation time, neccisary
	void Tick ( int n );           // n ticks occur
	
	void CreateInterface ();       
	void RemoveInterface ();
	void ShowInterface ();

	bool IsInterfaceVisible ();     

};

#endif

