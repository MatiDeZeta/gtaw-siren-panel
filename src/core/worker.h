#pragma once

#include <windows.h>

// The whole plugin runs on this one thread. Nothing is hooked, nothing is patched and no game
// code is touched: it connects to the local interface, installs the panel and then checks
// every few seconds that it is still there.
DWORD WINAPI HudWorker(LPVOID);

// Created before the thread starts so a shutdown can never race it.
bool createStopEvent();
void requestStop();
