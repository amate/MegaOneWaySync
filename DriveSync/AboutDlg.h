// aboutdlg.h : interface of the CAboutDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "MegaAppImpl.h"
#include "resource.h"

class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
	enum { IDD = IDD_ABOUTBOX };

	CAboutDlg() : m_pDUNotify(nullptr) {}

	BEGIN_MSG_MAP(CAboutDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CenterWindow(GetParent());

		if (CMegaAppImpl::GetMegaAppInstance()->IsLogined()) {
			m_pDUNotify = &CMegaAppImpl::GetMegaAppInstance()->GetMegaStrageInfo();
			m_pDUNotify->then([this](const TreeProcDU& du) {
				m_pDUNotify = nullptr;
				CString text;
				text.Format(L"ストレージの使用量：%d MB\nストレージ内のファイル数：%d\nストレージ内のフォルダ数：%d", int(du.numbytes / 1048576), du.numfiles, du.numfolders);
				GetDlgItem(IDC_STATIC_STRAGEINFO).SetWindowTextW(text);
			});
		}
		return TRUE;
	}

	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		if (m_pDUNotify) {
			m_pDUNotify->then(std::function<void(const TreeProcDU&)>());
			m_pDUNotify = nullptr;
		}
		EndDialog(wID);
		return 0;
	}
private:
	CAsyncDUNotify* m_pDUNotify;
};
