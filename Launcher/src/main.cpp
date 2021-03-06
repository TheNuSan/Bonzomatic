#include <stdio.h>
#include <string>
#include <assert.h>
#include <vector>

#include "Network.h"
#include "Instances.h"
#include "ControlWindow.h"
#include "jsonxx.h"

// TMP
#include <windows.h>

// TODO:
// button add to add a coder at runtime
// button remove to remove a coder at runtime
//
// bonzomatic: make mipmaps work so you can make blurry things (may not be ideal with raymarching as we dont have proper derivative)
//
// ideas:
// X if in diaporama and switching to a coder view, continue diaporama from there
// X have a button to kill/relaunch instance of one coder (if they crashed for example)
// X chose better keys so psenough can type in chat ^^
//    -> in fact, issue is that during a diaporama, windows get and loose focus (which prevent you from typing somewhere else) and can give focus back to control window
// - option to reserve last quarter for DJ's cam or something
// X for bonzomatic: sync the timer + have a button to reset them ?
// - verify that bonzomatic does compile if recieving a shader after losing the source for a while ?
/*
m for mosaic view, d for diaporama, then 1 through 0 for the fullscreen view of the person
and arrow keys to go next/previous
when you're in fullscreen of someone
also the standard bonzomatic hotkeys like f12 to show just the shader, if you press them with the controller window on focus it should affect all viewports
and g to toggle the grid view i guess
// ** shortcut: pressing ctrl + 1-0 display coder 11-20
// bonzomatic display mode with big coder names even in no-editor mode
// display fullscreen coder's name somewhere so it can also be chroma keyed

*/

// futur:
// connect to server (local as test first) to get coder name at runtime
// option mode to open new window when a new coder appear
// option mode to close window of inactive coder for more that n seconds
// ask Alkama <3 to change server so you can connect to the room and get all the coders messages

// may need:
// whitelist/blacklist to avoid bad player
// server that reserve coder names so people cannot steel channels
//    ex: server remember ip/connection id of the last person that streamed in a room for n seconds
//        if someone else stream, their messages are not propagated

LARGE_INTEGER LastPCV = { 0 };
double currentTime = 0.0;
double WhatTime()
{
  LARGE_INTEGER count, freq;
  if (!LastPCV.QuadPart) {
    QueryPerformanceCounter(&LastPCV);
  }
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&freq);

  currentTime += (double)(count.QuadPart - LastPCV.QuadPart) / (double)(freq.QuadPart);

  LastPCV = count;

  return currentTime;
}

int main(int argc, const char *argv[])
{
  printf("[LAUNCHER] Started \n");

  std::string configFile = "launcher.json";
  jsonxx::Object options;
  FILE * fConf = fopen(configFile.c_str(), "rb");
  if (fConf)
  {
    printf("Launcher config file found, parsing...\n");

    char szConfig[65535];
    memset(szConfig, 0, 65535);
    fread(szConfig, 1, 65535, fConf);
    fclose(fConf);

    options.parse(szConfig);
  }

  FindDesktopResolution();

  //Network::PrepareConnection();
  //Network::OpenConnection("ws://127.0.0.1:8000/");

  LaunchInstances(options);

  InitControlWindow(options);

  double Time = WhatTime();
  while (!WantsToQuit()) {
    double NewTime = WhatTime();
    float ElapsedTime = NewTime - Time;
    Time = NewTime;
    UpdateControlWindow(ElapsedTime);
    //Network::Tick();
	  //Sleep(30);
  }
  CloseControlWindow();
  

  ReleaseInstances();

  //Network::Release();


  exit(0);
  return 0;
}