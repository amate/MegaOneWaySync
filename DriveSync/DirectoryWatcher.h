/**
*	@file	DirectoryWatcher.h
*	@brief	フォルダ内のファイルの追加を監視します
*/

#pragma once

#include <thread>
#include <functional>
#include <list>
#include <map>
#include <Windows.h>
#include "DirectoryChanges.h"


struct ChangeInfo {
	DWORD	action;
	CString name;

	ChangeInfo(DWORD a, const CString& n) : action(a), name(n) {}
};



class CDirectoryWatcher : public CDirectoryChangeHandler
{
public:
	enum { 
		kTimeOutMilisecond = 5000, 
	};

	static void	Init()
	{
		s_watcher = new CDirectoryChangeWatcher(false);
	}

	static void Term()
	{
		s_watcher->UnwatchAllDirectories();
		delete s_watcher;
		s_watcher = nullptr;
	}

	static void SetMainWindowHandle(HWND hWnd) { s_hWndMainDlg = hWnd; }

	static void TimerCallback(UINT_PTR nIDEvent)
	{
		auto itfound = s_mapTimercallback.find((void*)nIDEvent);
		if (itfound != s_mapTimercallback.end()) {
			itfound->second();
		} else {
			ATLASSERT(FALSE);
		}
	}

	CDirectoryWatcher() : m_bTimerStarted(false) {}
	~CDirectoryWatcher() { StopWatchDirectory(); }

	/// コールバック関数を登録する  (コールバック関数は別スレッドから呼ばれることに注意！)
	void	SetCallbackFunction(std::function<void (std::list<ChangeInfo>&)> func) {
		m_funcCallback = func;
	}

	/// 監視を開始する
	bool	WatchDirectory(const CString& DirPath)
	{
		m_DirPath = DirPath;
		if (m_DirPath[m_DirPath.GetLength() - 1] != L'\\')
			m_DirPath += L'\\';

		enum  {
			kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
							FILE_NOTIFY_CHANGE_DIR_NAME |
							FILE_NOTIFY_CHANGE_LAST_WRITE |
							FILE_NOTIFY_CHANGE_CREATION
		};
		DWORD ret = s_watcher->WatchDirectory(DirPath, kNotifyFilter, this, true);
		ATLASSERT(ret == ERROR_SUCCESS);
		s_mapTimercallback.insert(std::make_pair((void*)this, [this]() {
			CCritSecLock lock(m_csChangeInfoList);
			KillTimer(s_hWndMainDlg, (UINT_PTR)this);
			m_bTimerStarted = false;

			std::thread([this]() {
				CCritSecLock lock(m_csChangeInfoList);
				if (m_changeInfoList.size()) {
					m_funcCallback(m_changeInfoList);
					m_changeInfoList.clear();
				}
			}).detach();
		}));
		return ret == ERROR_SUCCESS;
	}

	/// 監視を終わらせる
	void	StopWatchDirectory()
	{
		if (s_watcher && m_DirPath.GetLength()) {
			s_watcher->UnwatchDirectory(m_DirPath);
			KillTimer(s_hWndMainDlg, (UINT_PTR)this);
			s_mapTimercallback.erase((void*)this);
		}
	}

	bool On_FilterNotification(DWORD dwNotifyAction, LPCTSTR szFileName, LPCTSTR szNewFileName) override
	{
		CCritSecLock lock(m_csChangeInfoList);
		if (dwNotifyAction == FILE_ACTION_RENAMED_OLD_NAME) {
			m_changeInfoList.emplace_back(FILE_ACTION_RENAMED_OLD_NAME, &szFileName[m_DirPath.GetLength()]);
			m_changeInfoList.emplace_back(FILE_ACTION_RENAMED_NEW_NAME, &szNewFileName[m_DirPath.GetLength()]);
		} else {
			m_changeInfoList.emplace_back(dwNotifyAction, &szFileName[m_DirPath.GetLength()]);
		}
		if (m_bTimerStarted == false) {
			SetTimer(s_hWndMainDlg, (UINT_PTR)this, kTimeOutMilisecond, nullptr);
			m_bTimerStarted = true;
		}
		return true;
	}


private:

	// Data members
	static CDirectoryChangeWatcher*	s_watcher;
	static HWND	s_hWndMainDlg;
	static std::map<void*, std::function<void()>>	s_mapTimercallback;

	std::function<void (std::list<ChangeInfo>&)>	m_funcCallback;
	CString		m_DirPath;
	std::list<ChangeInfo>	m_changeInfoList;
	CCriticalSection	m_csChangeInfoList;
	bool		m_bTimerStarted;
};

__declspec(selectany) CDirectoryChangeWatcher* CDirectoryWatcher::s_watcher = nullptr;
__declspec(selectany) HWND	CDirectoryWatcher::s_hWndMainDlg = NULL;
__declspec(selectany) std::map<void*, std::function<void()>>	CDirectoryWatcher::s_mapTimercallback;













