// DriveSync.cpp : main source file for DriveSync.exe
//

#include "stdafx.h"

#include <atomic>
#include <thread>
#include <locale.h>

#include "MainDlg.h"
#include "MegaAppImpl.h"

// グローバル変数
CAppModule _Module;
std::atomic_bool	g_atomicMegaThreadActive;

int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT)
{
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainDlg dlgMain;

	if(dlgMain.Create(NULL) == NULL)
	{
		ATLTRACE(_T("Main dialog creation failed!\n"));
		return 0;
	}

	//dlgMain.ShowWindow(nCmdShow);

	int nRet = theLoop.Run();

	_Module.RemoveMessageLoop();
	return nRet;
}

int main();

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
#ifdef _DEBUG	// ATLTRACEで日本語を使うために必要
	_tsetlocale(LC_ALL, _T("japanese"));
#endif

	HRESULT hRes = ::CoInitialize(NULL);
// If you are running on NT 4.0 or higher you can use the following call instead to 
// make the EXE free threaded. This means that calls come in on a random RPC thread.
//	HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
	ATLASSERT(SUCCEEDED(hRes));

	// this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
	::DefWindowProc(NULL, 0, 0, 0L);

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	FILE* fp_out = freopen("result.txt", "w", stdout);

	g_atomicMegaThreadActive.store(true);
	std::thread threadMegaLoop(CMegaAppImpl::StartMegaLoop);

	int nRet = Run(lpstrCmdLine, nCmdShow);

	g_atomicMegaThreadActive.store(false);
	threadMegaLoop.join();

	fclose(fp_out);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
