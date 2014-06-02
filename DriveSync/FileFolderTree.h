/**
*	@file	FileFolderTree.h
*/

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <time.h>
#include "DirectoryWatcher.h"

struct FileFolderData
{
	std::wstring name;	// file or folder name
	time_t		 lastWriteTime;

	// folder
	std::unique_ptr<std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>>	pmapFolderContents;

	FileFolderData(const wchar_t* name = L"", time_t lwt = 0) : name(name), lastWriteTime(lwt) {}
};

struct FileFolderChangeInfo
{
	enum Action {
		kItemAddFile,
		kItemAddFolder,
		kItemDelete,
		kItemMove,
		kItemRename,
		kItemModify,
	};

	Action	action;
	std::wstring path;
	std::wstring path2;	// valid kItemMove or kItemRename

	FileFolderChangeInfo(Action action, const std::wstring& path, const std::wstring& path2 = L"") :
		action(action), path(path), path2(path2) {}
};


class CFileFolderTree
{
public:
	void SetFileFolderChangeCallback(std::function<void(const std::list<FileFolderChangeInfo>&)> func) {
		m_funcFileFolderChangeCallback = func; 
	}

	std::wstring	GetRootFolderPath() const { return m_rootFolderPath; }
	const FileFolderData&	GetRootFolderData() const {	return m_rootFolderData; }
	FileFolderData*	GetFolderData(const std::wstring& subFolderPath);
	std::unique_lock<std::recursive_mutex>	GetTreeLock() {
		return std::unique_lock<std::recursive_mutex>(m_treeMutex);
	}

	bool	BuildRootFolderTree(const std::wstring& rootFolder, std::atomic_bool& bActive);
	void	ClearRootFolderTree();

private:
	void	_RootFolderChangeCallback(const std::list<ChangeInfo>& changeList);
	std::list<FileFolderChangeInfo>	_ConvertChangeInfoFromChangeList(const std::list<ChangeInfo>& changeList);

	std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>* GetFolderContentsMap(const std::wstring& folderPath);

	// Data members
	std::recursive_mutex		m_treeMutex;
	std::wstring	m_rootFolderPath;
	FileFolderData	m_rootFolderData;
	CDirectoryWatcher	m_rootFolderWatcher;

	struct SymLinkData {
		//std::wstring folderPath;
		CDirectoryWatcher	watcher;
		std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>*	pmapFolderContents;
	};

	std::unordered_map<std::wstring, std::unique_ptr<SymLinkData>>	m_mapSymlinkPathWatcher;

	std::function<void(const std::list<FileFolderChangeInfo>&)>	m_funcFileFolderChangeCallback;

};


























