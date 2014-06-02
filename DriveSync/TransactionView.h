/**
*	@file	TransactionView.h
*/

#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <atlscrl.h>
#include <atlcrack.h>
#include <atltheme.h>
#include <atlsync.h>
#include <atlctrls.h>

class CTransactionView : 
	public CScrollWindowImpl<CTransactionView>, 
	public CThemeImpl<CTransactionView>
{
public:

	// Constants
	enum {
		ItemHeight = 50,
		UnderLineHeight = 1,

		cxImageMargin = 3,
		cyImageMargin = (ItemHeight - 32) / 2,

		cxFileNameMargin = cxImageMargin + 32 + cxImageMargin,
		cyFileNameMargin = 3,

		ProgressHeight = 12,
		cyProgressMargin = 21,
		cleftProgressMargin = 5, //30,

		//cxStopLeftSpace = 23,
		//cyStopTopMargin = 19,
		//cxStop = 16,
		//cyStop = 16,

		TOOLTIPTIMERID = 2,
		TOOLTIPDELAY = 250,

		WM_TRANSUPDATE = WM_APP + 100,
		WM_TRANSCOMPLETE = WM_APP + 101,

		kRemoveTransactionTimerInterval = 3000,
	};

	CTransactionView();
	~CTransactionView();

	void	TransferUpdate(int td, int64_t bytes, int64_t size, int distanceTime, const std::string& filename, bool bDownload);
	void	TransferComplete(int td, bool bSuccess);

	// Overrides
	void	DoPaint(CDCHandle dc);

	// Message map
	BEGIN_MSG_MAP_EX(CTransactionView)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		//MSG_WM_LBUTTONDOWN(OnLButtonDown)
		//MSG_WM_RBUTTONUP(OnRButtonUp)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_TIMER(OnTimer)
		//MSG_WM_COPYDATA(OnCopyData)
		MESSAGE_HANDLER_EX(WM_TRANSUPDATE, OnTransUpdate)
		MESSAGE_HANDLER_EX(WM_TRANSCOMPLETE, OnTransComplete)
		//MESSAGE_HANDLER_EX(WM_USER_ADDTODOWNLOADLIST, OnAddToList)
		//MESSAGE_HANDLER_EX(WM_USER_REMOVEFROMDOWNLIST, OnRemoveFromList)
		//MESSAGE_HANDLER_EX(WM_USER_USESAVEFILEDIALOG, OnUseSaveFileDialog)
		//NOTIFY_CODE_HANDLER_EX(TTN_GETDISPINFO, OnTooltipGetDispInfo)
		//MSG_WM_MOUSEMOVE(OnMouseMove)
		//COMMAND_ID_HANDLER_EX(ID_RENAME_DLITEM, OnRenameDLItem)
		//COMMAND_ID_HANDLER_EX(ID_OPEN_SAVEFOLDER, OnOpenSaveFolder)
		//COMMAND_ID_HANDLER_EX(ID_OPEN_REFERER, OnOpenReferer)
		CHAIN_MSG_MAP(CScrollWindowImpl<CTransactionView>)
		CHAIN_MSG_MAP_ALT(CScrollWindowImpl<CTransactionView>, 1)
		CHAIN_MSG_MAP(CThemeImpl<CTransactionView>)
	END_MSG_MAP()

	int		OnCreate(LPCREATESTRUCT lpCreateStruct);
	void	OnDestroy();
	void	OnSize(UINT nType, CSize size);
	//void	OnLButtonDown(UINT nFlags, CPoint point);
	//void	OnRButtonUp(UINT nFlags, CPoint point);
	BOOL	OnEraseBkgnd(CDCHandle dc) { return TRUE; }
	void	OnTimer(UINT_PTR nIDEvent);
	//BOOL	OnCopyData(CWindow wnd, PCOPYDATASTRUCT pCopyDataStruct);
	LRESULT OnTransUpdate(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnTransComplete(UINT uMsg, WPARAM wParam, LPARAM lParam);
	//LRESULT OnAddToList(UINT uMsg, WPARAM wParam, LPARAM lParam);
	//LRESULT OnRemoveFromList(UINT uMsg, WPARAM wParam, LPARAM lParam);
	//LRESULT OnUseSaveFileDialog(UINT uMsg, WPARAM wParam, LPARAM lParam);
	//LRESULT OnTooltipGetDispInfo(LPNMHDR pnmh);
	//void	OnMouseMove(UINT nFlags, CPoint point);
	//void	OnRenameDLItem(UINT uNotifyCode, int nID, CWindow wndCtl);
	//void	OnOpenSaveFolder(UINT uNotifyCode, int nID, CWindow wndCtl);
	//void	OnOpenReferer(UINT uNotifyCode, int nID, CWindow wndCtl);

private:
	void	_RefreshList();
	CRect	_GetItemClientRect(int nIndex);

	struct TransUpdate {
		int	td;
		int64_t	bytes;
		int64_t	size;
		int	distanceTime;
		std::string filename;
		bool bDownload;
	};

	struct TransData {
		int		td;
		int64_t	lastProgress;
		int64_t	fileSize;
		CString	fileName;
		CString	detailText;
		CRect	rcItem;
		int		nImgIndex;
		bool	bDownload;

		TransData(int td, int64_t fileSize, const std::wstring& fileName, bool bDownload) :
			td(td), lastProgress(0), fileSize(fileSize), fileName(fileName.c_str()), nImgIndex(0), bDownload(bDownload) {}
	};

	void	_AddIcon(TransData& data);

	// Data members
	std::vector<std::unique_ptr<TransData>>	m_vecTransData;
	int		m_lastQueSize;

	CFont				m_Font;
	CImageListManaged		m_ImageList;
	std::map<CString, int>	m_mapImgIndex;		// key:ägí£éqÅAvalue:ImagelistÇÃindex
};
















