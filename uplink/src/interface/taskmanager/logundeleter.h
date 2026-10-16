
#ifndef _included_logundeleter_h
#define _included_logundeleter_h


// ============================================================================

#include "interface/taskmanager/uplinktask.h"

class LogBank;

#define LOGUNDELETER_OFF		    0
#define LOGUNDELETER_INPROGRESS     1
#define LOGUNDELETER_FINISHED		2

// ============================================================================


class LogUnDeleter : public UplinkTask
{

protected:

	LogBank *source;	
	int sourceindex;

	int status;						
	int numticksrequired;
	int progress;					// 0.0 - numticksrequired
	
protected:

	static void BorderDraw    ( Button *button, bool highlighted, bool clicked );
	static void ProgressDraw  ( Button *button, bool highlighted, bool clicked );

	static void CloseClick    ( Button *button );
	static void BorderClick   ( Button *button );

public:

	LogUnDeleter ();
	~LogUnDeleter ();

	int GetStatus () const { return status; }
	int GetProgress () const { return status ? progress : 0; }
	int GetTicksRequired () const { return status ? numticksrequired : 0; }
	int GetSourceIndex () const { return sourceindex; }
	// Borrowed from the model; never retain across a task/model callback.
	const LogBank *GetSource () const { return source; }

	void SetTarget ( UplinkObject *uo, char *uos, int uoi );
	void MoveTo ( int x, int y, int time_ms );				// Centred on top-right of title image

	void Initialise ();     
	void Tick ( int n );    
	
	void CreateInterface ();       
	void RemoveInterface ();
	void ShowInterface ();

	bool IsInterfaceVisible ();     

};



#endif
