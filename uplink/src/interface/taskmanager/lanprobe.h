
/*
  LAN Probe software tool

	Run on a LAN viewer, targetting a specific system
	If the system is only partly visible, it becomes fully visible
	If it is fully visible, then the connecting lines are revealed	

  */

#ifndef _included_lanprobe_h
#define _included_lanprobe_h

#include "eclipse.h"
#include "interface/taskmanager/uplinktask.h"


#define LANPROBE_UNUSED				0
#define LANPROBE_SCANNINGSYSTEM		1
#define LANPROBE_SCANNINGLINKS		2
#define LANPROBE_FINISHED			3

class LanComputer;

// ============================================================================


class LanProbe : public UplinkTask
{

protected:

	int status;
	int progress;
	int numticksrequired;

	LanComputer *comp;
	int systemIndex;

protected:

	static void BorderDraw    ( Button *button, bool highlighted, bool clicked );
	static void ProgressDraw  ( Button *button, bool highlighted, bool clicked );

	static void CloseClick    ( Button *button );
	static void BorderClick   ( Button *button );

public:

	LanProbe();
	~LanProbe();

	int GetStatus () const { return status; }
	int GetProgress () const { return status ? progress : 0; }
	int GetTicksRequired () const { return status ? numticksrequired : 0; }
	// Borrowed from the live world, never retained across model callbacks.
	const LanComputer *GetLANComputer () const { return comp; }
	int GetSystemIndex () const { return systemIndex; }

	void SetTarget ( UplinkObject *uo, char *uos, int uoi );
	void MoveTo ( int x, int y, int time_ms );							// Origin top left		

	void Initialise ();     
	void Tick ( int n );    
	
	void CreateInterface ();       
	void RemoveInterface ();
	void ShowInterface ();

	bool IsInterfaceVisible ();     

};

#endif
