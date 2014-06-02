/**
*	@file SyncManager.cpp
*/


#include "stdafx.h"
#include "SyncManager.h"
#include "Utility.h"
#include "MegaAppImpl.h"
#include "MainDlg.h"
#include "OptionDialog.h"

/////////////////////////////////////////////////////////////////
// CSyncManager

CSyncManager::CSyncManager() : 
	m_bSyncing(false), m_bDownloading(false), m_syncInfo(this)
{
}


CSyncManager::~CSyncManager()
{
	SyncStop();
}

bool	CSyncManager::SyncStart(const CString& syncFolderPath)
{
	if (m_bSyncing) {
		PUTLOG(L"���ɓ������J�n����Ă��܂��B");
		return false;
	}

	if (::PathIsDirectory(syncFolderPath) == FALSE) {
		PUTLOG(L"��������t�H���_�����݂��܂���B: %s", (LPCWSTR)syncFolderPath);
		return false;
	}

	m_bSyncing = true;
	m_bSyncActive = true;

	PUTLOG(L"�������J�n���܂��B: %s", (LPCWSTR)syncFolderPath);

	m_syncThread = std::thread([this, syncFolderPath]() {
		PUTLOG(L"���[�J���t�H���_���������܂�...");
		if (m_rootFolderTree.BuildRootFolderTree((LPCWSTR)syncFolderPath, m_bSyncActive) == false) {
			PUTLOG(L"BuildRootFolderTree�Ɏ��s: %s", m_rootFolderTree.GetRootFolderPath().c_str());
			return ;
		}
		if (m_bSyncActive == false)
			return;

		PUTLOG(L"BuildRootFolderTree�ɐ���");

		m_syncInfo.Init();

		m_createFolderQue.push_back(L"");		// root�t�H���_���L���[�ɒǉ�
		_AddCreateFolderQue(L"", m_rootFolderTree.GetRootFolderData().pmapFolderContents.get());
		if (m_bSyncActive == false)
			return;

		PUTLOG(L"�t�H���_�쐬�L���[�ɓo�^�����B: %d", m_createFolderQue.size());
		
		// �ύX�ʒm�R�[���o�b�N���w��
		m_rootFolderTree.SetFileFolderChangeCallback(
			std::bind(&CSyncManager::_FileFolderChangeCallback, this, std::placeholders::_1) );

		CMegaAppImpl::GetMegaAppInstance()->AddCommandQue([this]() {
			_ProcessCreateFolderQue();
		});
	});
	return true;
}

void	CSyncManager::SyncStop()
{
	if (m_bSyncing == false) {
		PUTLOG(L"�������J�n����Ă��܂���B");
		return;
	}

	PUTLOG(L"�������~���܂��B");

	m_bSyncActive = false;
	ATLASSERT(m_syncThread.joinable());
	m_syncThread.join();

	CMegaAppImpl::GetMegaAppInstance()->AllTransCancel();

	std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	m_rootFolderTree.ClearRootFolderTree();

	m_processingPath.clear();
	m_createFolderQue.clear();
	m_createdFolderQue.clear();
	m_uploadFileQue.clear();
	m_modifyFileQue.clear();
	m_operateQueList.clear();

	m_bSyncing = false;
}


void	CSyncManager::_AddCreateFolderQue(const std::wstring& parnetFolderPath,
	std::unordered_map<std::wstring, std::unique_ptr<FileFolderData>>* pFolderContents)
{
	if (m_bSyncActive == false)
		return;
	for (auto& data : *pFolderContents) {
		if (data.second->pmapFolderContents == nullptr)
			continue;
		std::wstring folder = parnetFolderPath + data.second->name;
		m_createFolderQue.push_back(folder);
		_AddCreateFolderQue(folder + L"\\", data.second->pmapFolderContents.get());
	}
}


void	CSyncManager::_ProcessCreateFolderQue()
{
	if (m_bSyncActive == false)
		return;

	std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	CString title;
	title.Format(_T("FCQ [%d] FCDQ [%d] FUQ [%d]"), 
		m_createFolderQue.size(), m_createdFolderQue.size(), m_uploadFileQue.size());
	SetWindowText(m_hWndMainDlg, title);

	// ����҂��̃L���[�����s����
	std::list<std::list<std::function<bool()>>::iterator> eraseList;
	for (auto it = m_operateQueList.begin(); it != m_operateQueList.end(); ++it) {
		auto& func = *it;
		if (func())
			eraseList.emplace_back(it);
	}
	for (auto& eraseIt : eraseList)
		m_operateQueList.erase(eraseIt);

	if (m_createFolderQue.size()) {
		if (0 < m_processingPath.size())
			return;	// �A�b�v���t�@�C���� 0 �ɂȂ�܂ő҂�

		for (auto it = m_createFolderQue.begin(); 
			it != m_createFolderQue.end(); 
			it = m_createFolderQue.begin() ) 
		{
			std::wstring megaFolderPath = _MegaPathfromRootPath(*it);
			Node* folderNode = CMegaAppImpl::GetMegaAppInstance()->
												FindNodeFromPath(megaFolderPath);
			if (folderNode) {
				m_createdFolderQue.push_back(*it);
				m_createFolderQue.erase(it);				
				continue;
			} else {
				++m_syncInfo.folderCreateCount;
				PUTLOG(L"Mega�Ƀt�H���_���쐬���܂��B: %s", it->c_str());

				std::wstring folderPath = *it;
				if (folderPath.empty())
					folderPath = L"\\";
				m_processingPath.emplace(folderPath, false);

				CMegaAppImpl::GetMegaAppInstance()->CreateFolder(megaFolderPath).then(
					[this, megaFolderPath](NodeCore* newNode) {
					if (newNode == nullptr) {
						++m_syncInfo.folderCreateFailCount;
						m_createFolderQue.erase(m_createFolderQue.begin());
						PUTLOG(L"Mega�ւ̃t�H���_�̍쐬�Ɏ��s�B: %s", megaFolderPath.c_str());
					}
					if (m_bSyncing == false)
						return;

					ATLASSERT(m_processingPath.size() == 1);

					// FileFolderChangeInfo::kItemDelete �ō폜���ꂽ
					if (m_processingPath.begin()->second) {
						CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(newNode);
						m_createFolderQue.erase(m_createFolderQue.begin());
					}
					m_processingPath.clear();
					_ProcessCreateFolderQue();
				});
				return;
			}
		}
	}
	ATLASSERT(m_createFolderQue.empty());
	_ProcessCreatedFolderQue();
}

void	CSyncManager::_ProcessCreatedFolderQue()
{
	std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	if (m_uploadFileQue.size()) {
		_ProcessUploadFileQue(m_uploadFileQue);	// ��ɃA�b�v���[�h�L���[����������
		return;
	}

	for (auto it = m_createdFolderQue.begin(); it != m_createdFolderQue.end();) {
		std::wstring folderPath = m_createdFolderQue.front();
		auto lock = m_rootFolderTree.GetTreeLock();
		auto folderData = m_rootFolderTree.GetFolderData(folderPath);
		if (folderData == nullptr) {
			PUTLOG(L"�t�H���_�[�c���[����t�H���_��������܂���B: %s", folderPath.c_str());
			m_createdFolderQue.erase(it);
			it = m_createdFolderQue.begin();
			continue;
		}
		Node* folderNode = CMegaAppImpl::GetMegaAppInstance()->
											FindNodeFromPath(_MegaPathfromRootPath(folderPath));
		if (folderNode == nullptr) {
			PUTLOG(L"Mega�Ƀt�H���_��������܂���B: %s", folderPath.c_str());
			// ���g���C����
			m_createFolderQue.push_back(folderPath);
			_ProcessCreateFolderQue();
			return;
		}
		if (folderPath.size())
			folderPath.push_back(L'\\');

		// �d���`�F�b�N�p�̃}�b�v���쐬
		std::unordered_map<std::wstring, Node*>	mapMegaFolder;
		for (Node* node : folderNode->children) {
			mapMegaFolder.insert(std::make_pair(ConvertUTF16fromUTF8(node->displayname_string()), node));
		}
		for (auto& data : *folderData->pmapFolderContents) {
			auto itfound = mapMegaFolder.find(data.second->name);
			if (itfound != mapMegaFolder.end()) {	// Mega�t�H���_���Ɋ��ɑ��݂��Ă���
				time_t megafileLastModifyTime = itfound->second->mtime;
				time_t megafileCreateTime = itfound->second->ctime;
				Node* remoteNode = itfound->second;
				mapMegaFolder.erase(itfound);
				if (data.second->lastWriteTime <= megafileLastModifyTime ||
					data.second->lastWriteTime <= megafileCreateTime) {
					++m_syncInfo.fileUploadSkipCount;
					continue;	// �X�V����K�v�͂Ȃ�
				}
			}
			if (data.second->pmapFolderContents)
				continue;

			// �g���q���O�����ׂ�
			std::wstring ext = PathFindExtention(data.second->name);
			if (CConfig::s_setExcludeExt.find(ext) != CConfig::s_setExcludeExt.end())
				continue;
			std::wstring filePath = folderPath + data.second->name;
			m_uploadFileQue.push_back(filePath);	// �L���[�ɒǉ�
		}
		// ���[�J���ɖ����t�@�C��/�t�H���_��Mega����폜����
		int delCount = (int)mapMegaFolder.size();
		for (auto& delTarget : mapMegaFolder) {
			bool bFolder = delTarget.second->type == nodetype::FOLDERNODE;
			PUTLOG(L"�����[�g����%s���폜���܂� : %s", bFolder ? L"�t�H���_�[" : L"�t�@�C��",
				ConvertUTF16fromUTF8(delTarget.second->displayname_string()).c_str());
			if (bFolder)
				++m_syncInfo.folderRemoveCount;
			else
				++m_syncInfo.fileRemoveCount;

			CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(delTarget.second);
		}
		m_createdFolderQue.erase(m_createdFolderQue.begin());
		if (m_uploadFileQue.size()) {
			_ProcessUploadFileQue(m_uploadFileQue);
			return;
		} else {
			it = m_createdFolderQue.begin();
		}
	}

	if (m_processingPath.size()) {	// �L���[���܂��c���Ă���
		return;
	}

	if (m_modifyFileQue.size()) {	// �C���t�@�C���͍Ō�ɏ�������
		_ProcessUploadFileQue(m_modifyFileQue);
		return;
	}

	SetWindowText(m_hWndMainDlg, _T(""));
	m_syncInfo.SyncInfoPutLog();
	m_syncInfo.Init();
}

void	CSyncManager::_ProcessUploadFileQue(QuePathContainer& fileUpQue)
{
	std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	if (fileUpQue.size()) {
		//enum { kMaxUploadFileCount = 8 };
		while (m_processingPath.size() < CConfig::s_nMaxUploadFileCount && fileUpQue.size()) {
			std::wstring filePath = fileUpQue.front();
			CString parentFolderPath = GetParentFolderPath(filePath.c_str());
			Node* parentNode = CMegaAppImpl::GetMegaAppInstance()->
				FindNodeFromPath(_MegaPathfromRootPath((LPCWSTR)parentFolderPath));
			if (parentNode == nullptr) {
				PUTLOG(L"Mega�Ƀt�H���_��������܂���B: %s", (LPCWSTR)parentFolderPath);
				// ���g���C����
				m_createFolderQue.push_back(std::wstring((LPCWSTR)parentFolderPath));
				_ProcessCreateFolderQue();
				return;
			}
			auto funcCallback = [this](const std::wstring& filePath) {
				if (m_bSyncing == false)
					return;

				std::wstring path = filePath.substr(m_rootFolderTree.GetRootFolderPath().length());
				auto itfound = m_processingPath.find(path);
				ATLASSERT(itfound != m_processingPath.end());
				// FileFolderChangeInfo::kItemDelete �ō폜���ꂽ
				if (itfound->second) {
					Node* targetNode = CMegaAppImpl::GetMegaAppInstance()->FindNodeFromPath(_MegaPathfromRootPath(filePath));
					if (targetNode == nullptr) {
						PUTLOG(L"Mega�ɍ폜�\��̃t�@�C��������܂���A: %s", filePath.c_str());
					} else {
						CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(targetNode);
					}
				}
				m_processingPath.erase(itfound);
				_ProcessCreateFolderQue();
			};
			std::wstring fileName = (LPCWSTR)GetFileOrFolderName(filePath.c_str());
			std::wstring fullPath = m_rootFolderTree.GetRootFolderPath() + filePath;
			m_processingPath.emplace(filePath, false);

			Node* fileNode = CMegaAppImpl::ChildNodeByName(parentNode, ConvertUTF8fromUTF16(fileName));
			if (fileNode) {
				++m_syncInfo.fileRefreshUploadCount;
				PUTLOG(L"�t�@�C�����X�V���܂��B: %s", filePath.c_str());
				// �X�V
				CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(fileNode).then(
					[this, parentNode, fullPath, funcCallback](){
					CMegaAppImpl::GetMegaAppInstance()->
						AddUploadQue(parentNode->nodehandle, fullPath).then(funcCallback);
				});
			} else {
				++m_syncInfo.fileNewUploadCount;
				PUTLOG(L"�t�@�C�����A�b�v���[�h���܂��B: %s", filePath.c_str());
				CMegaAppImpl::GetMegaAppInstance()->
					AddUploadQue(parentNode->nodehandle, fullPath).then(funcCallback);
			}
			fileUpQue.erase(fileUpQue.begin());
		}
		if (m_processingPath.size() == 0) {
			_ProcessCreateFolderQue();
		}
	} else {
		_ProcessCreateFolderQue();
	}
}

void	CSyncManager::_FileFolderChangeCallback(const std::list<FileFolderChangeInfo>& changeInfo)
{
	CMegaAppImpl::GetMegaAppInstance()->AddCommandQue([this, changeInfo]() {

		auto funcGetTreeFolderData = [this](const std::wstring& folderPath) -> FileFolderData* {
			FileFolderData* pData = m_rootFolderTree.GetFolderData(folderPath);
			if (pData)
				return pData;
			throw std::exception("m_rootFolderTree.GetFolderData : folder not found");
		};
		auto funcAddFolderQue = [this, funcGetTreeFolderData](const std::wstring& path) {
			FileFolderData* pFolderData = funcGetTreeFolderData(path);
			m_createFolderQue.push_back(path);
			_AddCreateFolderQue(path + L"\\", pFolderData->pmapFolderContents.get());
		};

		std::lock_guard<std::recursive_mutex> lock(m_queMutex);
		auto lock2 = m_rootFolderTree.GetTreeLock();
		for (auto& changeData : changeInfo) {
			try {
				switch (changeData.action) {
					case FileFolderChangeInfo::kItemAddFile:
					{
						// ���O�g���q�����ׂ�
						std::wstring ext = PathFindExtention(changeData.path);
						if (CConfig::s_setExcludeExt.find(ext) != CConfig::s_setExcludeExt.end())
							continue;

						std::wstring parentFolder = (LPCWSTR)GetParentFolderPath(changeData.path.c_str());

						// �e�t�H���_���쐬��
						if (m_processingPath.find(parentFolder) != m_processingPath.end())
							continue;
						auto& map = m_createFolderQue.get<path>();
						auto itfound = map.find(parentFolder);
						if (itfound != map.end())
							continue;

						auto& map2 = m_createdFolderQue.get<path>();
						auto itfound2 = map2.find(parentFolder);
						if (itfound2 != map2.end())
							continue;

						PUTLOG(L"�t�@�C�����ǉ�����܂����B: %s", changeData.path.c_str());
						auto& map3 = m_uploadFileQue.get<path>();
						map3.insert(changeData.path);
					}
					break;

					case FileFolderChangeInfo::kItemAddFolder:
					{
						PUTLOG(L"�t�H���_���ǉ�����܂����B: %s", changeData.path.c_str());
						funcAddFolderQue(changeData.path);
					}
					break;

					case FileFolderChangeInfo::kItemDelete:
					{
						auto itprocess = m_processingPath.find(changeData.path);
						if (itprocess != m_processingPath.end()) {
							itprocess->second = true;
							std::wstring fullPath = m_rootFolderTree.GetRootFolderPath() + changeData.path;
							if (CMegaAppImpl::GetMegaAppInstance()->UploadFileCancel(fullPath)) {
								PUTLOG(L"�t�@�C�����폜���ꂽ�̂ŃA�b�v���[�h���L�����Z�����܂��B: %s", changeData.path.c_str());
							} else {
								PUTLOG(L"�t�H���_���폜����܂����B: %s", changeData.path.c_str());
							}							
							continue;
						}

						std::wstring parentFolder = (LPCWSTR)GetParentFolderPath(changeData.path.c_str());
						std::wstring name = (LPCWSTR)GetFileOrFolderName(changeData.path.c_str());

						auto& map = m_createFolderQue.get<path>();
						auto itfound = map.find(changeData.path);
						if (itfound != map.end()) {
							map.erase(itfound);	// �t�H���_�쐬�L���[����폜
							PUTLOG(L"�t�H���_���폜���ꂽ�̂ō쐬�L���[����폜���܂��B: %s", changeData.path.c_str());
							continue;
						}

						auto& map2 = m_createdFolderQue.get<path>();
						auto itfound2 = map2.find(changeData.path);
						if (itfound2 != map2.end()) {
							map2.erase(itfound2);
						}

						auto& map3 = m_uploadFileQue.get<path>();
						auto itfound3 = map3.find(changeData.path);
						if (itfound3 != map3.end()) {
							map3.erase(itfound3);	// �A�b�v���[�h�L���[����폜
							PUTLOG(L"�t�@�C�����폜���ꂽ�̂ŃA�b�v���[�h�L���[����폜���܂��B: %s", changeData.path.c_str());
							continue;
						}

						auto& map4 = m_modifyFileQue.get<path>();
						auto itfound4 = map4.find(changeData.path);
						if (itfound4 != map4.end()) {
							map4.erase(itfound4);
							PUTLOG(L"�t�@�C�����폜���ꂽ�̂ŃA�b�v���[�h�C���L���[����폜���܂��B: %s", changeData.path.c_str());
							continue;
						}

						PUTLOG(L"�t�@�C��/�t�H���_���폜����܂����B: %s", changeData.path.c_str());
						// mega����폜
						Node* targetNode = CMegaAppImpl::GetMegaAppInstance()->
							FindNodeFromPath(_MegaPathfromRootPath(changeData.path));
						if (targetNode == nullptr) {
							PUTLOG(L"Mega�ɍ폜�\��̃t�@�C��/�t�H���_�����݂��܂���B: %s", changeData.path.c_str());
							continue;
						}
						CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(targetNode);
					}
					break;

					case FileFolderChangeInfo::kItemMove:
					{
						if (m_processingPath.find(changeData.path) != m_processingPath.end()) {
							m_operateQueList.emplace_back([this, changeData]() -> bool {
								// �������I������̂ňړ������s
								if (m_processingPath.find(changeData.path) == m_processingPath.end()) {
									PUTLOG(L"�t�@�C��/�t�H���_���ړ�����܂����B: %s -> %s", changeData.path.c_str(), changeData.path2.c_str());
									CMegaAppImpl::GetMegaAppInstance()->MoveNode(
										_MegaPathfromRootPath(changeData.path),
										_MegaPathfromRootPath(changeData.path2));
									return true;
								} else {
									return false;
								}
							});
							if (funcGetTreeFolderData(changeData.path2)->pmapFolderContents)
								funcAddFolderQue(changeData.path2);
							continue;
						}

						std::wstring name = (LPCWSTR)GetFileOrFolderName(changeData.path.c_str());

						FileFolderData* pParentFolderData = funcGetTreeFolderData(changeData.path2);
						auto itfound = pParentFolderData->pmapFolderContents->find(name);
						ATLASSERT(itfound != pParentFolderData->pmapFolderContents->end());
						bool bFolder = itfound->second->pmapFolderContents != nullptr;
						if (bFolder) {
							auto& map = m_createFolderQue.get<path>();
							auto itfound = map.find(changeData.path);
							if (itfound != map.end()) {
								map.erase(itfound);	// �t�H���_�쐬�L���[����폜

								funcAddFolderQue(changeData.path2 + name);
								continue;
							}

							auto& map2 = m_createdFolderQue.get<path>();
							auto itfound2 = map2.find(changeData.path);
							if (itfound2 != map2.end()) {
								map2.erase(itfound2);
								Node* moveSrc = CMegaAppImpl::GetMegaAppInstance()->
									FindNodeFromPath(_MegaPathfromRootPath(changeData.path));
								if (moveSrc == nullptr) {
									PUTLOG(L"Mega�Ɉړ����̃t�H���_������܂���B: %s", changeData.path.c_str());
									continue;
								}
								CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(moveSrc);

								funcAddFolderQue(changeData.path2 + name);
								continue;
							}

						} else {
							auto& map = m_uploadFileQue.get<path>();
							auto itfound = map.find(changeData.path);
							if (itfound != map.end()) {
								map.erase(itfound);	// �A�b�v���[�h�L���[����폜
								m_uploadFileQue.push_back(changeData.path2 + name);
								continue;
							}

							auto& map2 = m_modifyFileQue.get<path>();
							auto itfound2 = map2.find(changeData.path);
							if (itfound2 != map2.end()) {
								map2.erase(itfound2);
								m_modifyFileQue.push_back(changeData.path2 + name);
								continue;
							}
						}
						PUTLOG(L"�t�@�C��/�t�H���_���ړ�����܂����B: %s -> %s", changeData.path.c_str(), changeData.path2.c_str());
						CMegaAppImpl::GetMegaAppInstance()->MoveNode(
							_MegaPathfromRootPath(changeData.path),
							_MegaPathfromRootPath(changeData.path2));
					}
					break;

					case FileFolderChangeInfo::kItemRename:
					{
						std::wstring parentFolder = (LPCWSTR)GetParentFolderPath(changeData.path.c_str());
						std::wstring name = changeData.path2;

						FileFolderData* pParentFolderData = funcGetTreeFolderData(parentFolder);
						auto itfound = pParentFolderData->pmapFolderContents->find(name);
						if (itfound == pParentFolderData->pmapFolderContents->end()) {
							PUTLOG(L"���l�[����̃t�@�C��/�t�H���_���c���[���猩����܂���B: %s\\%s", parentFolder.c_str(), name.c_str());
							continue;
						}
						bool bFolder = itfound->second->pmapFolderContents != nullptr;

						if (m_processingPath.find(changeData.path) != m_processingPath.end()) {
							m_operateQueList.emplace_back([this, changeData]() -> bool {
								if (m_processingPath.find(changeData.path) == m_processingPath.end()) {
									PUTLOG(L"�t�@�C��/�t�H���_�����l�[������܂����B: %s -> %s", changeData.path.c_str(), changeData.path2.c_str());
									CMegaAppImpl::GetMegaAppInstance()->RenameNode(
										_MegaPathfromRootPath(changeData.path), changeData.path2);
									return true;
								} else {
									return false;
								}
							});

							if (bFolder)
								funcAddFolderQue(parentFolder + L"\\" + name);
							continue;
						}
						if (bFolder) {
							auto& map = m_createFolderQue.get<path>();
							auto itfound = map.find(changeData.path);
							if (itfound != map.end()) {
								map.erase(itfound);	// �t�H���_�쐬�L���[����폜

								funcAddFolderQue(parentFolder + L"\\" + name);
								continue;
							}

							auto& map2 = m_createdFolderQue.get<path>();
							auto itfound2 = map2.find(changeData.path);
							if (itfound2 != map2.end()) {
								map2.erase(itfound2);

								m_operateQueList.emplace_back([this, changeData]() -> bool {
									CMegaAppImpl::GetMegaAppInstance()->RenameNode(
										_MegaPathfromRootPath(changeData.path), changeData.path2);
									return true;
								});

								funcAddFolderQue(parentFolder + L"\\" + name);
								continue;
							}

						} else {
							auto& map = m_uploadFileQue.get<path>();
							auto itfound = map.find(changeData.path);
							if (itfound != map.end()) {
								map.erase(itfound);	// �A�b�v���[�h�L���[����폜

								m_uploadFileQue.push_back(parentFolder + L"\\" + name);
								continue;
							}

							auto& map2 = m_modifyFileQue.get<path>();
							auto itfound2 = map2.find(changeData.path);
							if (itfound2 != map2.end()) {
								map2.erase(itfound2);	// �A�b�v���[�h�L���[����폜

								m_modifyFileQue.push_back(parentFolder + L"\\" + name);
								continue;
							}
						}
						PUTLOG(L"�t�@�C��/�t�H���_�����l�[������܂����B: %s -> %s", changeData.path.c_str(), changeData.path2.c_str());
						CMegaAppImpl::GetMegaAppInstance()->RenameNode(
							_MegaPathfromRootPath(changeData.path), changeData.path2);
					}
					break;

					case FileFolderChangeInfo::kItemModify:
					{
						// ���O�g���q�����ׂ�
						std::wstring ext = PathFindExtention(changeData.path);
						if (CConfig::s_setExcludeExt.find(ext) != CConfig::s_setExcludeExt.end())
							continue;

						if (m_processingPath.find(changeData.path) != m_processingPath.end()) {
							m_operateQueList.emplace_back([this, changeData]() -> bool {
								if (m_processingPath.find(changeData.path) == m_processingPath.end()) {
									Node* fileNode = CMegaAppImpl::GetMegaAppInstance()->FindNodeFromPath(changeData.path);
									//ATLASSERT(fileNode);
									if (fileNode == nullptr) {
										PUTLOG(L"�t�@�C��Node��������܂���B: %s", changeData.path.c_str());
										return true;
									}

									// �X�V
									PUTLOG(L"�t�@�C�����X�V����܂����B: %s", changeData.path.c_str());
									CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(fileNode).then(
										[this, fileNode, changeData](){
										CMegaAppImpl::GetMegaAppInstance()->
											AddUploadQue(fileNode->parenthandle, changeData.path);
									});
									return true;
								} else {
									return false;
								}
							});
							continue;
						}

						std::wstring parentFolder = (LPCWSTR)GetParentFolderPath(changeData.path.c_str());
						auto& map = m_createFolderQue.get<path>();
						auto itfound = map.find(parentFolder);
						if (itfound != map.end()) {
							// �t�H���_�쐬�L���[�ɑ��݂���̂ł��̂����A�b�v���[�h�L���[�ɒǉ������
							continue;
						}
						auto& map2 = m_createdFolderQue.get<path>();
						auto itfound2 = map2.find(parentFolder);
						if (itfound2 != map2.end()) {
							// �t�H���_�쐬�����L���[�ɑ��݂���̂ł��̂����A�b�v���[�h�L���[�ɒǉ������
							continue;
						}
						auto& map3 = m_uploadFileQue.get<path>();
						auto itfound3 = map3.find(changeData.path);
						if (itfound3 != map3.end()) {
							// �A�b�v���[�h�L���[�ɂ���̂ł��̂����A�b�v���[�h�����
							continue;
						}
						auto& map4 = m_modifyFileQue.get<path>();
						auto itfound4 = map4.find(changeData.path);
						if (itfound4 != map4.end()) {
							// �C���t�@�C���L���[�ɂ���̂ł��̂����A�b�v���[�h�����
							continue;
						}

						PUTLOG(L"�t�@�C�����X�V����܂����B: %s", changeData.path.c_str());
						m_modifyFileQue.push_back(changeData.path);

						//Node* fileNode = CMegaAppImpl::GetMegaAppInstance()->FindNodeFromPath(changeData.path);
						//if (fileNode == nullptr) {
						//	m_uploadFileQue.push_back(changeData.path);
						//} else {
						//	PUTLOG(L"�t�@�C�����X�V����܂����B: %s", changeData.path.c_str());
						//	// �X�V
						//	CMegaAppImpl::GetMegaAppInstance()->RemoveFileOrFolder(fileNode).then(
						//		[this, fileNode, changeData](){
						//		CMegaAppImpl::GetMegaAppInstance()->
						//			AddUploadQue(fileNode->parenthandle, changeData.path);
						//	});
						//}
					}
					break;
				}	// switch
			}
			catch (std::exception& e) {
				ATLTRACE(e.what());
			}
		}	// for
		if (m_processingPath.size())	// �������Ȃ̂ł����ƌĂ�ł����
			return;
		_ProcessCreateFolderQue();
	});
}

std::wstring CSyncManager::_MegaPathfromRootPath(const std::wstring& path)
{
	return m_rootFolderTree.GetRootFolderData().name + _T("\\") + path;
}


time_t	GetLastWritePosixTime(const std::wstring& filePath);

bool	CSyncManager::DownloadStart(const CString& megaPath, const CString& DLFolderPath)
{
	SyncStop();

	Node* megaFolderNode = CMegaAppImpl::GetMegaAppInstance()->FindNodeFromPath((LPCWSTR)megaPath);
	if (megaFolderNode == nullptr) {
		PUTLOG(L"Mega�̃t�H���_�����݂��܂���B: %s", (LPCWSTR)megaPath);
		return false;
	}

	std::function<void(const CString&, Node*)> funcAddDLQue;
	funcAddDLQue = [&](const CString& parentFolderPath, Node* parentNode) {
		for (Node* childNode : parentNode->children) {
			if (childNode->type == nodetype::FOLDERNODE) {
				CString newFolder = parentFolderPath + ConvertUTF16fromUTF8(childNode->displayname_string()).c_str();
				if (::PathIsDirectory(newFolder) == FALSE && ::CreateDirectory(newFolder, nullptr) == 0) {
					PUTLOG(L"�t�H���_�̍쐬�Ɏ��s�B: %s", (LPCWSTR)newFolder);
					continue;					
				}
				funcAddDLQue(newFolder + L"\\", childNode);

			} else if (childNode->type == nodetype::FILENODE) {
				CString filePath = parentFolderPath + ConvertUTF16fromUTF8(childNode->displayname_string()).c_str();
				time_t lastWriteTime = GetLastWritePosixTime((LPCWSTR)filePath);
				time_t createTime = GetCreatePosixTime((LPCWSTR)filePath);
				time_t megaLastWriteTime = childNode->mtime;
				if (megaLastWriteTime == 0)
					megaLastWriteTime = childNode->ctime;
				if (lastWriteTime < megaLastWriteTime && createTime <  megaLastWriteTime)
					m_downloadQue.push_back(std::make_pair(filePath, childNode));
			}
		}
	};
	funcAddDLQue(AddBackslash(DLFolderPath), megaFolderNode);
	if (m_downloadQue.empty()) {
		PUTLOG(L"�_�E�����[�h����t�@�C���͂���܂���B");
		return false;
	}

	m_bDownloading = true;
	_DownloadProcess();
	return true;
}

void	CSyncManager::_DownloadProcess()
{
	while (m_processingPath.size() < CConfig::s_nMaxDownloadFileCount && m_downloadQue.size()) {
		CString downloadPath = m_downloadQue.front().first;
		Node* node = m_downloadQue.front().second;
		PUTLOG(L"�_�E�����[�h���J�n���܂��B: %s", (LPCWSTR)downloadPath);
		m_processingPath.emplace((LPCWSTR)downloadPath, false);
		m_downloadQue.pop_front();

		time_t ctime = node->ctime;
		time_t mtime = node->mtime;
		if (mtime == 0)
			mtime = ctime;

		CMegaAppImpl::GetMegaAppInstance()->DownloadFile((LPCWSTR)downloadPath, node).
			then([this, mtime](const std::wstring& downloadFilePath) {

			if (mtime)
				SetLastWriteTime(downloadFilePath, mtime);

			std::lock_guard<std::recursive_mutex> lock(m_queMutex);
			m_processingPath.erase(downloadFilePath);

			_DownloadProcess();
		});
	}

	if (m_processingPath.empty() && m_downloadQue.empty()) {
		m_bDownloading = false;
		CConfig::ClearDLTarget();
		PUTLOG(L"���ׂẴ_�E�����[�h���I�����܂����B");
		if (m_funcDLComplete)
			m_funcDLComplete();
	}
}

void	CSyncManager::DownloadStop()
{
	if (m_bDownloading == false) {
		PUTLOG(L"�_�E�����[�h�͊J�n����Ă��܂���B");
		return;
	}
	m_bDownloading = false;
	CConfig::ClearDLTarget();
	PUTLOG(L"�_�E�����[�h�𒆎~���܂��B");

	CMegaAppImpl::GetMegaAppInstance()->AllTransCancel();

	std::lock_guard<std::recursive_mutex> lock(m_queMutex);
	m_downloadQue.clear();
}


/////////////////////////////////////////////////////
// SyncInfo

void	CSyncManager::SyncInfo::Init()
{
	fileNewUploadCount = 0;
	fileRefreshUploadCount = 0;
	folderCreateCount = 0;
	fileUploadSkipCount = 0;
	fileRemoveCount = 0;
	folderRemoveCount = 0;
	fileUploadFailCount = 0;
	folderCreateFailCount = 0;
}

void	CSyncManager::SyncInfo::SyncInfoPutLog()
{
	if (fileNewUploadCount == 0 &&
		fileRefreshUploadCount == 0 &&
		folderCreateCount == 0 &&
		fileUploadSkipCount == 0 &&
		fileRemoveCount == 0 &&
		folderRemoveCount == 0 &&
		fileUploadFailCount == 0 &&
		folderCreateFailCount == 0)
		return;

	PUTLOG(L"�����I��");
	PUTLOG(L"�t�@�C��[�V�K : %d]�A[�X�V : %d]�A[�폜 : %d]�A[�X�L�b�v : %d]", 
		fileNewUploadCount, fileRefreshUploadCount, fileRemoveCount, fileUploadSkipCount);
	PUTLOG(L"�t�H���_�[[�쐬 : %d]�A[�폜 : %d]", folderCreateCount, folderRemoveCount);
	PUTLOG(L"�t�@�C���쐬���s : %d �A�t�H���_�쐬���s : %d", fileUploadFailCount, folderCreateFailCount);
}

