/**
*	@file	MegaAppImpl.cpp
*/

#include "stdafx.h"
#include "MegaAppImpl.h"
#include <time.h>
#include <atomic>
#include <strstream>
#include <boost\format.hpp>
using boost::format;
using boost::wformat;

#include "mega\megawin32.h"
#include "mega\megabdb.h"
#include "mega\megacli.h"
#include "MainDlg.h"

#include "Utility.h"
#include "AppKey.h"


// 定義はmegawin32.cpp
int rename_file(const char*, const char*);
int unlink_file(const char*);

extern MegaClient* client;
extern std::atomic_bool	g_atomicMegaThreadActive;

namespace {

	//bool debug = true;

const wchar_t* errorstring(error e)
{
	switch (e)
	{
		case API_OK:
		return L"No error";
		case API_EINTERNAL:
		return L"Internal error";
		case API_EARGS:
		return L"Invalid argument";
		case API_EAGAIN:
		return L"Request failed, retrying";
		case API_ERATELIMIT:
		return L"Rate limit exceeded";
		case API_EFAILED:
		return L"Transfer failed";
		case API_ETOOMANY:
		return L"Too many concurrent connections or transfers";
		case API_ERANGE:
		return L"Out of range";
		case API_EEXPIRED:
		return L"Expired";
		case API_ENOENT:
		return L"Not found";
		case API_ECIRCULAR:
		return L"Circular linkage detected";
		case API_EACCESS:
		return L"Access denied";
		case API_EEXIST:
		return L"Already exists";
		case API_EINCOMPLETE:
		return L"Incomplete";
		case API_EKEY:
		return L"Invalid key/integrity check failed";
		case API_ESID:
		return L"Bad session ID";
		case API_EBLOCKED:
		return L"Blocked";
		case API_EOVERQUOTA:
		return L"Over quota";
		case API_ETEMPUNAVAIL:
		return L"Temporarily not available";
		case API_ETOOMANYCONNECTIONS:
		return L"Connection overflow";
		case API_EWRITE:
		return L"Write error";
		case API_EREAD:
		return L"Read error";
		case API_EAPPKEY:
		return L"Invalid application key";
		default:
		return L"Unknown error";
	}
}


}	// namespace

///////////////////////////////////////////////////////////////////////////
// CMegaAppImpl

CMegaAppImpl*	CMegaAppImpl::s_pMegaAppImpl = nullptr;

void	CMegaAppImpl::StartMegaLoop()
{
	s_pMegaAppImpl = new CMegaAppImpl;
	::client = new MegaClient(s_pMegaAppImpl, new WinHttpIO, new BdbAccess, kAppkey);

	s_pMegaAppImpl->LoopMain();

	s_pMegaAppImpl = nullptr;
}

void	CMegaAppImpl::LoopMain()
{
	m_loopThreadId = ::GetCurrentThreadId();

	while (g_atomicMegaThreadActive) {

		{
			CCritSecLock lock(m_csCommand);
			std::list<std::function<void()>> list;
			list.swap(m_listCommandFunc);
			m_listCommandFunc.clear();
			lock.Unlock();

			for (auto& func : list)
				func();
		}

		// pass the CPU to the engine (nonblocking)
		::client->exec();

		// have the engine invoke HttpIO's waitio(), if so required
		::client->wait();

		::_sleep(10);
	}
}

CMegaAppImpl::CMegaAppImpl() : m_loopThreadId(0), m_bLoginSucceed(false) {}

void	CMegaAppImpl::Login(LPCSTR mailaddress, LPCSTR password)
{
	m_bLoginSucceed = false;
	std::string mail = mailaddress;
	std::string pass = password;
	AddCommandQue([this, mail, pass](){
		byte pwkey[SymmCipher::KEYLENGTH];
		error result = client->pw_key(pass.c_str(), pwkey);
		if (result != API_OK) {
			ATLASSERT(FALSE);
			return;
		}

		client->login(mail.c_str(), pwkey);
	});
}

void	CMegaAppImpl::Logout()
{
	m_bLoginSucceed = false;
	if (m_funcLogoutPerform)
		m_funcLogoutPerform();
	AddCommandQue([this](){
		client->logout();
	});	
}

Node*	CMegaAppImpl::ChildNodeByName(Node* parent, const std::string& name)
{
	for (auto& childNode : parent->children) {
		if (name.compare(childNode->displayname()) == 0)
			return childNode;
	}

	return nullptr;
}

Node* CMegaAppImpl::FindNodeFromPath(const std::wstring& path, std::string* namePart /*= nullptr*/)
{
	std::vector<std::string>	partPath = SeparatePathUTF8(path);
	//if (partPath.empty())
	//	return nullptr;

	Node* node = client->nodebyhandle(client->rootnodes[0]);
	ATLASSERT(node);
	int nCount = (int)partPath.size();
	for (int i = 0; i < nCount; ++i) {
		//std::wstring displayName = ConvertUTF16fromUTF8(node->displayname());
		Node* childNode = ChildNodeByName(node, partPath[i]);
		if (childNode == nullptr) {
			if (namePart && i == (partPath.size() - 1)) {
				*namePart = partPath[i];
				return node;
			}
			return nullptr;
		}
		node = childNode;
	}
	return node;
}


void	CMegaAppImpl::AddCommandQue(std::function<void()> func)
{
	CCritSecLock lock(m_csCommand);
	m_listCommandFunc.emplace_back(func);
}


CAsyncPutNodesNotify&	CMegaAppImpl::CreateFolder(const std::wstring& folderPath)
{
	CCritSecLock lock(m_csAsyncPutNodes);
	CAsyncPutNodesNotify* notify = m_mapAsyncPutNodesNotify.insert(
		std::make_pair(folderPath, std::unique_ptr<CAsyncPutNodesNotify>(new CAsyncPutNodesNotify))).first->second.get();
	lock.Unlock();

	AddCommandQue([this, folderPath]() {
		auto funcNotify = [this, folderPath](Node* n) {
			CCritSecLock lock(m_csAsyncPutNodes);
			auto itfound = m_mapAsyncPutNodesNotify.find(folderPath);
			if (itfound != m_mapAsyncPutNodesNotify.end()) {
				std::unique_ptr<CAsyncPutNodesNotify> async(itfound->second.release());
				m_mapAsyncPutNodesNotify.erase(itfound);
				async->Notify(n);
			}
		};
		std::string newname;
		if (Node* n = FindNodeFromPath(folderPath, &newname)) {
			if (client->checkaccess(n, RDWR) == 0) {
				ATLTRACE(L"Write access denied\n");
				funcNotify(nullptr);
				return ;
			}

			if (newname.size()) {
				SymmCipher key;
				string attrstring;
				byte buf[Node::FOLDERNODEKEYLENGTH];
				NewNode* newnode = new NewNode[1];

				// set up new node as folder node
				newnode->source = NEW_NODE;
				newnode->type = FOLDERNODE;
				newnode->nodehandle = 0;
				newnode->mtime = newnode->ctime = time(NULL);
				newnode->parenthandle = UNDEF;

				// generate fresh random key for this folder node
				PrnGen::genblock(buf, Node::FOLDERNODEKEYLENGTH);
				newnode->nodekey.assign((char*)buf, Node::FOLDERNODEKEYLENGTH);
				key.setkey(buf);

				// generate fresh attribute object with the folder name
				AttrMap attrs;
				attrs.map['n'] = newname;

				// JSON-encode object and encrypt attribute string
				attrs.getjson(&attrstring);
				client->makeattr(&key, &newnode->attrstring, attrstring.c_str());

				// add the newly generated folder node
				handle parentNodeHandle = n->nodehandle;

				CCritSecLock lock(m_csAsyncPutNodes);
				auto itfound = m_mapAsyncPutNodesNotify.find(folderPath);
				ATLASSERT(itfound != m_mapAsyncPutNodesNotify.end());
				itfound->second->SetWatchNode(newnode);
				lock.Unlock();

				CCritSecLock lock2(m_csCommand);
				m_listCommandFunc.push_back([this, parentNodeHandle, newnode]() {
					client->putnodes(parentNodeHandle, newnode, 1);
				});
				return ;

			} else {
				ATLTRACE(L"Path already exists: %s\n", folderPath.c_str());
				funcNotify(n);
			}
		} else {
			funcNotify(nullptr);
		}
	});

	return *notify;
}


CAsyncUnlinkNotify&	CMegaAppImpl::RemoveFileOrFolder(NodeCore* fileOrFolderNode)
{
	//Node* rmTarget = FindNodeFromPath(fileOrFolderPath);
	//if (rmTarget == nullptr || client->checkaccess(rmTarget, FULL) == false)
	//	return false;
	CCritSecLock lock(m_csAsyncUnlink);
	m_vecpAsyncUnlinkNotify.emplace_back(new CAsyncUnlinkNotify(fileOrFolderNode->nodehandle));
	CAsyncUnlinkNotify* notify = m_vecpAsyncUnlinkNotify.back().get();
	lock.Unlock();
	handle targetHandle = fileOrFolderNode->nodehandle;

	AddCommandQue([this, targetHandle]() {
		Node* delNode = client->nodebyhandle(targetHandle);
		if (delNode == nullptr) {
			PUTLOG(L"RemoveFileOrFolder : Nodeが見つかりません。");
			return;
		}
		error e = client->unlink(delNode);
		ATLASSERT(e == API_OK);
	});
	return *notify;
}

CAsyncAddUploadQueNotify&	CMegaAppImpl::AddUploadQue(handle parentNodeHandle, const std::wstring& filePath)
{
	std::string utf8filePath = ConvertUTF8fromUTF16(filePath);
	std::string filename = ConvertUTF8fromUTF16(::PathFindFileNameW(filePath.c_str()));
	CCritSecLock lock(m_csAsyncAddQue);
	auto it = m_mapAsyncAddQueNotify.insert(
		std::make_pair(filePath, std::unique_ptr<CAsyncAddUploadQueNotify>(new CAsyncAddUploadQueNotify)));
	lock.Unlock();

	if (m_loopThreadId == ::GetCurrentThreadId()) {
		client->putq.push_back(
			new AppFilePut(utf8filePath.c_str(), parentNodeHandle, "", filename.c_str()));
	} else {
		AddCommandQue([this, parentNodeHandle, utf8filePath, filename]() {
			client->putq.push_back(
				new AppFilePut(utf8filePath.c_str(), parentNodeHandle, "", filename.c_str()));
		});
	}
	return *it.first->second.get();
}

void	CMegaAppImpl::MoveNode(const std::wstring& moveNodePath, const std::wstring& targetFolderPath)
{
	AddCommandQue([this, moveNodePath, targetFolderPath]() {
		Node* moveNode = FindNodeFromPath(moveNodePath);
		if (moveNode == nullptr) {
			PUTLOG(L"移動元のファイル/フォルダーが存在しません。: %s", moveNodePath.c_str());
			return;
		}
		std::string newname;
		Node* targetNode = FindNodeFromPath(targetFolderPath, &newname);
		if (targetNode == nullptr || newname.size()) {
			PUTLOG(L"移動先のフォルダが存在しません。: %s", targetFolderPath.c_str());
			return;
		}		
		if (moveNode->parent == targetNode) {
			PUTLOG(L"移動元と移動先のフォルダが同一です。: %s", targetFolderPath.c_str());
			return;
		}
		error e = client->rename(moveNode, targetNode);
		ATLASSERT(e == API_OK);
	});
}

void	CMegaAppImpl::RenameNode(const std::wstring& renameNodePath, const std::wstring& newName)
{
	AddCommandQue([this, renameNodePath, newName]() {
		Node* targetNode = FindNodeFromPath(renameNodePath);
		if (targetNode == nullptr) {
			PUTLOG(L"リネームするファイル/フォルダが存在しません。: %s", renameNodePath.c_str());
			return;
		}
		if (ChildNodeByName(targetNode->parent, ConvertUTF8fromUTF16(newName))) {
			PUTLOG(L"リネーム先の名前が既に存在します。: %s", newName.c_str());
			return;
		}
		if (client->checkaccess(targetNode, RDWR) == false) {
			PUTLOG(L"書き込み権限がありません。: %s", renameNodePath.c_str());
		}
		// rename
		targetNode->attrs.map['n'] = ConvertUTF8fromUTF16(newName);
		error e = client->setattr(targetNode);
		ATLASSERT(e == API_OK);
		if (e != API_OK) {
			PUTLOG(L"リネーム失敗しました。( %s )", errorstring(e));
		}
	});
}

bool	CMegaAppImpl::UploadFileCancel(const std::wstring& filePath)
{
	ATLASSERT(m_loopThreadId == ::GetCurrentThreadId());
	std::string utf8filePath = ConvertUTF8fromUTF16(filePath);
	for (auto it = client->putq.begin(); it != client->putq.end(); ++it) {
		if ((*it)->filename == utf8filePath) {
			if (0 <= (*it)->td) {
				client->tclose((*it)->td);	// disconnect
				m_pTransactionView->TransferComplete((*it)->td, false);
			}
			delete *it;
			client->putq.erase(it);

			auto itfound = m_mapAsyncAddQueNotify.find(filePath);
			if (itfound != m_mapAsyncAddQueNotify.end()) {
				itfound->second->Notify(filePath);
			} else {
				ATLASSERT(FALSE);
			}
			return true;
		}
	}
	return false;
}

void	CMegaAppImpl::AllTransCancel()
{
	AddCommandQue([this]() {
		{
			CCritSecLock lock(m_csAsyncPutNodes);
			m_mapAsyncPutNodesNotify.clear();
		}
		{
			CCritSecLock lock(m_csAsyncAddQue);
			m_mapAsyncAddQueNotify.clear();
		}
		{
			CCritSecLock lock(m_csAsyncDL);
			m_mapAsyncDLNotify.clear();
		}

		for (auto& q : client->putq) {
			if (0 <= q->td) {
				client->tclose(q->td);
				m_pTransactionView->TransferComplete(q->td, false);
			}
			delete q;
		}
		client->putq.clear();

		for (auto& q : client->getq) {
			if (0 <= q->td) {
				client->tclose(q->td);
				m_pTransactionView->TransferComplete(q->td, false);
			}
			delete q;
		}
		client->getq.clear();
	});
}

CAsyncDLNotify&	CMegaAppImpl::DownloadFile(const std::wstring& filePath, Node* node)
{
	ATLASSERT(node);

	CCritSecLock lock(m_csAsyncDL);
	auto it = m_mapAsyncDLNotify.insert(
		std::make_pair(filePath, std::make_unique<CAsyncDLNotify>()));
	lock.Unlock();

	AddCommandQue([this, node, filePath]() {
		client->getq.push_back(new AppFileGet(node->nodehandle, filePath));
	});
	return *it.first->second;
}

CAsyncDUNotify&	CMegaAppImpl::GetMegaStrageInfo()
{
	auto notify = new CAsyncDUNotify;
	AddCommandQue([this, notify]() {
		Node* root = client->nodebyhandle(client->rootnodes[0]);
		if (root) {
			TreeProcDU du;
			client->proctree(root, &du);
			notify->Notify(du);
		}
		delete notify;
	});
	return *notify;
}

// -----------------------------------------------------------
// MegaApp : callback interface

FileAccess* CMegaAppImpl::newfile()
{
	return new WinFileAccess();
}


// callback for non-EAGAIN request-level errors
// in most cases, retrying is futile, so the application exits
// this can occur e.g. with syntactically malformed requests (due to a bug), an invalid application key
void CMegaAppImpl::request_error(error e)
{
	if (e == API_ESID) {
		ATLTRACE(L"Invalid or expired session, logging out...\n");
		client->logout();
		return;
	}

	ATLTRACE(L"FATAL: Request failed ( %s ), exiting\n", errorstring(e));

	//exit(0);
	//ATLASSERT(FALSE);
}

// login result
void CMegaAppImpl::login_result(error e)
{
	if (m_funcLoginResult)
		m_funcLoginResult(e == API_OK);

	if (e != API_OK) {
		PUTLOG(L"ログインに失敗: %s\n", errorstring(e));

	} else {
		PUTLOG(L"ログインに成功、アカウントを検索します...\n");
		client->fetchnodes();
	}
}


// ephemeral session result
void CMegaAppImpl::ephemeral_result(error e)
{
	if (e != API_OK)
		ATLTRACE(L"Ephemeral session error ( %s )\n", errorstring(e));
}

void CMegaAppImpl::ephemeral_result(handle uh, const byte* pw)
{
	char buf[SymmCipher::KEYLENGTH * 4 / 3 + 3];
	Base64::btoa((byte*)&uh, sizeof uh, buf);
	char buf2[SymmCipher::KEYLENGTH * 4 / 3 + 3];
	Base64::btoa(pw, SymmCipher::KEYLENGTH, buf2);

	ATLTRACE((format("Ephemeral session established, session ID: %s#%s\n") % buf % buf2).str().c_str());

	client->fetchnodes();
}



// signup link send request result
void CMegaAppImpl::sendsignuplink_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Unable to send signup link ( %s )\n", errorstring(e));
	} else {
		ATLTRACE(L"Thank you. Please check your e-mail and enter the command signup followed by the confirmation link.\n");
	}
}

// signup link query result
void CMegaAppImpl::querysignuplink_result(handle uh, const char* email, const char* name, const byte* pwc, const byte* kc, const byte* c, size_t len)
{
	ATLTRACE(L"Ready to confirm user account %s ( %s ) - enter confirm to execute.\n", email, name);

	ATLASSERT(FALSE);
	//signupemail = email;
	//signupcode.assign((char*)c, len);
	//memcpy(signuppwchallenge, pwc, sizeof signuppwchallenge);
	//memcpy(signupencryptedmasterkey, pwc, sizeof signupencryptedmasterkey);
}

// signup link query failed
void CMegaAppImpl::querysignuplink_result(error e)
{
	ATLTRACE(L"Signuplink confirmation failed ( %s )\n", errorstring(e));
}

// signup link (account e-mail) confirmation result
void CMegaAppImpl::confirmsignuplink_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Signuplink confirmation failed ( %s )\n", errorstring(e));
	} else {
		ATLTRACE(L"Signup confirmed, logging in...\n");
		ATLASSERT(FALSE);
		//client->login(signupemail.c_str(), pwkey);
	}
}

// asymmetric keypair configuration result
void CMegaAppImpl::setkeypair_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"RSA keypair setup failed ( %s )\n", errorstring(e));
	} else {
		ATLTRACE(L"RSA keypair added. Account setup complete.\n");
	}
}



// display account details/history
void CMegaAppImpl::account_details(AccountDetails* ad, int storage, int transfer, int pro, int purchases, int transactions, int sessions)
{
	User* u;
	if (u = client->finduser(client->me)) {
		ATLTRACE((format("Account e-mail: %s\n") % u->email).str().c_str());
	}

	if (storage) {
		ATLTRACE(L"\tStorage: %d of %d (%d%%)\n", ad->storage_used, ad->storage_max, (100 * ad->storage_used / ad->storage_max));
	}

	if (transfer) {
		if (ad->transfer_max) {
			ATLTRACE(L"\tTransfer in progress: %d/%d\n", ad->transfer_own_reserved, ad->transfer_srv_reserved);
			ATLTRACE(L"\tTransfer completed: %d/%d of %d (%d%%)\n", 
				ad->transfer_own_used, 
				ad->transfer_srv_used, 
				ad->transfer_max, 
				(100 * (ad->transfer_own_used + ad->transfer_srv_used) / ad->transfer_max));
			ATLTRACE(L"\tServing bandwidth ratio: %d%%\n", ad->srv_ratio);
		}

		if (ad->transfer_hist_starttime) {
			time_t t = time(NULL) - ad->transfer_hist_starttime;

			ATLTRACE(L"\tTransfer history:\n");

			for (unsigned i = 0; i < ad->transfer_hist.size(); i++) {
				t -= ad->transfer_hist_interval;
				std::wstringstream ss;
				ss << L"\t\t";
				if (t < ad->transfer_hist_interval) {
					ss << L" second(s) ago until now: ";
				} else {
					ss << L"-" << (t - ad->transfer_hist_interval) << L"second(s) ago: ";
				}
				ss << ad->transfer_hist[i] << "byte(s)\n";
				ATLTRACE(ss.str().c_str());
			}
		}

		if (ad->transfer_limit) {
			ATLTRACE(L"Per-IP transfer limit: %d\n", ad->transfer_limit);
		}
	}

	if (pro) {
		ATLTRACE(L"\tPro level: %d", ad->pro_level);
		ATLTRACE("\tSubscription type: %c", ad->subscription_type);
		ATLTRACE(L"\tAccount balance:");

		for (auto& blance : ad->balances) {
			ATLTRACE(L"\tBalance: %.3s %.02f\n", blance.currency, blance.amount);
		}
	}

	if (purchases) {
		ATLTRACE(L"Purchase history:");

		for (auto& purchase : ad->purchases) {
			wchar_t timebuf[32] = L"";
			wcsftime(timebuf, sizeof timebuf, L"%c", localtime(&purchase.timestamp));
			ATLTRACE(L"\tID: %.11s Time: %s Amount: %.3s %.02f Payment method: %d\n", 
				purchase.handle, timebuf, purchase.currency, purchase.amount, purchase.method);
		}
	}

	if (transactions) {
		ATLTRACE(L"Transaction history:");

		for (auto& transaction : ad->transactions) {
			wchar_t timebuf[32] = L"";
			wcsftime(timebuf, sizeof timebuf, L"%c", localtime(&transaction.timestamp));
			ATLTRACE(L"\tID: %.11s Time: %s Delta: %.3s %.02f\n", 
				transaction.handle, timebuf, transaction.currency, transaction.delta);
		}
	}

	if (sessions) {
		ATLTRACE(L"Session history:");

		for (auto& session : ad->sessions) {
			wchar_t timebuf[32] = L"";
			wchar_t timebuf2[32] = L"";
			wcsftime(timebuf, sizeof timebuf, L"%c", localtime(&session.timestamp));
			wcsftime(timebuf2, sizeof timebuf, L"%c", localtime(&session.mru));
			ATLTRACE(L"\tSession start: %s Most recent activity: %s IP: %s Country: %.2s User-Agent: %s\n", 
				timebuf, timebuf2, session.ip.c_str(), session.country, session.useragent.c_str());
		}
	}
}

// account details could not be retrieved
void CMegaAppImpl::account_details(AccountDetails* ad, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Account details retrieval failed ( %s )\n", errorstring(e));
	}
}


void CMegaAppImpl::setattr_result(handle, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Node attribute update failed ( %s )\n", errorstring(e));
	}
}

void CMegaAppImpl::rename_result(handle, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Node move failed ( %s )\n", errorstring(e));
	}
}

void CMegaAppImpl::unlink_result(handle h, error e)
{
	CCritSecLock lock(m_csAsyncUnlink);
	for (auto it = m_vecpAsyncUnlinkNotify.begin(); it != m_vecpAsyncUnlinkNotify.end(); ++it) {
		if (it->get()->GetWatchHandle() == h) {
			std::unique_ptr<CAsyncUnlinkNotify> notify(it->release());
			m_vecpAsyncUnlinkNotify.erase(it);
			lock.Unlock();

			notify->Notify();			
			break;
		}
	}

	if (e != API_OK) {
		ATLTRACE(L"Node deletion failed ( %s )\n", errorstring(e));
	}
}


// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void CMegaAppImpl::nodes_updated(Node** n, int count)
{
	int c[2][6] = { { 0 } };

	if (n) {
		while (count--) {
			if ((*n)->type < 6) {
				c[!(*n)->removed][(*n)->type]++;
				n++;
			}
		}
	} else {
		for (node_map::iterator it = client->nodes.begin(); it != client->nodes.end(); it++)
			if (it->second->type < 6)
				c[1][it->second->type]++;
	}

	auto nodestats = [](int* c, const char* action) {
		std::wstringstream ss;
		if (c[FILENODE]) 
			ss << c[FILENODE] << (c[FILENODE] == 1) ? L" file" : L" files";
		if (c[FILENODE] && c[FOLDERNODE]) 
			ss << L" and ";
		if (c[FOLDERNODE]) 
			ss << c[FOLDERNODE] << (c[FOLDERNODE] == 1) ? L" folder" : L" folders";
		if (c[MAILNODE] && (c[FILENODE] || c[FOLDERNODE])) 
			ss << L" and ";
		if (c[MAILNODE]) 
			ss << c[MAILNODE] <<  (c[MAILNODE] == 1) ? L" mail" : L" mails";

		if (c[FILENODE] || c[FOLDERNODE] || c[MAILNODE]) {
			ss << " " << (LPCWSTR)CA2W(action) << std::endl;
			ATLTRACE(ss.str().c_str());
		}
	};

	nodestats(c[1], "added or updated");
	nodestats(c[0], "removed");

	//if (ISUNDEF(cwd)) cwd = client->rootnodes[0];
}

// user addition/update (users never get deleted)
void CMegaAppImpl::users_updated(User** u, int count)
{
	if (count == 1) {
		ATLTRACE(L"1 user received or updated\n");
	} else {
		ATLTRACE(L"%d users received or updated\n", count);
	}
}


void CMegaAppImpl::fetchnodes_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"File/folder retrieval failed ( %s )\n", errorstring(e));
	} else {
		PUTLOG(L"fetchNodes完了");
		m_bLoginSucceed = true;
		if (m_funcLoginSucess)
			m_funcLoginSucess();
	}
}

void CMegaAppImpl::putnodes_result(error e, targettype t, NewNode* nn, int count)
{
	if (t == USER_HANDLE) {
		if (e == API_OK)
			ATLTRACE(L"Success.\n");
	} else if (t == targettype::NODE_HANDLE) {
		CCritSecLock lock(m_csAsyncPutNodes);
		for (auto it = m_mapAsyncPutNodesNotify.begin(); it != m_mapAsyncPutNodesNotify.end(); ++it) {
			for (int i = 0; i < count; ++i) {
				if (it->second->GetWatchNode() == &nn[i]) {
					std::unique_ptr<CAsyncPutNodesNotify> notify(it->second.release());
					Node* n = FindNodeFromPath(it->first);
					m_mapAsyncPutNodesNotify.erase(it);
					lock.Unlock();

					notify->Notify(e != API_OK ? nullptr : n);
					delete[] nn;
					return;
				}
			}
		}		
	}
	delete[] nn;
	if (e != API_OK) {
		ATLTRACE(L"Node addition failed ( %s )\n", errorstring(e));
	}
}


void CMegaAppImpl::share_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Share creation/modification request failed ( %s )\n", errorstring(e));
	}
}

void CMegaAppImpl::share_result(int, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Share creation/modification failed ( %s )", errorstring(e));
	} else {
		ATLTRACE(L"Share creation/modification succeeded\n");
	}
}

void CMegaAppImpl::fa_complete(Node* n, fatype type, const char* data, uint32_t len)
{
	ATLTRACE( (format("Got attribute of type %d (%d bytes) for %s\n") % type % len % n->displayname()).str().c_str() );
}

int CMegaAppImpl::fa_failed(handle, fatype type, int retries)
{
	ATLTRACE(L"File attribute retrieval of type %d failed (retries: %d)\n", type, retries);

	return retries > 2;
}

void CMegaAppImpl::putfa_result(handle, fatype, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"File attribute attachment failed ( %s )\n", errorstring(e));
	}
}


void CMegaAppImpl::invite_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Invitation failed ( %s )\n", errorstring(e));
	} else {
		ATLTRACE(L"Success.\n");
	}
}

void CMegaAppImpl::putua_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"User attribute update failed ( %s )\n", errorstring(e));
	} else {
		ATLTRACE(L"Success.\n");
	}
}

void CMegaAppImpl::getua_result(error e)
{
	ATLTRACE(L"User attribute retrieval failed ( %s )\n", errorstring(e));
}

void CMegaAppImpl::getua_result(byte* data, unsigned l)
{
	ATLTRACE(L"Received %d byte(s) of user attribute: \n", l);
	//fwrite(data, 1, l, stdout);
}


// node export failed
void CMegaAppImpl::exportnode_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Export failed: %s\n", errorstring(e));
	}
}

void CMegaAppImpl::exportnode_result(handle h, handle ph)
{
	Node* n;
	if (n = client->nodebyhandle(h)) {
		std::string path = _NodePath(h);
		ATLTRACE(L"Exported %s: ", CA2W(path.c_str()));

		char node[9];
		Base64::btoa((byte*)&ph, MegaClient::NODEHANDLE, node);

		char key[Node::FILENODEKEYLENGTH * 4 / 3 + 3];
		// the key
		if (n->type == FILENODE) {
			Base64::btoa((const byte*)n->nodekey.data(), Node::FILENODEKEYLENGTH, key);
		} else if (n->sharekey) {
			Base64::btoa(n->sharekey->key, Node::FOLDERNODEKEYLENGTH, key);
		} else {
			ATLTRACE(L"No key available for exported folder\n");
			return;
		}
		ATLTRACE( (format("https://mega.co.nz/#%s!%s!%s\n") % (n->type ? "F" : "") % node % key).str().c_str() );

	} else {
		ATLTRACE(L"Exported node no longer available\n");
	}
}


// the requested link could not be opened
void CMegaAppImpl::openfilelink_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Failed to open link: %s\n", errorstring(e));
	}
}

// the requested link was opened successfully
// (it is the application's responsibility to delete n!)
void CMegaAppImpl::openfilelink_result(Node* n)
{
	ATLTRACE(L"Importing %s...\n", n->displayname());

	if (client->loggedin()) {
		NewNode* newnode = new NewNode[1];
		string attrstring;

		// copy core properties
		*(NodeCore*)newnode = *(NodeCore*)n;

		// generate encrypted attribute string
		n->attrs.getjson(&attrstring);
		client->makeattr(&n->key, &newnode->attrstring, attrstring.c_str());

		delete n;

		newnode->source = NEW_PUBLIC;
		newnode->parenthandle = UNDEF;

		// add node
		ATLASSERT(FALSE);
		//client->putnodes(cwd, newnode, 1);

	} else {
		ATLTRACE(L"Need to be logged in to import file links.\n");
	}
}


// topen() failed
void CMegaAppImpl::topen_result(int td, error e)
{
	ATLTRACE(L"TD %d: Failed to open file ( %s )\n", td, errorstring(e));

	client->tclose(td);
}

// topen() succeeded (download only)
void CMegaAppImpl::topen_result(int td, string* filename, const char* fa, int pfa)
{
	ATLTRACE(L"TD %d: File opened successfully, filename: %s\n", td, filename->c_str());

	if (fa && debug) {
		ATLTRACE(L"File has attributes: %s/%d\n", CA2W(fa), pfa);
	}

	//MegaClient::escapefilename(filename);
	std::string utf8name = ConvertUTF8fromUTF16(
		(LPCWSTR)GetFileOrFolderName(client->ft[td].donwloadFilePath.c_str()));
	ATLASSERT(utf8name == *filename);
	string tmpfilename = ConvertUTF8fromUTF16(client->ft[td].donwloadFilePath);
	tmpfilename.append(".tmp");

	client->dlopen(td, tmpfilename.c_str());
	
	m_pTransactionView->TransferUpdate(td, 0, client->ft[td].size, 1, utf8name, true);
}


void CMegaAppImpl::transfer_update(int td, m_off_t bytes, m_off_t size, dstime starttime)
{
//	ATLTRACE(L"TD %d: update: %d KB of %d KB, %d KB/s\n",
//		td, int(bytes / 1024), int(size / 1024), int(bytes * 10 / (1024 * (client->httpio->ds - starttime) + 1)));

	std::string name;
	for (auto it = client->putq.begin(); it != client->putq.end(); ++it) {
		if ((*it)->td == td) {
			name = (*it)->newname;
			break;
		}
	}
	m_pTransactionView->TransferUpdate(td, bytes, size, (client->httpio->ds - starttime), name, false);
}

// returns value 0: continue transfer, 1: implicit tclose(), transfer abort
int CMegaAppImpl::transfer_error(int td, int httpcode, int count)
{
	ATLTRACE(L"TD %d: Failed, HTTP error code %d (count %d)\n", td, httpcode, count);
#if 0
	std::string name;
	for (auto it = client->putq.begin(); it != client->putq.end(); ++it) {
		if ((*it)->td == td) {
			std::wstring filePath = ConvertUTF16fromUTF8((*it)->filename);
			CCritSecLock lock(m_csAsyncAddQue);
			auto itfound = m_mapAsyncAddQueNotify.find(filePath);
			if (itfound != m_mapAsyncAddQueNotify.end()) {
				itfound->second->Notify(filePath);
				m_mapAsyncAddQueNotify.erase(itfound);
			}
			m_pTransactionView->TransferComplete(td, false, (int)client->putq.size());
			break;
		}
	}
#endif
	return 0;
}

// download failed: tclose() is implicitly invoked by the engine
void CMegaAppImpl::transfer_failed(int td, string& filename, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"TD %d: Download failed ( %s )\n", td, errorstring(e));
	}
	
	std::wstring tempfilePath = client->ft[td].donwloadFilePath + L".tmp";
	DeleteFileW(tempfilePath.c_str());

	{
		CCritSecLock lock(m_csAsyncDL);
		auto itfound = m_mapAsyncDLNotify.find(client->ft[td].donwloadFilePath);
		if (itfound != m_mapAsyncDLNotify.end()) {
			std::unique_ptr<CAsyncDLNotify> DLNotify(itfound->second.release());
			m_mapAsyncDLNotify.erase(itfound);
			lock.Unlock();

			DLNotify->Notify(client->ft[td].donwloadFilePath);
		} else {
			ATLASSERT(FALSE);
		}
	}

	m_pTransactionView->TransferComplete(td, false);
}

// upload failed: tclose() is implicitly invoked by the engine
void CMegaAppImpl::transfer_failed(int td, error e)
{
	if (e != API_OK) {
		ATLTRACE(L"TD %d: Upload failed ( %s )\n", td, errorstring(e));
		for (auto& que : client->putq) {
			if (que->td == td) {
				std::wstring filePath = ConvertUTF16fromUTF8(que->filename);
				CCritSecLock lock(m_csAsyncAddQue);
				auto itfound = m_mapAsyncAddQueNotify.find(filePath);
				if (itfound != m_mapAsyncAddQueNotify.end()) {
					itfound->second->Notify(filePath);
					m_mapAsyncAddQueNotify.erase(itfound);
				}
				break;
			}
		}
		m_pTransactionView->TransferComplete(td, false);
	}
}

void CMegaAppImpl::upload_fileopen_failed(const std::string& filename)
{
	std::wstring filePath = ConvertUTF16fromUTF8(filename);
	CCritSecLock lock(m_csAsyncAddQue);
	auto itfound = m_mapAsyncAddQueNotify.find(filePath);
	if (itfound != m_mapAsyncAddQueNotify.end()) {
		PUTLOG(L"アップロードファイルのオープンに失敗。: %s", filePath.c_str());
		itfound->second->Notify(filePath);
		m_mapAsyncAddQueNotify.erase(itfound);
	}
}

void CMegaAppImpl::download_fileopen_failed(const std::wstring& downloadFilePath)
{
	CCritSecLock lock(m_csAsyncDL);
	auto itfound = m_mapAsyncDLNotify.find(downloadFilePath);
	if (itfound != m_mapAsyncDLNotify.end()) {
		std::unique_ptr<CAsyncDLNotify> DLNotify(itfound->second.release());
		m_mapAsyncDLNotify.erase(itfound);
		lock.Unlock();

		PUTLOG(L"ダウンロードファイルのオープンに失敗。: %s", downloadFilePath.c_str());
		DLNotify->Notify(downloadFilePath);
	} else {
		ATLASSERT(FALSE);
	}
}

void CMegaAppImpl::transfer_limit(int td)
{
	ATLTRACE(L"TD %d: Transfer limit reached, retrying later...\n", td);
}

void CMegaAppImpl::transfer_complete(int td, chunkmac_map* macs, const char* fn)
{
	ATLTRACE(L"TD %d: Download complete\n", td);

	std::wstring tmpfilename = client->ft[td].donwloadFilePath + L".tmp";
	std::wstring filename = client->ft[td].donwloadFilePath;

	client->tclose(td);

	if (MoveFileExW(tmpfilename.c_str(), filename.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		ATLTRACE(L"TD %d: Download complete: %s\n", td, filename.c_str());
	} else {
		ATLTRACE(L"TD %d: rename(%s, %s) failed\n", td, tmpfilename.c_str(), filename.c_str());
	}

	{
		CCritSecLock lock(m_csAsyncDL);
		auto itfound = m_mapAsyncDLNotify.find(client->ft[td].donwloadFilePath);
		if (itfound != m_mapAsyncDLNotify.end()) {
			std::unique_ptr<CAsyncDLNotify> DLNotify(itfound->second.release());
			m_mapAsyncDLNotify.erase(itfound);
			lock.Unlock();

			DLNotify->Notify(client->ft[td].donwloadFilePath);
		} else {
			ATLASSERT(FALSE);
		}
	}
	m_pTransactionView->TransferComplete(td, true);

	delete client->gettransfer((transfer_list*)&client->getq, td);
}

void CMegaAppImpl::transfer_complete(int td, handle ulhandle, const byte* ultoken, const byte* filekey, SymmCipher* key)
{
	ATLTRACE(L"TD %d: Upload complete\n", td);

	m_pTransactionView->TransferComplete(td, true);

	FilePut* putf = (FilePut*)client->gettransfer((transfer_list*)&client->putq, td);

	std::wstring filePath = ConvertUTF16fromUTF8(putf->filename);
	CCritSecLock lock(m_csAsyncAddQue);
	auto itfound = m_mapAsyncAddQueNotify.find(filePath);
	if (itfound != m_mapAsyncAddQueNotify.end()) {
		itfound->second->Notify(filePath);
		m_mapAsyncAddQueNotify.erase(itfound);
	}

	if (putf) {
		if (!putf->targetuser.size() && !client->nodebyhandle(putf->target)) {
			ATLTRACE(L"Upload target folder inaccessible, using /\n");
			putf->target = client->rootnodes[0];
		}

		NewNode* newnode = new NewNode[1];

		// build new node
		newnode->source = NEW_UPLOAD;

		// upload handle required to retrieve/include pending file attributes
		newnode->uploadhandle = ulhandle;

		// reference to uploaded file
		memcpy(newnode->uploadtoken, ultoken, sizeof newnode->uploadtoken);

		// file's crypto key
		newnode->nodekey.assign((char*)filekey, Node::FILENODEKEYLENGTH);
		newnode->mtime = client->ft[td].file->mtime;
		newnode->ctime = time(NULL);
		newnode->type = FILENODE;
		newnode->parenthandle = UNDEF;

		AttrMap attrs;

		MegaClient::unescapefilename(&putf->filename);

		attrs.map['n'] = putf->newname.size() ? putf->newname : putf->filename;

		attrs.getjson(&putf->filename);

		client->makeattr(key, &newnode->attrstring, putf->filename.c_str());

		if (putf->targetuser.size()) {
			ATLTRACE(L"Attempting to drop file into user %s's inbox...\n", CA2W(putf->targetuser.c_str()));
			client->putnodes(putf->targetuser.c_str(), newnode, 1);
		} else {
			client->putnodes(putf->target, newnode, 1);
		}

		delete putf;
	} else {
		ATLTRACE(L"(Canceled transfer, ignoring)\n");
	}

	client->tclose(td);
}


// password change result
void CMegaAppImpl::changepw_result(error e)
{
	if (e != API_OK) {
		ATLTRACE(L"Password update failed: %s\n", errorstring(e));
	} else {
		ATLTRACE(L"Password updated.\n");
	}
}

// user attribute update notification
void CMegaAppImpl::userattr_update(User* u, int priv, const char* n)
{
	ATLTRACE(L"Notification: User %s -%s attribute %s added or updated\n",
		u->email.c_str(), (priv ? " private" : ""), n);
}


// reload needed
void CMegaAppImpl::reload(const char* reason)
{
	ATLTRACE(L"Reload suggested ( %s ) - use 'reload' to trigger\n", CA2W(reason));
}

// reload initiated
void CMegaAppImpl::clearing()
{
	if (debug)
		PUTLOG(L"Clearing all nodes/users...\n");
}

void CMegaAppImpl::notify_retry(dstime dsdelta)
{
	ATLTRACE("API request failed, retrying in %d ms - Use 'retry' to retry immediately...\n", dsdelta * 100);
}

void CMegaAppImpl::debug_log(const char* message)
{
	if (debug)
		PUTLOG(L"DEBUG: %s\n", CA2W(message));
}





std::string CMegaAppImpl::_NodePath(handle h)
{
	if (h == client->rootnodes[0]) {
		return "/";
	}

	Node* n = client->nodebyhandle(h);
	std::string path;
	while (n) {
		switch (n->type)
		{
			case FOLDERNODE:
			path.insert(0, n->displayname());

			if (n->inshare) {
				path.insert(0, ":");
				if (n->inshare->user) {
					path.insert(0, n->inshare->user->email);
				} else {
					path.insert(0, "UNKNOWN");
				}
				return path;
			}
			break;

			case INCOMINGNODE:
			path.insert(0, "//in");
			return path;

			case ROOTNODE:
			return path;

			case RUBBISHNODE:
			path.insert(0, "//bin");
			return path;

			case MAILNODE:
			path.insert(0, "//mail");
			return path;

			case TYPE_UNKNOWN:
			case FILENODE:
			path.insert(0, n->displayname());
		}

		path.insert(0, "/");

		n = n->parent;
	}
	return path;
}



