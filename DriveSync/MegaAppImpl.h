/**
*	@file	MegaAppImpl.h
*/

#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <atlsync.h>
#include "mega\megaclient.h"


class CTransactionView;


class CAsyncPutNodesNotify
{
public:
	CAsyncPutNodesNotify() : m_watchNode(nullptr) {}

	NodeCore*	GetWatchNode() const { return m_watchNode; }
	void		SetWatchNode(NodeCore* node) { m_watchNode = node; }

	void	then(std::function<void(NodeCore*)> func) {	m_funcNotify = func; }

	void	Notify(NodeCore* node) { if (m_funcNotify) m_funcNotify(node); }
private:
	NodeCore*	m_watchNode;
	std::function<void (NodeCore*)>	m_funcNotify;
};

class CAsyncUnlinkNotify
{
public:
	CAsyncUnlinkNotify(handle watchHandle) : m_watchHandle(watchHandle) {}

	handle	GetWatchHandle() const { return m_watchHandle; }

	void	then(std::function<void()> func) { m_funcNotify = func;  }

	void	Notify() { if (m_funcNotify) m_funcNotify(); }
private:
	handle	m_watchHandle;
	std::function<void ()>	m_funcNotify;
};

class CAsyncAddUploadQueNotify
{
public:
	//CAsyncAddUploadQueNotify() {}

	void	then(std::function<void (const std::wstring&)> func) { m_funcNotify = func; }

	void	Notify(const std::wstring& path) { if (m_funcNotify) m_funcNotify(path); }
private:
	std::function<void (const std::wstring&)>	m_funcNotify;
};

class CAsyncDUNotify
{
public:
	void	then(std::function<void(const TreeProcDU&)> func) { m_funcNotify = func; }

	void Notify(const TreeProcDU& du) { if (m_funcNotify) m_funcNotify(du); }
private:
	std::function<void(const TreeProcDU&)>	m_funcNotify;
};

class CAsyncDLNotify
{
public:
	void	then(std::function<void(const std::wstring&)> func) { m_funcNotify = func; }

	void Notify(const std::wstring& downloadFilePath) { 
		if (m_funcNotify) m_funcNotify(downloadFilePath); 
	}
private:
	std::function<void(const std::wstring&)>	m_funcNotify;
};



/////////////////////////////////////////////////////////////////
// CMegaAppImpl

class CMegaAppImpl : public MegaApp
{
public:
	static void	StartMegaLoop();
	void	LoopMain();

	// CMegaAppImpl::GetMegaAppInstance()->
	static CMegaAppImpl* GetMegaAppInstance() { return s_pMegaAppImpl; }

	CMegaAppImpl();

	void	SetTransactionViewPtr(CTransactionView*	p) {
		m_pTransactionView = p;
	}

	Node*	FindNodeFromPath(const std::wstring& path, std::string* namePart = nullptr);
	static Node*	ChildNodeByName(Node* parent, const std::string& name);

	void	Login(LPCSTR mailaddress, LPCSTR password);
	void	Logout();
	bool	IsLogined() const { return m_bLoginSucceed; }
	void	SetLoginResultCallback(std::function<void (bool)> func) { m_funcLoginResult = func; }
	void	SetLoginSuccessCallback(std::function<void()> func) { m_funcLoginSucess = func; }
	void	SetLogoutPerformCallback(std::function<void()> func) { m_funcLogoutPerform = func; }

	void	AddCommandQue(std::function<void()> func);

	CAsyncPutNodesNotify&	CreateFolder(const std::wstring& folderPath);
	CAsyncUnlinkNotify&		RemoveFileOrFolder(NodeCore* fileOrFolderNode);
	CAsyncAddUploadQueNotify&	AddUploadQue(handle parentNodeHandle, const std::wstring& filePath);
	void	MoveNode(const std::wstring& moveNodePath, const std::wstring& targetFolderPath);
	void	RenameNode(const std::wstring& renameNodePath, const std::wstring& newName);

	bool	UploadFileCancel(const std::wstring& filePath);

	void	AllTransCancel();

	CAsyncDLNotify&	DownloadFile(const std::wstring& filePath, Node* node);

	CAsyncDUNotify&	GetMegaStrageInfo();
	// -----------------------------------------------------------
	// MegaApp : callback interface

	// returns a new instance of the application's own FileAccess class
	virtual FileAccess* newfile() override;

	// a request-level error occurred (other than API_EAGAIN, which will lead to a retry)
	virtual void request_error(error) override;

	// login result
	virtual void login_result(error) override;

	// ephemeral session creation/resumption result
	virtual void ephemeral_result(error) override;
	virtual void ephemeral_result(handle, const byte*) override;

	// account creation
	virtual void sendsignuplink_result(error) override;
	virtual void querysignuplink_result(error) override;
	virtual void querysignuplink_result(handle, const char*, const char*, const byte*, const byte*, const byte*, size_t) override;
	virtual void confirmsignuplink_result(error) override;
	virtual void setkeypair_result(error) override;

	// account credentials, properties and history
	virtual void account_details(AccountDetails*, int, int, int, int, int, int) override;
	virtual void account_details(AccountDetails*, error) override;

	// node attribute update failed (not invoked unless error != API_OK)
	virtual void setattr_result(handle, error) override;

	// move node failed (not invoked unless error != API_OK)
	virtual void rename_result(handle, error) override;

	// node deletion failed (not invoked unless error != API_OK)
	virtual void unlink_result(handle, error) override;

	// nodes have been updated
	virtual void nodes_updated(Node**, int) override;

	// users have been added or updated
	virtual void users_updated(User**, int) override;

	// node fetch has failed
	virtual void fetchnodes_result(error) override;

	// node addition has failed
	virtual void putnodes_result(error, targettype, NewNode*, int) override;

	// share update result
	virtual void share_result(error) override;
	virtual void share_result(int, error) override;

	// file attribute fetch result
	virtual void fa_complete(Node*, fatype, const char*, uint32_t) override;
	virtual int fa_failed(handle, fatype, int) override;

	// file attribute modification result
	virtual void putfa_result(handle, fatype, error) override;

	// user invites/attributes
	virtual void invite_result(error) override;
	virtual void putua_result(error) override;
	virtual void getua_result(error) override;
	virtual void getua_result(byte*, unsigned) override;

	// file node export result
	virtual void exportnode_result(error) override;
	virtual void exportnode_result(handle, handle) override;

	// exported link access result
	virtual void openfilelink_result(error) override;
	virtual void openfilelink_result(Node*) override;

	// topen() result
	virtual void topen_result(int, error) override;
	virtual void topen_result(int, string*, const char*, int) override;

	// transfer progress information
	virtual void transfer_update(int, m_off_t, m_off_t, dstime) override;

	// intermitten transfer error
	virtual int transfer_error(int, int, int) override;

	// transfer error
	virtual void transfer_failed(int, error) override;
	virtual void transfer_failed(int, string&, error) override;

	virtual void upload_fileopen_failed(const std::string& filename) override;
	virtual void download_fileopen_failed(const std::wstring& downloadFilePath) override;

	// transfer limit reached
	virtual void transfer_limit(int) override;

	// transfer completed
	virtual void transfer_complete(int, chunkmac_map*, const char*) override;
	virtual void transfer_complete(int, handle, const byte*, const byte*, SymmCipher*) override;

	virtual void changepw_result(error) override;

	// user attribute update notification
	virtual void userattr_update(User*, int, const char*) override;

	// suggest reload due to possible race condition with other clients
	virtual void reload(const char*) override;

	// wipe all users, nodes and shares
	virtual void clearing() override;

	// failed request retry notification
	virtual void notify_retry(dstime) override;

	// generic debug logging
	virtual void debug_log(const char*) override;

private:
	std::string _NodePath(handle h);

	// Data members
	static CMegaAppImpl*	s_pMegaAppImpl;

	DWORD	m_loopThreadId;

	bool	m_bLoginSucceed;
	std::function<void(bool)>	m_funcLoginResult;
	std::function<void()>	m_funcLoginSucess;
	std::function<void()>	m_funcLogoutPerform;

	CCriticalSection	m_csAsyncPutNodes;
	std::unordered_map<std::wstring, std::unique_ptr<CAsyncPutNodesNotify>>	m_mapAsyncPutNodesNotify;

	CCriticalSection	m_csAsyncUnlink;
	std::vector<std::unique_ptr<CAsyncUnlinkNotify>>	m_vecpAsyncUnlinkNotify;

	CCriticalSection	m_csAsyncAddQue;
	std::unordered_map<std::wstring, std::unique_ptr<CAsyncAddUploadQueNotify>> m_mapAsyncAddQueNotify;

	CCriticalSection	m_csAsyncDL;
	std::unordered_map<std::wstring, std::unique_ptr<CAsyncDLNotify>> m_mapAsyncDLNotify;

	CCriticalSection	m_csCommand;
	std::list<std::function<void()>>	m_listCommandFunc;
	CTransactionView*	m_pTransactionView;

};






























