// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlcrack.h>

#include "resource.h"

#include "mega\megaclient.h"
#include "MegaAppImpl.h"

#include "TransactionView.h"
#include "SyncManager.h"


extern MegaClient* client;

#define	PUTLOG	CMainDlg::PutLog


//////////////////////////////////////////////////////////////////
// CMainDlg

class CMainDlg : 
	public CDialogImpl<CMainDlg>, 
	public CUpdateUI<CMainDlg>,
	public CMessageFilter, 
	public CIdleHandler,
	public CDialogResize<CMainDlg>
{
public:
	enum { IDD = IDD_MAINDLG };

	CMainDlg() : m_bFirstAutoSync(false), m_bVisibleOnDestroy(true) {}

	enum {
		kTrayIconId = 1,
		WM_TRAYICONNOTIFY = WM_APP + 100,
	};

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		return CWindow::IsDialogMessage(pMsg);
	}

	virtual BOOL OnIdle()
	{
		UIUpdateChildWindows();
		return FALSE;
	}

	static void	PutLog(LPCWSTR strFormat, ...);

	BEGIN_UPDATE_UI_MAP(CMainDlg)
	END_UPDATE_UI_MAP()

	BEGIN_DLGRESIZE_MAP(CMainDlg)
		DLGRESIZE_CONTROL(IDC_TRANSACTIONVIEWPOS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_LIST_LOG			, DLSZ_MOVE_Y | DLSZ_SIZE_X)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP_EX(CMainDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MSG_WM_TIMER( OnTimer )
		MSG_WM_SIZE( OnSize )
		MESSAGE_HANDLER_EX(WM_TRAYICONNOTIFY, OnTrayIconNotify)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_BUTTON_OPENSYNCFOLDER, OnOpenSyncFolder )
		COMMAND_ID_HANDLER(IDC_BUTTON_OPTION, OnOption )

		COMMAND_HANDLER_EX(IDC_LIST_LOG, LBN_DBLCLK, OnListLogDblclk)

		CHAIN_MSG_MAP(CDialogResize<CMainDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	void OnTimer(UINT_PTR nIDEvent);
	void OnSize(UINT nType, CSize size);
	LRESULT OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);

	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	LRESULT OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		CloseDialog(wID);
		return 0;
	}

	LRESULT OnOpenSyncFolder(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOption(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void OnListLogDblclk(UINT uNotifyCode, int nID, CWindow wndCtl);

	void CloseDialog(int nVal)
	{
		m_bVisibleOnDestroy = IsWindowVisible() != 0;
		DestroyWindow();
		::PostQuitMessage(nVal);
	}

private:

	// Data members
	static CMainDlg*	s_pMainDlg;
	CSyncManager		m_syncManager;
	CTransactionView	m_transactionView;
	
	CListBox		m_logListBox;
	bool	m_bFirstAutoSync;
	bool	m_bVisibleOnDestroy;
};
