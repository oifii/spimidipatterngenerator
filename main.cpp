#define STRICT 1 
#include <windows.h>
#include <iostream>
using namespace std;

#include "portmidi.h"

#include <string>
#include <map>
#include <assert.h>

//The event signaled when the app should be terminated.
HANDLE g_hTerminateEvent = NULL;
//Handles events that would normally terminate a console application. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType);

//global variables and function
UINT global_TimerId=0;
PmStream* global_pPmStream = NULL; // midi output
DWORD global_dwStartTime_ms;
float global_loopduration_s;
int Terminate();

int outputmidideviceid =  13; 
map<string,int> global_outputmididevicemap;
string outputmididevicename = "Out To MIDI Yoke:  1"; //"Out To MIDI Yoke:  1", "Out To MIDI Yoke:  2", ... , "Out To MIDI Yoke:  8"


VOID CALLBACK TimerProc(HWND hWnd, UINT nMsg, UINT nIDEvent, DWORD dwTime) 
{
	//cout << "Time: " << dwTime << '\n';
	//cout.flush();
	float totalduration_s = (dwTime-global_dwStartTime_ms)/1000.0;
	if(global_loopduration_s>0.0f && totalduration_s>global_loopduration_s)
	{	
		KillTimer(NULL, global_TimerId);
		global_TimerId = 0;
		//exit(0);
        // Tell the main thread to exit the app 
        //::SetEvent(g_hTerminateEvent);
		HWND hWnd = ::GetConsoleWindow( ) ;
		::PostMessage(hWnd, WM_CLOSE, 0, 0 ) ;
	}
	else
	{
		cout << "Time: " << totalduration_s << " sec" << '\n';
		cout.flush();
	}
}


int main(int argc, char **argv) 
{
	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "begin.bat", "", NULL, nShowCmd);

	string midieventpattern = "10001000111100001111111111000000*000*00011*100001*1*1*1111000000";
	float patternduration_s = midieventpattern.size()*0.125; //0.25s per unit
	global_loopduration_s = 30.0f; //total loop duration, if negative will loop indefinitely
	//int outputmididevice = 12; //device id 12, for "Microsoft GS Wavetable Synth", when 8 yoke installed for spi
	//int outputmididevice = 13; //device id 13, for "Out To MIDI Yoke:  1", when 8 yoke installed for spi
	int outputmidichannel = 1; 
	if(argc>1)
	{
		//midieventpattern
		midieventpattern = argv[1];
	}
	if(argc>2)
	{
		//pattern duration in seconds
		patternduration_s = atof(argv[2]);
	}
	if(argc>3)
	{
		//loop duration in seconds
		global_loopduration_s = atof(argv[3]);
	}
	if(argc>4)
	{
		//outputmididevice
		//outputmididevice = atoi(argv[4]);
		outputmididevicename = argv[4];
	}
	if(argc>5)
	{
		//output midi channel
		outputmidichannel = atoi(argv[5]);
	}

	if(midieventpattern.size()<2)
	{
		cout << "invalid pattern, pattern must be at least 2 in size, i.e. 10 or 01 etc." << endl;
		Terminate();
	}
	
	if(patternduration_s<=0)
	{
		cout << "invalid pattern duration, duration must be positive." << endl;
		Terminate();
	}
	if(outputmidichannel<0 || outputmidichannel>15)
	{
		cout << "invalid outputmidichannel, midi channel must range from 0 to 15 inclusively." << endl;
		Terminate();
	}
	
	//Auto-reset, initially non-signaled event 
    g_hTerminateEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //Add the break handler
    ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);


	/////////////////////////
	//portmidi initialization
	/////////////////////////
    PmError err;
	Pm_Initialize();

	//////////////////////////////
	//output midi device selection
	//////////////////////////////
	const PmDeviceInfo* deviceInfo;
    int numDevices = Pm_CountDevices();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pm_GetDeviceInfo( i );
		if (deviceInfo->output)
		{
			string devicenamestring = deviceInfo->name;
			global_outputmididevicemap.insert(pair<string,int>(devicenamestring,i));
		}
	}
	map<string,int>::iterator it;
	it = global_outputmididevicemap.find(outputmididevicename);
	if(it!=global_outputmididevicemap.end())
	{
		outputmidideviceid = (*it).second;
		printf("%s maps to %d\n", outputmididevicename.c_str(), outputmidideviceid);
		deviceInfo = Pm_GetDeviceInfo(outputmidideviceid);
	}
	else
	{
		assert(false);
		for(it=global_outputmididevicemap.begin(); it!=global_outputmididevicemap.end(); it++)
		{
			printf("%s maps to %d\n", (*it).first.c_str(), (*it).second);
		}
		printf("output midi device not found\n");
	}

    // list device information 
    cout << "MIDI output devices:" << endl;
    for (int i = 0; i < Pm_CountDevices(); i++) 
	{
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->output) printf("%d: %s, %s\n", i, info->interf, info->name);
    }
	cout << "device " << outputmidideviceid << " selected" << endl;
    //err = Pm_OpenInput(&midi_in, inp, NULL, 512, NULL, NULL);
    err = Pm_OpenOutput(&global_pPmStream, outputmidideviceid, NULL, 512, NULL, NULL, 0); //0 latency
    if (err) 
	{
        printf(Pm_GetErrorText(err));
        //Pt_Stop();
		Terminate();
        //mmexit(1);
    }


	/////////////////////////
	//midi message definition
	/////////////////////////
	PmEvent myPmEvent[2];
	//note on
	myPmEvent[0].timestamp = 0;
	myPmEvent[0].message = Pm_Message(0x90+outputmidichannel, 60, 100); //channel 0
	//myPmEvent[0].message = Pm_Message(0x91, 60, 100); //channel 1
	//note off
	myPmEvent[1].timestamp = 0;
	myPmEvent[1].message = Pm_Message(0x90+outputmidichannel, 60, 0); //channel 0
	//myPmEvent[1].message = Pm_Message(0x91, 60, 0); //channel 1

	PmEvent tempPmEvent;

	//////////////////
	//loop the pattern
	//////////////////
	float timerelapse_s = 1.0f*patternduration_s/midieventpattern.size();
	UINT timerelapse_ms = (UINT)(timerelapse_s*1000);
	string patterncode;
	int Counter=0;
	MSG Msg;
	global_dwStartTime_ms = GetTickCount(); //log start time
	//global_TimerId = SetTimer(NULL, 1, 1000, &TimerProc); //SetTimer(NULL, 0, 500, &TimerProc);
	global_TimerId = SetTimer(NULL, 1, timerelapse_ms, &TimerProc);
	cout << "TimerId: " << global_TimerId << '\n';
	if (!global_TimerId)
		return 16;
	PmEvent* prev_pPmEvent = NULL;
	while (GetMessage(&Msg, NULL, 0, 0)) 
	{
		++Counter;
		if (Msg.message == WM_TIMER)
		{
			cout << "Counter: " << Counter << "; timer message\n";
			if(prev_pPmEvent)
			{
				//i.e. note off
				//Pm_Write(global_pPmStream, &(myPmEvent[1]), 1);
				PmEvent thisPmEvent;
				thisPmEvent.timestamp = 0;
				thisPmEvent.message = Pm_Message(0x90+outputmidichannel, Pm_MessageData1(prev_pPmEvent->message), 0); //note off, note on with velocity 0
				Pm_Write(global_pPmStream, &thisPmEvent, 1);
			}
			patterncode = midieventpattern.substr(Counter-1,1);
			if(patterncode.compare("0")!=0)
			{
				if(patterncode.compare("*")!=0)
				{
					//note on
					Pm_Write(global_pPmStream, &(myPmEvent[0]), 1);
					prev_pPmEvent = &(myPmEvent[0]);
				}
				else
				{
					//random note on
					int random_integer;
					int lowest=1, highest=20;
					int range=(highest-lowest)+1;
					random_integer = lowest+int(range*rand()/(RAND_MAX + 1.0));

					tempPmEvent.timestamp = 0;
					tempPmEvent.message = Pm_Message(0x90+outputmidichannel, 60+random_integer, 100); //note on, channel 0
					Pm_Write(global_pPmStream, &tempPmEvent, 1);
					prev_pPmEvent = &tempPmEvent;
				}
			}
			else
			{
				prev_pPmEvent = NULL;	
			}
			if(Counter==midieventpattern.size()) Counter=0;
		}
		else
		{
			cout << "Counter: " << Counter << "; message: " << Msg.message << '\n';
		}
		DispatchMessage(&Msg);
	}
	Terminate();
	return 0;
}

int Terminate()
{
	//kill timer
	if(global_TimerId) 
	{
		KillTimer(NULL, global_TimerId);
	}
	//terminate portmidi
	Pm_Close(global_pPmStream);
    //Pt_Stop();
    Pm_Terminate();

	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "end.bat", "", NULL, nShowCmd);
	return 0;
}

//Called by the operating system in a separate thread to handle an app-terminating event. 
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT ||
        dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT)
    {
        // CTRL_C_EVENT - Ctrl+C was pressed 
        // CTRL_BREAK_EVENT - Ctrl+Break was pressed 
        // CTRL_CLOSE_EVENT - Console window was closed 
		Terminate();
        // Tell the main thread to exit the app 
        ::SetEvent(g_hTerminateEvent);
        return TRUE;
    }

    //Not an event handled by this function.
    //The only events that should be able to
	//reach this line of code are events that
    //should only be sent to services. 
    return FALSE;
}
