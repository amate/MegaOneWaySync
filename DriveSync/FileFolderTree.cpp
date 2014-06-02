/**
*	@file	FileFolderTree.cpp
*/

#include "stdafx.h"
#include "FileFolderTree.h"
#include <functional>
#include <stack>
#include "Utility.h"
#include "MainDlg.h"

template <class F>
bool ForEachObject_OldShell(const std::wstring& strDirectoryPath, F func)
{
	std::wstring FolderPath = strDirectoryPath;
	if (FolderPath.back() != L'\\')
		FolderPath.push_back(L'\\');
	std::wstring FolderFind = FolderPath;
	FolderFind.push_back(L'*');

	WIN32_FIND_DATAW wfd = {};
	HANDLE h = ::FindFirstFileW(FolderFind.c_str(), &wfd);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	// Now scan the directory
	do {
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// ignore the current and parent directory entries
			if (::lstrcmp(wfd.cFileName, _T(".")) == 0 || ::lstrcmp(wfd.cFileName, _T("..")) == 0)
				continue;

			func(FolderPath, wfd);
		} else {
			func(FolderPath, wfd);
		}
	} while (::FindNextFile(h, &wfd));

	::FindClose(h);

	return true;
}

namespace {

	BOOL	EnablePrivilege(LPCTSTR pszPrivName)
	{
		BOOL fOk = FALSE;
		// Assume function fails    
		HANDLE hToken;
		// Try to open this process's access token    
		if (OpenProcessToken(GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES, &hToken))
		{
			// privilege        
			TOKEN_PRIVILEGES tp = { 1 };

			if (LookupPrivilegeValue(NULL, pszPrivName, &tp.Privileges[0].Luid))
			{
				tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

				AdjustTokenPrivileges(hToken, FALSE, &tp,
					sizeof(tp), NULL, NULL);

				fOk = (GetLastError() == ERROR_SUCCESS);
			}
			CloseHandle(hToken);
		}
		return(fOk);
	}

	void	SeBuckupPrivilegeEnable()
	{
		static bool bInit = false;
		if (bInit)
			return;

		bInit = true;

		LPCTSTR arPrivelegeNames[] = {
			SE_BACKUP_NAME, //	these two are required for the FILE_FLAG_BACKUP_SEMANTICS flag used in the call to 
			SE_RESTORE_NAME,//  CreateFile() to open the directory handle for ReadDirectoryChangesW

			SE_CHANGE_NOTIFY_NAME //just to make sure...it's on by default for all users.
			//<others here as needed>
		};

		for (LPCTSTR privilegeName : arPrivelegeNames) {
			EnablePrivilege(privilegeName);
		}
	}


	CString GetSymLinkActualPath(const CString& symLinkPath)
	{
		// ディレクトリを開く 
		HANDLE hDir = ::CreateFile(symLinkPath, GENERIC_READ, 0, NULL, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (hDir == INVALID_HANDLE_VALUE) {
			PUTLOG(L"シンボリックリンクを開けません。: %s", (LPCWSTR)symLinkPath);
			return L"";
		}

		typedef struct _REPARSE_DATA_BUFFER {
			ULONG  ReparseTag;
			USHORT ReparseDataLength;
			USHORT Reserved;
			union {
				struct {
					USHORT SubstituteNameOffset;
					USHORT SubstituteNameLength;
					USHORT PrintNameOffset;
					USHORT PrintNameLength;
					ULONG  Flags;
					WCHAR  PathBuffer[1];
				} SymbolicLinkReparseBuffer;
				struct {
					USHORT SubstituteNameOffset;
					USHORT SubstituteNameLength;
					USHORT PrintNameOffset;
					USHORT PrintNameLength;
					WCHAR  PathBuffer[1];
				} MountPointReparseBuffer;
				struct {
					UCHAR DataBuffer[1];
				} GenericReparseBuffer;
			};
		} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

		// デバイス情報を取得する 
		BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
		REPARSE_DATA_BUFFER& ReparseBuffer = (REPARSE_DATA_BUFFER&)buf;
		DWORD dwRet = 0;
		BOOL br = DeviceIoControl(hDir, FSCTL_GET_REPARSE_POINT, NULL, 0, &ReparseBuffer,
			MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dwRet, NULL);
		if (br == 0) {
			CloseHandle(hDir);
			PUTLOG(L"DeviceIoControlに失敗。: %s", (LPCWSTR)symLinkPath);
			return L"";
		}
		CloseHandle(hDir);

		// Microsoft タグがどうかを判定する
		if (IsReparseTagMicrosoft(ReparseBuffer.ReparseTag) && 
			ReparseBuffer.ReparseTag == IO_REPARSE_TAG_SYMLINK)
		{
			int start = ReparseBuffer.SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(WCHAR);
			int length = ReparseBuffer.SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);
			CString actualPath(&ReparseBuffer.SymbolicLinkReparseBuffer.PathBuffer[start], length);
			return actualPath;
		}
		return L"";
	}

}	// namespace


////////////////////////////////////////////////////////////////
// CFileFolderTree


bool	CFileFolderTree::BuildRootFolderTree(const std::wstring& rootFolder, std::atomic_bool& bActive)
{
	if (rootFolder.empty())
		return false;

	m_rootFolderPath = rootFolder;
	if (m_rootFolderPath.back() != L'\\')
		m_rootFolderPath.push_back(L'\\');

	if (::PathIsDirectoryW(rootFolder.c_str()) == FALSE)
		return false;

	SeBuckupPrivilegeEnable();

	// clear
	m_rootFolderData.pmapFolderContents.reset();
	m_mapSymlinkPathWatcher.clear();

	std::stack<std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>*>	stackpTopTree;

	m_rootFolderData.name = GetFileOrFolderName(m_rootFolderPath.c_str());
	m_rootFolderData.pmapFolderContents.reset(
		new std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>);
	stackpTopTree.push(m_rootFolderData.pmapFolderContents.get());
	
	std::function <void (const std::wstring&, const WIN32_FIND_DATAW&)> funcBuildTree;
	funcBuildTree = [&](const std::wstring& parentFolder, const WIN32_FIND_DATAW& wfd) {
		if (bActive == false)
			return;

		auto pData = new FileFolderData(wfd.cFileName, FileTime_to_POSIX(&wfd.ftLastWriteTime));
		stackpTopTree.top()->insert(
			std::make_pair(std::wstring(wfd.cFileName), std::unique_ptr<FileFolderData>(std::move(pData))));
		// フォルダ
		if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			pData->pmapFolderContents.reset(
				new std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>);
			std::wstring folderPath = parentFolder + wfd.cFileName;
			// シンボリックリンク
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
				CString actualPath = GetSymLinkActualPath(folderPath.c_str());
				if (actualPath.GetLength()) {
					std::wstring symLinkPath = folderPath.substr(m_rootFolderPath.length());
					SymLinkData* symLinkData = new SymLinkData;
					//symLinkData->folderPath = folderPath;
					symLinkData->pmapFolderContents = pData->pmapFolderContents.get();
					m_mapSymlinkPathWatcher[symLinkPath].reset(symLinkData);
				}
			}
			
			stackpTopTree.push(pData->pmapFolderContents.get());
			ForEachObject_OldShell(parentFolder + wfd.cFileName, funcBuildTree);
			stackpTopTree.pop();
		}
	};
	ForEachObject_OldShell(m_rootFolderPath, funcBuildTree);

	if (bActive == false)
		return true;

	// 監視を設定
	m_rootFolderWatcher.SetCallbackFunction([this](std::list<ChangeInfo>& changeList) {
		_RootFolderChangeCallback(changeList);
	});
	ATLVERIFY(m_rootFolderWatcher.WatchDirectory(m_rootFolderPath.c_str()));

	for (auto& symLinkWatcher : m_mapSymlinkPathWatcher) {
		std::wstring symLinkPath = symLinkWatcher.first + L"\\";
		symLinkWatcher.second->watcher.SetCallbackFunction(
			[this, symLinkPath](std::list<ChangeInfo>& changeList) {
			for (auto& changeInfo : changeList) {
				changeInfo.name.Insert(0, symLinkPath.c_str());
			}
			_RootFolderChangeCallback(changeList);
		});
		CString symLinkFullPath = (m_rootFolderPath + symLinkPath).c_str();
		ATLVERIFY(symLinkWatcher.second->watcher.WatchDirectory(symLinkFullPath));
	}
	return true;
}

void	CFileFolderTree::ClearRootFolderTree()
{
	auto lock = GetTreeLock();
	m_rootFolderPath.clear();

	m_rootFolderWatcher.UnwatchDirectory();
	m_mapSymlinkPathWatcher.clear();

	m_rootFolderData.name.clear();
	m_rootFolderData.lastWriteTime = 0;
	m_rootFolderData.pmapFolderContents.reset();
}


std::list<FileFolderChangeInfo>	
	CFileFolderTree::_ConvertChangeInfoFromChangeList(const std::list<ChangeInfo>& changeList)
{
	std::list<FileFolderChangeInfo>	changeInfoList;

	std::list<CString>	addFolderPathList;
	std::set<std::wstring>	setProcessedFilePath;
	for (auto it = changeList.begin(); it != changeList.end(); ++it) {
		if (it->action == FILE_ACTION_RENAMED_OLD_NAME) {
			auto itnext = std::next(it);
			if (itnext == changeList.end() || itnext->action != FILE_ACTION_RENAMED_NEW_NAME) {
				PUTLOG(L"新規リネームファイル名が存在しません。");
				ATLASSERT(FALSE);
				continue;
			}

			std::wstring newName = (LPCWSTR)GetFileOrFolderName(itnext->name);
			// リネーム
			changeInfoList.emplace_back(FileFolderChangeInfo::kItemRename, (LPCWSTR)it->name, newName);
			it = itnext;
			continue;

		} else if (it->action == FILE_ACTION_REMOVED) {
			auto itnext = std::next(it);
			if (itnext != changeList.end() && itnext->action == FILE_ACTION_ADDED) {
				std::wstring sourceName = (LPCWSTR)GetFileOrFolderName(it->name);
				std::wstring moveName = (LPCWSTR)GetFileOrFolderName(itnext->name);
				if (sourceName == moveName) {	// 同名なら移動とみなす
					CString targetFolder = GetParentFolderPath(itnext->name);
					if (targetFolder.GetLength())
						targetFolder = AddBackslash(targetFolder);

					// 移動
					changeInfoList.emplace_back(FileFolderChangeInfo::kItemMove, (LPCWSTR)it->name, (LPCWSTR)targetFolder);
					it = itnext;
					continue;
				}	// それ以外は単なる追加と、削除
			}

			// 削除
			changeInfoList.emplace_back(FileFolderChangeInfo::kItemDelete, (LPCWSTR)it->name);
			continue;

		} else if (it->action == FILE_ACTION_ADDED) {
			CString fullPath = (m_rootFolderPath + (LPCWSTR)it->name).c_str();
			auto itfound = setProcessedFilePath.find(std::wstring((LPCWSTR)fullPath));
			if (itfound != setProcessedFilePath.end()) {
				continue;	// もう追加されたのでここでは処理しない
			}
			bool bSameFolder = false;
			for (auto& folder : addFolderPathList) {
				if (::wcsncmp(folder, it->name, folder.GetLength()) == 0) {
					bSameFolder = true;
					break;
				}
			}
			if (bSameFolder)
				continue;

			setProcessedFilePath.insert(std::wstring((LPCWSTR)fullPath));
			if (::PathIsDirectory(fullPath)) {
				addFolderPathList.emplace_back(it->name);
				// フォルダの追加
				changeInfoList.emplace_back(FileFolderChangeInfo::kItemAddFolder, (LPCWSTR)it->name);
				continue;

			} else {
				// ファイルの追加
				changeInfoList.emplace_back(FileFolderChangeInfo::kItemAddFile, (LPCWSTR)it->name);
				continue;
			}
		} else if (it->action == FILE_ACTION_MODIFIED) {
			CString fullPath = (m_rootFolderPath + (LPCWSTR)it->name).c_str();
			if (::PathIsDirectory(fullPath)) {
				continue;	// フォルダの更新は無視する
			} else {
				auto itfound = setProcessedFilePath.find(std::wstring((LPCWSTR)fullPath));
				if (itfound != setProcessedFilePath.end()) {
					continue;	// もう追加されたのでここでは処理しない
				}
				bool bSameFolder = false;
				for (auto& folder : addFolderPathList) {
					if (::wcsncmp(folder, it->name, folder.GetLength()) == 0) {
						bSameFolder = true;
						break;
					}
				}
				if (bSameFolder)
					continue;

				setProcessedFilePath.insert(std::wstring(fullPath));

				// 更新
				changeInfoList.emplace_back(FileFolderChangeInfo::kItemModify, (LPCWSTR)it->name);
				continue;
			}
		}
	}
	return changeInfoList;
}


void	CFileFolderTree::_RootFolderChangeCallback(const std::list<ChangeInfo>& changeList)
{
	auto lock = GetTreeLock();
	std::list<FileFolderChangeInfo>	changeInfoList = _ConvertChangeInfoFromChangeList(changeList);
	for (auto& changeInfo : changeInfoList) {
		std::wstring parentFolderPath = (LPCWSTR)GetParentFolderPath(changeInfo.path.c_str());
		auto folderContentsMap = GetFolderContentsMap(parentFolderPath);
		if (folderContentsMap == nullptr) {
			PUTLOG(L"親フォルダが見つかりません。: %s", parentFolderPath.c_str());
			continue;
		}
		switch (changeInfo.action) {
			case FileFolderChangeInfo::kItemAddFile:
			{
				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				std::wstring fullPath = m_rootFolderPath + changeInfo.path;
				auto data = std::make_unique<FileFolderData>(name.c_str(), GetLastWritePosixTime(fullPath));
				folderContentsMap->insert(std::make_pair(name, std::move(data)));
			}
			break;

			case FileFolderChangeInfo::kItemAddFolder:
			{
				
				std::stack<std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>*>	stackpTopTree;

				std::vector<std::pair<std::wstring, SymLinkData*>>	vecSymLink;

				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				if (folderContentsMap->find(name) != folderContentsMap->end()) {
					PUTLOG(L"追加されたフォルダが既に存在しています。: %s", changeInfo.path.c_str());
					continue;
				}

				std::wstring fullPath = m_rootFolderPath + changeInfo.path;
				auto data = std::make_unique<FileFolderData>(name.c_str());
				data->pmapFolderContents.reset(
					new std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>);
				stackpTopTree.push(data->pmapFolderContents.get());
				folderContentsMap->insert(std::make_pair(name, std::move(data)));

				// シンボリックリンクが追加された
				CString actualPath = GetSymLinkActualPath(fullPath.c_str());
				if (actualPath.GetLength()) {
					std::wstring symLinkPath = fullPath.substr(m_rootFolderPath.length());
					SymLinkData* symLinkData = new SymLinkData;
					//symLinkData->folderPath = folderPath;
					//symLinkData->pmapFolderContents = pData->pmapFolderContents.get();
					symLinkData->watcher.SetCallbackFunction(
						[this, symLinkPath](std::list<ChangeInfo>& changeList) {
						for (auto& changeInfo : changeList) {
							changeInfo.name.Insert(0, (symLinkPath + _T("\\")).c_str());
						}
						_RootFolderChangeCallback(changeList);
					});
					symLinkData->watcher.WatchDirectory(fullPath.c_str());
					m_mapSymlinkPathWatcher[symLinkPath].reset(symLinkData);
				}

				std::function <void(const std::wstring&, const WIN32_FIND_DATAW&)> funcBuildTree;
				funcBuildTree = [&](const std::wstring& parentFolder, const WIN32_FIND_DATAW& wfd) {
					auto pData = new FileFolderData(wfd.cFileName, FileTime_to_POSIX(&wfd.ftLastWriteTime));
					stackpTopTree.top()->insert(
						std::make_pair(std::wstring(wfd.cFileName), std::unique_ptr<FileFolderData>(std::move(pData))));
					// フォルダ
					if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
						pData->pmapFolderContents.reset(
							new std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>);
						std::wstring folderPath = parentFolder + wfd.cFileName;
						// シンボリックリンク
						if (wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
							CString actualPath = GetSymLinkActualPath(folderPath.c_str());
							if (actualPath.GetLength()) {
								std::wstring symLinkPath = folderPath.substr(m_rootFolderPath.length());
								SymLinkData* symLinkData = new SymLinkData;
								vecSymLink.push_back(std::make_pair(symLinkPath, symLinkData));
								//symLinkData->folderPath = folderPath;
								symLinkData->pmapFolderContents = pData->pmapFolderContents.get();
								//m_mapSymlinkPathWatcher[symLinkPath].reset(symLinkData);
							}
						}

						stackpTopTree.push(pData->pmapFolderContents.get());
						ForEachObject_OldShell(parentFolder + wfd.cFileName, funcBuildTree);
						stackpTopTree.pop();
					}
				};
				ForEachObject_OldShell(fullPath, funcBuildTree);

				for (auto& symData : vecSymLink) {
					m_mapSymlinkPathWatcher[symData.first].reset(symData.second);
				}
			}
			break;

			case FileFolderChangeInfo::kItemDelete:
			{
				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				auto itfound = folderContentsMap->find(name);
				if (itfound == folderContentsMap->end()) {
					PUTLOG(L"削除されたファイル/フォルダがツリーから見つかりません。: %s", changeInfo.path.c_str());
					continue;
				}
				folderContentsMap->erase(itfound);

				auto itsymLink = m_mapSymlinkPathWatcher.find(changeInfo.path);
				if (itsymLink != m_mapSymlinkPathWatcher.end()) {
					m_mapSymlinkPathWatcher.erase(itsymLink);
				}
			}
			break;

			case FileFolderChangeInfo::kItemMove:
			{
				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				auto itfound = folderContentsMap->find(name);
				if (itfound == folderContentsMap->end()) {
					PUTLOG(L"移動されたファイル/フォルダがツリーから見つかりません。: %s", changeInfo.path.c_str());
					continue;
				}
				auto targetFolderContentsMap = GetFolderContentsMap(changeInfo.path2);
				if (targetFolderContentsMap == nullptr) {
					PUTLOG(L"移動先のフォルダがツリーに存在しません。: %s", changeInfo.path2.c_str());
					continue;
				}

				std::unique_ptr<FileFolderData> srcData(itfound->second.release());
				folderContentsMap->erase(itfound);

				targetFolderContentsMap->insert(std::make_pair(srcData->name, std::move(srcData)));

				auto itsymLink = m_mapSymlinkPathWatcher.find(changeInfo.path);
				if (itsymLink != m_mapSymlinkPathWatcher.end()) {
					std::unique_ptr<SymLinkData> symLinkData(itsymLink->second.release());
					m_mapSymlinkPathWatcher.erase(itsymLink);
					std::wstring symLinkPath = LPCWSTR(AddBackslash(changeInfo.path2.c_str()) + name.c_str());
					symLinkData->watcher.SetCallbackFunction(
						[this, symLinkPath](std::list<ChangeInfo>& changeList) {
						for (auto& changeInfo : changeList) {
							changeInfo.name.Insert(0, symLinkPath.c_str());
						}
						_RootFolderChangeCallback(changeList);
					});
					m_mapSymlinkPathWatcher[symLinkPath].reset(symLinkData.release());
				}
			}
			break;

			case FileFolderChangeInfo::kItemRename:
			{
				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				auto itfound = folderContentsMap->find(name);
				if (itfound == folderContentsMap->end()) {
					PUTLOG(L"リネーム前のファイル/フォルダがツリーから見つかりません。: %s", changeInfo.path.c_str());
					continue;
				}

				std::unique_ptr<FileFolderData> srcData(itfound->second.release());
				srcData->name = changeInfo.path2;
				folderContentsMap->erase(itfound);
				folderContentsMap->insert(std::make_pair(srcData->name, std::move(srcData)));

				auto itsymLink = m_mapSymlinkPathWatcher.find(changeInfo.path);
				if (itsymLink != m_mapSymlinkPathWatcher.end()) {
					std::unique_ptr<SymLinkData> symLinkData(itsymLink->second.release());
					m_mapSymlinkPathWatcher.erase(itsymLink);
					std::wstring symLinkPath = 
						LPCWSTR(AddBackslash(parentFolderPath.c_str()) + changeInfo.path2.c_str());
					m_mapSymlinkPathWatcher[symLinkPath].reset(symLinkData.release());
				}
			}
			break;

			case FileFolderChangeInfo::kItemModify:
			{
				std::wstring name = GetFileOrFolderName(changeInfo.path.c_str());
				auto itfound = folderContentsMap->find(name);
				if (itfound == folderContentsMap->end()) {
					PUTLOG(L"更新されたファイル/フォルダがツリーから見つかりません。: %s", changeInfo.path.c_str());
					continue;
				}
				std::wstring fullPath = m_rootFolderPath + changeInfo.path;
				itfound->second->lastWriteTime = GetLastWritePosixTime(fullPath);
			}
			break;
		}
	}	// switch
	lock.unlock();
	ATLASSERT(m_funcFileFolderChangeCallback);
	m_funcFileFolderChangeCallback(changeInfoList);	// notify
}


std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>* 
	CFileFolderTree::GetFolderContentsMap(const std::wstring& folderPath)
{
	auto vecPartPath = SeparatePathUTF16(folderPath);
	std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>* 
		pmap = m_rootFolderData.pmapFolderContents.get();
	for (auto& partPath : vecPartPath) {
		auto itfound = pmap->find(partPath);
		if (itfound == pmap->end())
			return nullptr;
		pmap = itfound->second->pmapFolderContents.get();
		ATLASSERT(pmap);
		if (pmap == nullptr)
			return nullptr;
	}
	return pmap;
}


FileFolderData*	CFileFolderTree::GetFolderData(const std::wstring& subFolderPath)
{
	std::vector<std::wstring> partPath = SeparatePathUTF16(subFolderPath);
	if (partPath.empty())
		return &m_rootFolderData;

	FileFolderData*	pFolderData = &m_rootFolderData;
	for (auto it = partPath.begin(); it != partPath.end(); ++it) {
		bool bFolderFound = false;
		auto itfound = pFolderData->pmapFolderContents->find(*it);
		if (itfound == pFolderData->pmapFolderContents->end())
			return nullptr;
		pFolderData = itfound->second.get();
		ATLASSERT(pFolderData->pmapFolderContents);
	}
	return pFolderData;
}




















