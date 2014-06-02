/**
*	@file	MainDlg.cpp
*/

#include "stdafx.h"
#include "MainDlg.h"

#include <unordered_map>
#include <boost\property_tree\ptree.hpp>
#include <boost\property_tree\ini_parser.hpp>

#include "Utility.h"
#include "aboutdlg.h"
#include "DirectoryWatcher.h"
#include "OptionDialog.h"
#include "ptreeWrapper.h"
#include "AppConst.h"

////////////////////////////////////////////////////////////
// CMainDlg

CMainDlg*	CMainDlg::s_pMainDlg = nullptr;

void	CMainDlg::PutLog(LPCWSTR strFormat, ...)
{
	if (s_pMainDlg) {
		va_list args;
		va_start(args, strFormat);
		CString str;
		str.FormatV(strFormat, args);
		va_end(args);

		enum { kMaxLogCount = 300 };
		if (kMaxLogCount < s_pMainDlg->m_logListBox.GetCount())
			s_pMainDlg->m_logListBox.DeleteString(0);

		int nAddIndex = s_pMainDlg->m_logListBox.AddString(str);
		s_pMainDlg->m_logListBox.SetCurSel(nAddIndex);
	}
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	s_pMainDlg = this;

	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(hIconSmall, FALSE);

	CWindow wndView = GetDlgItem(IDC_TRANSACTIONVIEWPOS);
	CRect rcView;
	wndView.GetClientRect(&rcView);
	wndView.MapWindowPoints(m_hWnd, &rcView);
	wndView.SetDlgCtrlID(0);
	wndView.ShowWindow(FALSE);
	m_transactionView.Create(m_hWnd, rcView, nullptr
		, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER);
	m_transactionView.SetDlgCtrlID(IDC_TRANSACTIONVIEWPOS);

	m_logListBox = GetDlgItem(IDC_LIST_LOG);

	// �_�C�A���O���T�C�Y������
	DlgResize_Init(true, true, WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN);

	// �ʒu�𕜌�
	auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));
	CRect rcWindow;
	rcWindow.top = pt.get(L"MainWindow.top", 0);
	rcWindow.left = pt.get(L"MainWindow.left", 0);
	rcWindow.right = pt.get(L"MainWindow.right", 0);
	rcWindow.bottom = pt.get(L"MainWindow.bottom", 0);
	if (rcWindow != CRect()) {
		MoveWindow(&rcWindow);
	}
	GetClientRect(&rcWindow);
	DlgResize_UpdateLayout(rcWindow.Width(), rcWindow.Height());
	if (pt.get(L"MainWindow.ShowWindow", true))
		ShowWindow(TRUE);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	UIAddChildWindowContainer(m_hWnd);

	{	// �g���C�A�C�R�����쐬
		NOTIFYICONDATA	nid = { sizeof(NOTIFYICONDATA) };
		nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		nid.hWnd = m_hWnd;
		nid.hIcon = hIconSmall;
		nid.uID = kTrayIconId;
		nid.uCallbackMessage = WM_TRAYICONNOTIFY;
		::_tcscpy_s(nid.szTip, APP_NAME);
		::Shell_NotifyIcon(NIM_ADD, &nid);
	}

	// init
	CMegaAppImpl::GetMegaAppInstance()->SetTransactionViewPtr(&m_transactionView);
	m_syncManager.SetMainWindowHandle(m_hWnd);
	m_syncManager.SetDLCompleteCallback([this]() {
		GetDlgItem(IDOK).SetWindowTextW(L"����");
	});
	CDirectoryWatcher::Init();
	CDirectoryWatcher::SetMainWindowHandle(m_hWnd);

	CConfig::LoadConfig();

	// �������O�C��
	GetDlgItem(IDOK).EnableWindow(FALSE);
	if (CConfig::s_mailaddress.GetLength() && CConfig::s_password.GetLength()) {
		m_bFirstAutoSync = CConfig::s_bAutoSync;
		CMegaAppImpl::GetMegaAppInstance()->SetLoginResultCallback([this](bool bSuccess) {
			if (bSuccess == false) {
				m_bFirstAutoSync = false;
			}
		});
		CMegaAppImpl::GetMegaAppInstance()->Login(
			CW2A(CConfig::s_mailaddress), CW2A(CConfig::s_password));
	}

	CMegaAppImpl::GetMegaAppInstance()->SetLoginSuccessCallback([this]() {
		GetDlgItem(IDOK).EnableWindow(TRUE);
		if (CConfig::s_targetMegaFolder.GetLength() && CConfig::s_DLTargetFolder.GetLength()) {
			int ret = MessageBox(_T("�O��̃_�E�����[�h���������Ă��܂���B\n�ĊJ���܂����H"), _T("�m�F"), MB_YESNO | MB_ICONQUESTION);
			if (ret == IDYES) {
				if (m_syncManager.DownloadStart(CConfig::s_targetMegaFolder, CConfig::s_DLTargetFolder)) {
					GetDlgItem(IDOK).SetWindowTextW(L"�_�E�����[�h���~");
				}
			} else {
				CConfig::ClearDLTarget();
			}
			return;
		}
		if (m_bFirstAutoSync) {	// ��������
			m_bFirstAutoSync = false;
			BOOL bHandled = TRUE;
			OnOK(0, 0, NULL, bHandled);
		}
	});
	CMegaAppImpl::GetMegaAppInstance()->SetLogoutPerformCallback([this]() {
		GetDlgItem(IDOK).EnableWindow(FALSE);
		if (m_syncManager.IsSyncing()) {
			m_syncManager.SyncStop();
			GetDlgItem(IDOK).SetWindowTextW(L"����");
		}
	});

	return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	s_pMainDlg = nullptr;
	CMegaAppImpl::GetMegaAppInstance()->SetLoginSuccessCallback(std::function<void()>());

	auto pt = ptreeWrapper::LoadIniPtree(_T("setting.ini"));

	CRect rcWindow;
	GetWindowRect(&rcWindow);

	if (m_bVisibleOnDestroy) {
		pt.put(L"MainWindow.top", rcWindow.top);
		pt.put(L"MainWindow.left", rcWindow.left);
		pt.put(L"MainWindow.right", rcWindow.right);
		pt.put(L"MainWindow.bottom", rcWindow.bottom);
	}

	pt.put(L"MainWindow.ShowWindow", m_bVisibleOnDestroy);

	ptreeWrapper::SaveIniPtree(_T("setting.ini"), pt);

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	CDirectoryWatcher::Term();

	{	// �g���C�A�C�R�����폜
		NOTIFYICONDATA	nid = { sizeof(NOTIFYICONDATA) };
		nid.hWnd = m_hWnd;
		nid.uID = kTrayIconId;
		::Shell_NotifyIcon(NIM_DELETE, &nid);
	}

	//CMegaAppImpl::GetMegaAppInstance()->Logout();
	m_syncManager.SyncStop();

	return 0;
}


void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
	CDirectoryWatcher::TimerCallback(nIDEvent);
}

void CMainDlg::OnSize(UINT nType, CSize size)
{
	if (nType == SIZE_MINIMIZED) {
		ShowWindow(FALSE);
	} else {
		SetMsgHandled(FALSE);
	}
}

LRESULT CMainDlg::OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (lParam == WM_LBUTTONDOWN) {
		ShowWindow(TRUE);
		SetForegroundWindow(m_hWnd);
	} else if (lParam == WM_RBUTTONUP) {
		enum {
			kOpenSyncFolder = 100,
			kOpenMegaSite,
			kStopStartSync,
			kExit,
		};
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, kOpenSyncFolder, _T("�����t�H���_���J��"));
		menu.AppendMenu(MF_STRING, kOpenMegaSite, _T("Mega�̃T�C�g���J��"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, kStopStartSync,
			m_syncManager.IsSyncing() ? _T("�����𒆎~") : _T("�������J�n"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, kExit, _T("�I��"));
		CPoint pt;
		::GetCursorPos(&pt);
		::SetForegroundWindow(m_hWnd);
		BOOL bRet = menu.TrackPopupMenu(TPM_RETURNCMD, pt.x, pt.y, m_hWnd, NULL);
		switch (bRet) {
			case kOpenSyncFolder:
			{
				BOOL bHandled = TRUE;
				OnOpenSyncFolder(0, 0, NULL, bHandled);
			}
			break;

			case kOpenMegaSite:
			{
				::ShellExecute(NULL, _T("open"), _T("https://mega.co.nz/"), NULL, NULL, SW_SHOWNORMAL);
			}
			break;

			case kStopStartSync:
			{
				BOOL bHandled = TRUE;
				OnOK(0, 0, NULL, bHandled);
			}
			break;

			case kExit:
			{
				CloseDialog(0);
			}
			break;

			default:;
		}
	}
	return 0;
}

LRESULT CMainDlg::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}



LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (CMegaAppImpl::GetMegaAppInstance()->IsLogined() == false) {
		PUTLOG(L"���O�C�����������Ă��܂���B");
		return 0;
	}
	if (m_syncManager.IsDownloading()) {
		m_syncManager.DownloadStop();
		GetDlgItem(IDOK).SetWindowTextW(L"����");
		return 0;
	}

	if (m_syncManager.IsSyncing()) {
		m_syncManager.SyncStop();
		GetDlgItem(IDOK).SetWindowTextW(L"����");
	} else {
		if (m_syncManager.SyncStart(CConfig::s_syncFolderPath)) {
			GetDlgItem(IDOK).SetWindowTextW(L"�������~");
		}
	}
	return 0;
}

LRESULT CMainDlg::OnOpenSyncFolder(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (CConfig::s_syncFolderPath.GetLength()) {
		::ShellExecute(NULL, NULL, CConfig::s_syncFolderPath, NULL, NULL, SW_SHOWNORMAL);
	}
	return 0;
}

LRESULT CMainDlg::OnOption(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	COptionSheet dlg;
	INT_PTR ret = dlg.Show(m_hWnd);
	if (CConfig::s_bDownload) {
		if (m_syncManager.DownloadStart(CConfig::s_targetMegaFolder, CConfig::s_DLTargetFolder)) {
			GetDlgItem(IDOK).SetWindowTextW(L"�_�E�����[�h���~");
		}
	}
	return 0;
}


void CMainDlg::OnListLogDblclk(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	int nSel = m_logListBox.GetCurSel();
	if (nSel == -1)
		return;
	CString text;
	m_logListBox.GetText(nSel, text);
	SetClipboardText(text);
}









