/*
*	@file	OptionDialog.h
*/

#include "stdafx.h"
#include "OptionDialog.h"
#include <string>
#include <atlctrls.h>
#include <atlddx.h>
#include <atlenc.h>
#include <boost\tokenizer.hpp>
#include "Utility.h"
#include "ptreeWrapper.h"
#include "MegaAppImpl.h"

#include <wincrypt.h>
#pragma comment (lib, "crypt32.lib")

namespace {

	// 暗号化してBase64Encodeして返す
	bool	EncryptString(const CString& data, std::wstring& encryptText)
	{
		DATA_BLOB	DataOut;
		DATA_BLOB	DataIn;
		DataIn.pbData = (BYTE*)(LPCWSTR)data;
		DataIn.cbData = data.GetLength() * sizeof(WCHAR);
		if (!CryptProtectData(&DataIn, NULL, NULL, NULL, NULL, 0, &DataOut)) {
			return false;
		}
		int reqLength = Base64EncodeGetRequiredLength(DataOut.cbData, ATL_BASE64_FLAG_NOCRLF);
		auto buff = std::make_unique<BYTE[]>(reqLength);
		if (!Base64Encode(DataOut.pbData, DataOut.cbData, (LPSTR)buff.get(), &reqLength, ATL_BASE64_FLAG_NOCRLF))
			return false;
		std::string temp((LPCSTR)buff.get(), reqLength);
		encryptText = (LPWSTR)CA2W(temp.c_str());
		return true;
	}

	// Base64Decodeして符号化して返す
	bool	DecryptString(const CString& data, std::wstring& decyptText)
	{
		std::string temp = (LPSTR)CW2A(data);
		int reqLength = Base64DecodeGetRequiredLength(temp.length());
		auto buff = std::make_unique<BYTE[]>(reqLength);
		if (!Base64Decode(temp.data(), temp.size(), buff.get(), &reqLength))
			return false;

		DATA_BLOB	DataOut;
		DATA_BLOB	DataIn;
		DataIn.cbData = reqLength;
		DataIn.pbData = buff.get();
		if (!CryptUnprotectData(&DataIn, NULL, NULL, NULL, NULL, 0, &DataOut)) {
			return false;
		}
		decyptText.assign((LPCWSTR)DataOut.pbData, DataOut.cbData / sizeof(WCHAR));
		return true;
	}

}	// namespace

///////////////////////////////////////////////////
// CConfig

CString	CConfig::s_mailaddress;
CString	CConfig::s_password;

CString	CConfig::s_syncFolderPath;
bool	CConfig::s_bAutoSync = true;

bool	CConfig::s_bDownload = false;
CString	CConfig::s_targetMegaFolder;
CString	CConfig::s_DLTargetFolder;

std::unordered_set<std::wstring>	CConfig::s_setExcludeExt;

int		CConfig::s_nMaxUploadFileCount = 3;
int		CConfig::s_nMaxDownloadFileCount = 3;


void	CConfig::LoadConfig()
{
	auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));

	s_mailaddress = pt.get(L"Config.mailaddress", L"").c_str();
	std::wstring pass;
	CString encryptPass = pt.get(L"Config.password", L"").c_str();
	if (encryptPass.GetLength()) {
		std::wstring decryptPass;
		if (DecryptString(encryptPass, decryptPass)) {
			s_password = decryptPass.c_str();
		} else {
			MessageBox(NULL, _T("パスワードの符号化に失敗しました。"), _T("エラー"), MB_OK | MB_ICONERROR);
		}
	}

	s_syncFolderPath = pt.get(L"Config.syncFolderPath", L"").c_str();
	s_bAutoSync = pt.get<bool>(L"Config.AutoSync", s_bAutoSync);

	s_targetMegaFolder = pt.get(L"Config.targetMegaFolder", L"").c_str();
	s_DLTargetFolder = pt.get(L"Config.DLTargetFolder", L"").c_str();

	std::wstring excludeExt = pt.get(L"Config.ExcludeExt", L"");
	boost::char_separator<wchar_t>	sep(L";");
	boost::tokenizer<boost::char_separator<wchar_t>, std::wstring::const_iterator, std::wstring> takenizer(excludeExt, sep);
	for (auto it = takenizer.begin(); it != takenizer.end(); ++it)
		s_setExcludeExt.insert(*it);

	if (auto value = pt.get_optional<int>(L"Config.MaxUploadFileCount"))
		s_nMaxUploadFileCount = value.get();
	if (auto value = pt.get_optional<int>(L"Config.MaxDownloadFileCount"))
		s_nMaxDownloadFileCount = value.get();
}

void	CConfig::SaveConfig()
{
	auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));

	pt.put(L"Config.mailaddress", (LPCWSTR)s_mailaddress);
	std::wstring cryptPass;
	if (EncryptString(s_password, cryptPass)) {
		pt.put(L"Config.password", cryptPass);
	} else {
		MessageBox(NULL, _T("パスワードの暗号化に失敗しました。"), _T("エラー"), MB_OK | MB_ICONERROR);
	}

	pt.put(L"Config.syncFolderPath", (LPCWSTR)s_syncFolderPath);
	pt.put<bool>(L"Config.AutoSync", s_bAutoSync);

	std::wstring excludeExt;
	for (auto& ext : s_setExcludeExt) {
		excludeExt += ext + L";";
	}
	pt.put(L"Config.ExcludeExt", excludeExt);

	pt.put(L"Config.MaxUploadFileCount", s_nMaxUploadFileCount);
	pt.put(L"Config.MaxDownloadFileCount", s_nMaxDownloadFileCount);

	ATLVERIFY(ptreeWrapper::SaveIniPtree(_T("setting.ini"), pt));
}

void	CConfig::ClearDLTarget()
{
	auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));
	pt.put(L"Config.targetMegaFolder", L"");
	pt.put(L"Config.DLTargetFolder", L"");
	ATLVERIFY(ptreeWrapper::SaveIniPtree(_T("setting.ini"), pt));
}

//////////////////////////////////////////////
/// 全般
class CGeneralPropertyPage : 
	public CPropertyPageImpl<CGeneralPropertyPage>,
	public CWinDataExchange<CGeneralPropertyPage>,
	protected CConfig
{
public:
	enum { IDD = IDD_OPTION };

	CGeneralPropertyPage() {}
	~CGeneralPropertyPage() {
		CMegaAppImpl::GetMegaAppInstance()->SetLoginResultCallback(std::function<void(bool)>());
	}

	BEGIN_DDX_MAP(CMainDlg)
		DDX_TEXT(IDC_EDIT_MAIL, s_mailaddress)
		DDX_TEXT(IDC_EDIT_PASS, s_password)
		DDX_TEXT(IDC_EDIT_SYNCFOLDERPAHT, s_syncFolderPath)
		DDX_CHECK(IDC_CHECKBOX_AUTOSYNC, s_bAutoSync)

		DDX_CONTROL_HANDLE(IDC_COMBO_MEGAFOLDER, m_cmbMegaFolder)
		DDX_CONTROL_HANDLE(IDC_COMBO_EXCLUDEEXT, m_cmbExcludeExt)
		DDX_CONTROL_HANDLE(IDC_SPIN_MAXUPCOUNT, m_updownMaxUpCount)
		DDX_CONTROL_HANDLE(IDC_SPIN_MAXDOWNCOUNT, m_updownMaxDownCount)
	END_DDX_MAP()

	BEGIN_MSG_MAP_EX(CGeneralPropertyPage)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_LOGIN, OnLogin)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_FOLDERSELECT, OnSyncFolderSelect)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_DLFOLDERSELECT, OnSyncFolderSelect)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_DLSTART, OnDLStart)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_ADDEXCLUDEEXT, OnAddExcludeExt)
		COMMAND_ID_HANDLER_EX(IDC_BUTTON_REMOVEEXCLUDEEXT, OnRemoveExcludeExt)

		CHAIN_MSG_MAP(CPropertyPageImpl<CGeneralPropertyPage>)
	END_MSG_MAP()

	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
	{
		DoDataExchange(DDX_LOAD);

		m_updownMaxUpCount.SetRange(1, 8);
		m_updownMaxUpCount.SetPos(s_nMaxUploadFileCount);
		m_updownMaxDownCount.SetRange(1, 8);
		m_updownMaxDownCount.SetPos(s_nMaxDownloadFileCount);

		if (CMegaAppImpl::GetMegaAppInstance()->IsLogined()) {
			GetDlgItem(IDC_BUTTON_LOGIN).SetWindowTextW(L"ログアウト");

			Node* rootNode = CMegaAppImpl::GetMegaAppInstance()->FindNodeFromPath(L"");
			for (auto& childNode : rootNode->children) {
				if (childNode->type != nodetype::FOLDERNODE)
					continue;
				std::wstring childFoldreName = ConvertUTF16fromUTF8(childNode->displayname_string());
				m_cmbMegaFolder.AddString(childFoldreName.c_str());
			}
		}
		CMegaAppImpl::GetMegaAppInstance()->SetLoginResultCallback([this](bool bSuccess) {
			GetDlgItem(IDC_BUTTON_LOGIN).EnableWindow(TRUE);
			if (bSuccess) {
				GetDlgItem(IDC_BUTTON_LOGIN).SetWindowTextW(L"ログアウト");
			} else {
				GetDlgItem(IDC_BUTTON_LOGIN).SetWindowTextW(L"ログイン");
			}
		});
		s_bDownload = false;

		std::list<std::wstring> exts;
		for (auto& ext : s_setExcludeExt) {
			exts.push_back(ext);
		}
		exts.sort();

		for (auto& ext : exts) {
			m_cmbExcludeExt.AddString(ext.c_str());
		}
		return TRUE;
	}

	BOOL OnApply()
	{
		CString back = s_syncFolderPath;
		DoDataExchange(DDX_SAVE, IDC_EDIT_SYNCFOLDERPAHT);
		if (::PathIsDirectory(s_syncFolderPath) == FALSE) {
			CString text;
			text.Format(L"同期するフォルダが存在しません。: %s", s_syncFolderPath);
			MessageBox(text);
			s_syncFolderPath = back;
			return FALSE;
		} else if (::PathIsRoot(s_syncFolderPath)) {
			MessageBox(_T("ルートは選択できません"));
			s_syncFolderPath = back;
			return FALSE;
		}
		DoDataExchange(DDX_SAVE);
		s_nMaxUploadFileCount = m_updownMaxUpCount.GetPos();
		s_nMaxDownloadFileCount = m_updownMaxDownCount.GetPos();
		CConfig::SaveConfig();
		return TRUE;
	}

	void OnSyncFolderSelect(UINT uNotifyCode, int nID, CWindow wndCtl)
	{
		CFolderDialog dlg(NULL, 
			nID == IDC_BUTTON_DLFOLDERSELECT ? _T("ダウンロード先のフォルダを選択してください") 
											 : _T("同期するフォルダを選択してください。"));
		if (dlg.DoModal(m_hWnd) == IDOK) {
			if (::PathIsRoot(dlg.m_szFolderPath)) {
				MessageBox(_T("ルートは選択できません"));
				return;
			}
			if (nID == IDC_BUTTON_DLFOLDERSELECT) {
				GetDlgItem(IDC_EDIT_DLTARGETFOLDER).SetWindowText(dlg.m_szFolderPath);
			} else {
				GetDlgItem(IDC_EDIT_SYNCFOLDERPAHT).SetWindowText(dlg.m_szFolderPath);
			}
		}
	}

	void OnLogin(UINT uNotifyCode, int nID, CWindow wndCtl)
	{
		if (CMegaAppImpl::GetMegaAppInstance()->IsLogined()) {
			CMegaAppImpl::GetMegaAppInstance()->Logout();
			GetDlgItem(IDC_BUTTON_LOGIN).SetWindowTextW(L"ログイン");
		} else {
			DoDataExchange(DDX_SAVE, IDC_EDIT_MAIL);
			DoDataExchange(DDX_SAVE, IDC_EDIT_PASS);
			if (s_mailaddress.GetLength() && s_password.GetLength()) {
				GetDlgItem(IDC_BUTTON_LOGIN).SetWindowTextW(L"ログイン中...");
				GetDlgItem(IDC_BUTTON_LOGIN).EnableWindow(FALSE);
				CMegaAppImpl::GetMegaAppInstance()->Login(CW2A(s_mailaddress), CW2A(s_password));
			}
		}
	}

	void OnDLStart(UINT uNotifyCode, int nID, CWindow wndCtl)
	{
		int nSel = m_cmbMegaFolder.GetCurSel();
		if (nSel == -1)
			return;
		CString megaFolder;
		m_cmbMegaFolder.GetLBText(nSel, megaFolder);

		CString DLFolderTarget;
		GetDlgItem(IDC_EDIT_DLTARGETFOLDER).GetWindowTextW(DLFolderTarget.GetBuffer(MAX_PATH), MAX_PATH);
		DLFolderTarget.ReleaseBuffer();
		if (::PathIsDirectory(DLFolderTarget) == FALSE) {
			MessageBox(_T("ダウンロード先フォルダが存在しません。"));
			return;
		}

		CString text;
		text.Format(_T("Megaのフォルダ:「%s」内のファイル/フォルダを\n「%s」へダウンロードします。\nよろしいですか？"), megaFolder, DLFolderTarget);
		int ret = MessageBox(text, _T("確認"), MB_YESNO | MB_ICONQUESTION);
		if (ret == IDNO)
			return;

		s_bAutoSync = false;
		s_targetMegaFolder = megaFolder;
		s_DLTargetFolder = DLFolderTarget;
		s_nMaxDownloadFileCount = m_updownMaxDownCount.GetPos();

		{
			auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));
			pt.put<bool>(L"Config.AutoSync", s_bAutoSync);

			pt.put(L"Config.targetMegaFolder", (LPCWSTR)s_targetMegaFolder);
			pt.put(L"Config.DLTargetFolder", (LPCWSTR)s_DLTargetFolder);

			pt.put(L"Config.MaxDownloadFileCount", s_nMaxDownloadFileCount);

			ATLVERIFY(ptreeWrapper::SaveIniPtree(_T("setting.ini"), pt));
		}
		s_bDownload = true;
		GetParent().PostMessage(WM_CLOSE);
	}

	void OnAddExcludeExt(UINT uNotifyCode, int nID, CWindow wndCtl)
	{
		CString ext;
		COMBOBOXINFO cmbInfo = { sizeof(COMBOBOXINFO) };
		m_cmbExcludeExt.GetComboBoxInfo(&cmbInfo);
		::GetWindowText(cmbInfo.hwndItem, ext.GetBuffer(MAX_PATH), MAX_PATH);
		ext.ReleaseBuffer();
		if (ext.IsEmpty())
			return;
		ext.MakeLower();
		if (ext[0] == L'.')
			ext.Delete(0);
		if (s_setExcludeExt.insert(std::wstring((LPCWSTR)ext)).second)
			m_cmbExcludeExt.AddString(ext);
	}

	void OnRemoveExcludeExt(UINT uNotifyCode, int nID, CWindow wndCtl)
	{
		int nSel = m_cmbExcludeExt.GetCurSel();
		if (nSel == -1)
			return;
		CString ext;
		m_cmbExcludeExt.GetLBText(nSel, ext);
		m_cmbExcludeExt.DeleteString(nSel);
		s_setExcludeExt.erase(std::wstring((LPCWSTR)ext));
	}

private:
	CComboBox	m_cmbMegaFolder;
	CComboBox	m_cmbExcludeExt;
	CUpDownCtrl	m_updownMaxUpCount;
	CUpDownCtrl	m_updownMaxDownCount;
};








//////////////////////////////////////////////////////////////////
// COptionSheet

/// プロパティシートを表示
INT_PTR	COptionSheet::Show(HWND hWndParent)
{
	SetTitle(_T("設定"));
	m_psh.dwFlags |= PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;

	CGeneralPropertyPage	generalPage;
	AddPage(generalPage);

	return DoModal(hWndParent);
}

















