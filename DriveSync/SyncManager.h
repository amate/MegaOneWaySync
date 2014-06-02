/**
*	@file	SyncManager.h
*/

#pragma once

#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <atlsync.h>
#include <boost\multi_index_container.hpp>
#include <boost\multi_index\sequenced_index.hpp>
#include <boost\multi_index\hashed_index.hpp>
#include <boost\multi_index\identity.hpp>
#include "FileFolderTree.h"
#include "DirectoryWatcher.h"
#include "mega\megaclient.h"

using namespace boost::multi_index;

class CSyncManager
{
	struct seq {}; // 挿入順のタグ
	struct path {};
	typedef boost::multi_index_container <
		std::wstring,
		indexed_by<
		sequenced<tag<seq>>,	// 挿入順
		hashed_unique<tag<path>, boost::multi_index::identity<std::wstring>>	// set
		>
	> QuePathContainer;

public:
	CSyncManager();
	~CSyncManager();

	void	SetMainWindowHandle(HWND hWnd) { m_hWndMainDlg = hWnd; }

	bool	SyncStart(const CString& syncFolderPath);
	void	SyncStop();

	bool	IsSyncing() const { return m_bSyncing; }

	bool	DownloadStart(const CString& megaPath, const CString& DLFolderPath);
	void	DownloadStop();

	bool	IsDownloading() const { return m_bDownloading; }

	void	SetDLCompleteCallback(std::function<void ()> func) { m_funcDLComplete = func; }

private:
	void	_AddCreateFolderQue(const std::wstring& parnetFolderPath,
		std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>* pFolderContents);

	void	_ProcessCreateFolderQue();
	void	_ProcessCreatedFolderQue();
	void	_ProcessUploadFileQue(QuePathContainer& fileUpQue);

	void	_FileFolderChangeCallback(const std::list<FileFolderChangeInfo>& changeInfo);

	std::wstring _MegaPathfromRootPath(const std::wstring& path);

	void	_DownloadProcess();

	// Data members
	CFileFolderTree	m_rootFolderTree;
	bool	m_bSyncing;
	bool	m_bDownloading;

	std::thread	m_syncThread;
	std::atomic_bool	m_bSyncActive;

	struct SyncInfo {
		CSyncManager*	syncManager;
		int	fileNewUploadCount;
		int	fileRefreshUploadCount;
		int	folderCreateCount;
		int	fileUploadSkipCount;

		int fileRemoveCount;
		int folderRemoveCount;

		int	fileUploadFailCount;
		int folderCreateFailCount;

		SyncInfo(CSyncManager* syncManager) : syncManager(syncManager), fileNewUploadCount(0), fileRefreshUploadCount(0), folderCreateCount(0), fileUploadSkipCount(0), fileRemoveCount(0), folderRemoveCount(0), fileUploadFailCount(0), folderCreateFailCount(0) {}

		void	Init();
		void	SyncInfoPutLog();
	};
	SyncInfo	m_syncInfo;

	HWND	m_hWndMainDlg;

	std::recursive_mutex	m_queMutex;	// std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	std::unordered_map<std::wstring, bool>	m_processingPath;
	QuePathContainer	m_createFolderQue;
	QuePathContainer	m_createdFolderQue;
	QuePathContainer	m_uploadFileQue;
	QuePathContainer	m_modifyFileQue;
	std::list<std::function<bool ()>>	m_operateQueList;	// キューから消すならtrue、消さないならfalseを返す

	std::list<std::pair<CString, Node*>>	m_downloadQue;
	std::function<void()>	m_funcDLComplete;
};

