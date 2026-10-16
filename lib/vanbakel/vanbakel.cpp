
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "vanbakel.h"
#include "interface.h"

#include "mmgr.h"

// Local protected variables 


local LList <TaskWrapper *> tasklist;          // List of all running tasks
local std::map<int, double> minimumCPUShare;
local std::vector<int> cyclePIDs;
local void (*taskRemovalObserver)(int, void *) = NULL;
local void *taskRemovalOwner = NULL;
local bool resettingTasks = false;

// Local interface variables

local int numregisteredbuttons = 0;            // Number of buttons currently on screen
local int xpos;
local int ypos;

local int nextpid = 0;

void SvbReset ()
{

	resettingTasks = true;
	while ( tasklist.Size () > 0 )
		SvbRemoveTask ( tasklist.GetData (0)->pid );

	tasklist.Empty ();
	minimumCPUShare.clear ();
	cyclePIDs.clear ();
	nextpid = 0;
	resettingTasks = false;

	Svb_initialise_interface ();

}

int SvbRegisterTask ( char *name, Task *task )
{

	// Add the task to the list

	TaskWrapper *newtw = new TaskWrapper;
	newtw->SetPID ( SvbGenerateUniquePID () );
	newtw->SetName ( name );
	newtw->SetTask ( task );
	newtw->SetPriority ( 0.0 );	
	tasklist.PutData ( newtw );

	SvbCalculatePriorities_Add ();	
	SvbUpdateInterface ();

	// Run the task

	const int pid = newtw->pid;
	task->Initialise ();
	// Initialise/Tick callbacks may remove the task and its wrapper.
	task = SvbGetTask (pid);
	if ( task && SvbIsInterfaceVisible () ) task->CreateInterface ();

	return pid;

}

void SvbRemoveTask ( int pid )
{

	int index = SvbLookupIndex ( pid );
	
	double removedpriority;

	if ( tasklist.ValidIndex (index) ) {

		TaskWrapper *taskwrapper = tasklist [index];
		assert ( taskwrapper );

		if ( taskRemovalObserver && !resettingTasks ) taskRemovalObserver (pid, taskRemovalOwner);
		if ( taskwrapper->task ) {
			taskwrapper->task->RemoveInterface ();
			delete taskwrapper->task;
		}

		removedpriority = taskwrapper->priority;
		tasklist.RemoveData ( index );
		minimumCPUShare.erase (pid);

		delete taskwrapper;

		SvbCalculatePriorities_Remove ( removedpriority );

	}

	SvbUpdateInterface ();

}

void SvbRemoveTask ( Task *task )
{

	int pid = SvbLookupPID ( task );

	SvbRemoveTask ( pid );

}

int SvbNumTasks ()
{

	return tasklist.Size ();

}

int SvbGenerateUniquePID ()
{

	++nextpid;
	return nextpid;

}

void SvbCalculatePriorities ()
{

	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );
		tw->SetPriority ( 1.0 / (double) tasklist.Size () );

	}

	SvbCompensatePriorities ();

}

void SvbCalculatePriorities_Add ()
{

	if ( tasklist.Size () <= 0 )
		return;

	double newpriority = 1.0 / (double) tasklist.Size ();

	for ( int i = 0; i < tasklist.Size () - 1; ++i ) {
		assert ( tasklist [i] );
		SvbChangePriority ( i, tasklist [i]->priority * -newpriority, true );
	}
	
	assert ( tasklist [tasklist.Size () - 1] );
	SvbChangePriority ( tasklist.Size () - 1, newpriority, true );

	SvbCompensatePriorities ();

}

void SvbCalculatePriorities_Remove ( double removedpriority )
{

	if ( tasklist.Size () <= 0 )
		return;

	double dP = double ( tasklist.Size () + 1 ) / double ( tasklist.Size () );	

	for ( int i = 0; i < tasklist.Size (); ++i ) {
		assert ( tasklist [i] );
		SvbChangePriority ( i, tasklist [i]->priority * removedpriority * dP, true );
	}

	SvbCompensatePriorities ();

}

void SvbChangePriority ( int index, double amount, bool locked )
{
	if ( !tasklist.ValidIndex (index) || !std::isfinite (amount) ) return;
	TaskWrapper *tw = tasklist [index];
	if ( !tw ) return;
	const double current = tw->priority;
	tw->priority = std::max (0.0, std::min (1.0, current + amount));
	// Manual decreases leave idle CPU, including when this is the only task.
	if ( locked || tw->priority <= current ) return;
	double peers = 0.0;
	for ( int i = 0; i < tasklist.Size (); ++i )
		if ( i != index ) peers += tasklist [i]->priority;
	// Increases use idle CPU first; only the actual excess comes from peers.
	double excess = std::max (0.0, tw->priority + peers - 1.0);
	for ( int i = 0; i < tasklist.Size () && excess > 0.0; ++i ) {
		if ( i == index || tasklist [i]->priority <= 0.0 ) continue;
		TaskWrapper *peer = tasklist [i];
		const double priority = peer->priority;
		const double reduction = std::min (priority, std::min (excess,
			peers > priority ? excess * (priority / peers) : excess));
		peer->priority -= reduction;
		peers -= priority;
		excess -= reduction;
	}
	// Do not let rounding or an exhausted peer budget create extra CPU.
	if ( excess > 0.0 ) tw->priority = std::max (0.0, tw->priority - excess);
}

void SvbCompensatePriorities ()
{
	if ( tasklist.Size () <= 0 ) return;
	double total = 0.0;
	for ( int i = 0; i < tasklist.Size (); ++i ) total += tasklist [i]->priority;
	// Under-allocation is intentional after a manual decrease, not an error.
	if ( total <= 1.0 ) return;
	for ( int i = 0; i < tasklist.Size (); ++i )
		tasklist [i]->priority /= total;
}

void SvbSetMinimumCPUShare ( int pid, double minimum )
{
	if ( SvbGetTask (pid) && std::isfinite (minimum) && minimum >= 0.0 )
		minimumCPUShare[pid] = minimum;
}

double SvbGetMinimumCPUShare ( int pid )
{
	const auto found = minimumCPUShare.find (pid);
	return found != minimumCPUShare.end () ? found->second : 0.0;
}

void SvbSetTaskRemovalObserver ( void (*observer)(int, void *), void *owner )
{
	taskRemovalObserver = observer;
	taskRemovalOwner = owner;
}

void SvbClearTaskRemovalObserver ( void *owner )
{
	if ( taskRemovalOwner != owner ) return;
	taskRemovalObserver = NULL;
	taskRemovalOwner = NULL;
}

void SvbCycle ()
{
	
	SvbCycle ( 1 );

}

void SvbCycle ( int n )
{
	if ( n <= 0 ) return;
	cyclePIDs.clear ();
	double total = 0.0;
	for ( int i = 0; i < tasklist.Size (); ++i ) {
		TaskWrapper *wrapper = tasklist [i];
		cyclePIDs.push_back (wrapper->pid);
		total += wrapper->priority;
	}
	// GOLD HD_App::Update 004AE820 and total-share producer 004AED30:
	// allocations above 90% reduce the common progress budget by 20%.
	const double budget = total > 0.9 ? n + int (n * -0.2) : n;
	for ( size_t index = 0; index < cyclePIDs.size (); ++index ) {
		const int pid = cyclePIDs[index];
		TaskWrapper *wrapper = SvbGetTaskWrapper (pid);
		if ( !wrapper || !wrapper->task ) continue;
		double rate = wrapper->priority * budget;
		const auto minimum = minimumCPUShare.find (pid);
		// 0045F140 stores minimum at +180; 0045F160 stores priority at
		// +184. Preserve 004AE820's literal, surprising greater-than rule.
		if ( minimum != minimumCPUShare.end () && wrapper->priority > minimum->second )
			rate *= 0.5;
		wrapper->progress += rate;
		const int ticks = static_cast<int> (wrapper->progress);
		if ( ticks <= 0 ) continue;
		wrapper->progress -= ticks;
		Task *task = wrapper->task;
		task->Tick (ticks);
		// Tick may delete itself, remove peers or register another PID.
		// Never touch this task/wrapper again; new tasks wait for next cycle.
	}
}

int SvbLookupIndex ( int pid )
{

	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );

		if ( tw->pid == pid )
			return i;

	}

	return -1;	

}

int SvbLookupPID ( Task *task )
{

	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );

		if ( tw->task == task )
			return tw->pid;

	}

	return -1;	

}

Task *SvbGetTask ( int pid )
{
	
	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );

		if ( tw->pid == pid )
			return tw->task;

	}

	return NULL;

}

Task *SvbGetTask ( char *name )
{

    for ( int i = 0; i < tasklist.Size (); ++i ) {

        TaskWrapper *tw = tasklist [i];
        assert (tw);

        if ( strcmp ( tw->name, name ) == 0 )
            return tw->task;

    }

    return NULL;

}

TaskWrapper *SvbGetTaskWrapper ( int pid )
{

	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );

		if ( tw->pid == pid )
			return tw;

	}

	return NULL;
	
}

TaskWrapper *SvbGetTaskWrapperAtIndex ( int index )
{
	
	if ( index < tasklist.Size () ) 
		return tasklist [index];

	else
		return NULL;

}

void SvbCreateInterface ( int x, int y )
{

	xpos = x;
	ypos = y;

	if ( !SvbIsInterfaceVisible () ) {

		// Create a button which covers the scale bar (to ensure it is properly masked when removed)
		EclRegisterButton ( x - 3, y - 4, 106, 11, "", "svb_mask" );
		EclRegisterButtonCallbacks ( "svb_mask", NULL, NULL, NULL, NULL );

		// Create a title bar
		EclRegisterButton ( x, y - 15, 100, 12, "CPU usage", "", "svb_titlebar" );
		EclRegisterButtonCallbacks ( "svb_titlebar", Svb_textbutton_draw, NULL, NULL, NULL );

		// Create the scale bar
		EclRegisterButton ( x, y, 100, 5, "0...........50...........100", "svb_scalebar" );
		EclRegisterButtonCallbacks ( "svb_scalebar", Svb_button_draw, NULL, NULL, NULL );
										  
		// Create the task buttons

		for ( int i = 0; i < SvbNumTasks (); ++i ) {
			
			SvbCreateColumn ( xpos, ypos, tasklist [i], i );
			assert ( tasklist [i]->task );
			tasklist [i]->task->CreateInterface ();

		}
		
		numregisteredbuttons = SvbNumTasks ();

	}

}

void SvbRemoveInterface ()
{

	if ( SvbIsInterfaceVisible () ) {

		// Remove the scale bar
		EclRemoveButton ( "svb_mask" );
		EclRemoveButton ( "svb_titlebar" );
		EclRemoveButton ( "svb_scalebar" );

		// Remove the task buttons
		for ( int i = 0; i < SvbNumTasks (); ++i ) {

			SvbRemoveColumn ( i );
			assert ( tasklist [i]->task );
			tasklist [i]->task->RemoveInterface ();

		}

	}
}

void SvbShowTask ( int pid )
{

	TaskWrapper *tw = SvbGetTaskWrapper ( pid );

	if ( tw ) {

		assert ( tw->task );
		tw->task->CreateInterface ();

	}

}

void SvbHideTask ( int pid )
{

	TaskWrapper *tw = SvbGetTaskWrapper ( pid );

	if ( tw ) {

		assert ( tw->task );
		tw->task->RemoveInterface ();

	}

}

void SvbShowAllTasks ()
{

	for ( int i = 0; i < tasklist.Size (); ++i ) {

		TaskWrapper *tw = tasklist [i];
		assert ( tw );
		assert ( tw->task );

		tw->task->ShowInterface ();
		
	}

}

void SvbUpdateInterface ()
{

	if ( SvbIsInterfaceVisible () ) {

		// Add / remove task buttons as neccisary

		if ( SvbNumTasks () > numregisteredbuttons ) {

			// Add more buttons
			for ( int i = numregisteredbuttons; i < SvbNumTasks (); ++i )
				SvbCreateColumn ( xpos, ypos, tasklist [i], i );

		}
		else if ( SvbNumTasks () < numregisteredbuttons ) {

			// Remove some buttons
			for ( int i = SvbNumTasks (); i < numregisteredbuttons; ++i )
				SvbRemoveColumn ( i );

		}

		numregisteredbuttons = SvbNumTasks ();

		// Resize/update the remaining buttons
		
		for ( int i = 0; i < SvbNumTasks (); ++i ) {

			TaskWrapper *tw = tasklist [i];
			assert ( tw );

			char name [32];
			sprintf ( name, "svb_priority %d", i );

			char caption [32];
			sprintf ( caption, "%s", tw->name );
			//sprintf ( caption, "%f", tw->priority );

			EclRegisterResize ( name, (int)(tw->priority * 100), 10, 500 );
			EclGetButton ( name )->SetCaption ( caption );

		}

	}

}

bool SvbIsInterfaceVisible ()
{

	return ( EclGetButton ( "svb_scalebar" ) != NULL );

}

void SvbDebugPrint ()
{

	printf ( "============== V A N  B A K E L ============================\n" );	

	printf ( "numregisteredbuttons:%d, xpos:%d, ypos:%d, nextpid:%d\n", numregisteredbuttons, xpos, ypos, nextpid );

	for ( int i = 0; i < tasklist.Size (); ++i )
		tasklist.GetData (i)->DebugPrint ();

	printf ( "============== E N D  O F  V A N  B A K E L ================\n" );

}

