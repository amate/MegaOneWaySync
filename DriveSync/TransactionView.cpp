/**
*	@file	TransactionView.cpp
*/

#include "stdafx.h"
#include "TransactionView.h"
#include <VersionHelpers.h>
#include "Utility.h"
#include "MainDlg.h"

////////////////////////////////////////////////////////////////////////
// CTransactionView

CTransactionView::CTransactionView() : m_lastQueSize(0)
{
}

CTransactionView::~CTransactionView()
{
}


void	CTransactionView::TransferUpdate(int td, int64_t bytes, int64_t size, int distanceTime, const std::string& filename,  bool bDownload)
{
	TransUpdate	tu{ td, bytes, size, distanceTime, filename, bDownload };
	SendMessage(WM_TRANSUPDATE, (WPARAM)&tu);
}

void	CTransactionView::_RefreshList()
{
	CRect rcClient;
	GetClientRect(rcClient);

	int cySize = 0;
	for (auto& data : m_vecTransData) {
		// アイテムの位置を設定
		data->rcItem.top = cySize;
		cySize += ItemHeight;
		data->rcItem.bottom = cySize;
		cySize += UnderLineHeight;

		data->rcItem.right = rcClient.right;
	}

	CSize	size;
	size.cx = rcClient.right;
	size.cy = m_vecTransData.size() ? cySize : 1;
	SetScrollSize(size, TRUE, false);
	SetScrollLine(10, 10);

	Invalidate(FALSE);
}

void	CTransactionView::TransferComplete(int td, bool bSuccess)
{
	SendMessage(WM_TRANSCOMPLETE, td, bSuccess);
}

LRESULT CTransactionView::OnTransUpdate(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	TransUpdate* ptu = (TransUpdate*)wParam;

	// 新規追加
	auto itfound = std::find_if(m_vecTransData.begin(), m_vecTransData.end(), 
		[ptu](const std::unique_ptr<TransData>& transData) {
		return transData->td == ptu->td;
	});
	if (itfound == m_vecTransData.end()) {
		m_vecTransData.emplace(m_vecTransData.begin(),
			new TransData(ptu->td, ptu->size, ConvertUTF16fromUTF8(ptu->filename), ptu->bDownload));
		m_vecTransData.front()->detailText = ptu->bDownload ? L"ダウンロード開始" : L"アップロード開始";
		_AddIcon(*m_vecTransData.front());
		_RefreshList();
	}

	// 更新
	int	i = 0;
	for (auto& data : m_vecTransData) {
		if (data->td != ptu->td) {
			++i;
			continue;
		} else {
			// (1.2 MB/sec)
			CString strTransferRate;
			int64_t nProgressMargin = ptu->bytes;
			data->lastProgress = ptu->bytes;

			//if (itemInfo.deqProgressAndTime.size() >= 10)
			//	itemInfo.deqProgressAndTime.pop_front();
			//itemInfo.deqProgressAndTime.push_back(make_pair(nProgressMargin, (int)dwTimeMargin));
			//nProgressMargin = 0;
			//int nTotalTime = 0;
			//for (auto itq = itemInfo.deqProgressAndTime.cbegin(); itq != itemInfo.deqProgressAndTime.cend(); ++itq) {
			//	nProgressMargin += itq->first;
			//	nTotalTime += itq->second;
			//}

			//ATLASSERT(ptu->distanceTime);
			// kbyte / second
			double dKbTransferRate = 
				(double)(nProgressMargin * 10) / (double)(1024 * ptu->distanceTime + 1);
			double MbTransferRate = dKbTransferRate / 1000.0;
			if (MbTransferRate > 1) {
				::swprintf(strTransferRate.GetBuffer(30), _T(" (%.1lf MB/sec)"), MbTransferRate);
				strTransferRate.ReleaseBuffer();
			} else {
				::swprintf(strTransferRate.GetBuffer(30), _T(" (%.1lf KB/sec)"), dKbTransferRate);
				strTransferRate.ReleaseBuffer();
			}

			// 残り 4 分
			int64_t nRestByte = data->fileSize - ptu->bytes;
			if (dKbTransferRate > 0) {
				int nTotalSecondTime = static_cast<int>((nRestByte / 1000) / dKbTransferRate);	// 残り時間(秒)
				int nhourTime = nTotalSecondTime / (60 * 60);									// 時間
				int nMinTime = (nTotalSecondTime - (nhourTime * (60 * 60))) / 60;			// 分
				int nSecondTime = nTotalSecondTime - (nhourTime * (60 * 60)) - (nMinTime * 60);	// 秒
				data->detailText = _T("残り ");
				if (nhourTime > 0) {
					data->detailText.Append(nhourTime) += _T(" 時間 ");
				}
				if (nMinTime > 0) {
					data->detailText.Append(nMinTime) += _T(" 分 ");
				}
				data->detailText.Append(nSecondTime) += _T(" 秒 ― ");


				// 10.5 / 288 MB
				CString strDownloaded;
				double dMByte = (double)data->fileSize / (1000.0 * 1000.0);
				if (dMByte >= 1) {
					double DownloadMByte = (double)ptu->bytes / (1000.0 * 1000.0);
					::swprintf(strDownloaded.GetBuffer(30), _T("%.1lf / %.1lf MB"), DownloadMByte, dMByte);
					strDownloaded.ReleaseBuffer();
				} else {
					double dKByte = (double)data->fileSize / 1000.0;
					double DownloadKByte = (double)ptu->bytes / 1000.0;
					::swprintf(strDownloaded.GetBuffer(30), _T("%.1lf / %.1lf KB"), DownloadKByte, dKByte);
					strDownloaded.ReleaseBuffer();
				}
				data->detailText += strDownloaded + strTransferRate;
			}
			InvalidateRect(_GetItemClientRect(i), FALSE);
			return 0;
		}
	}

	return 0;
}

LRESULT CTransactionView::OnTransComplete(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int td = (int)wParam;
	bool bSuccess = lParam != 0;
	int i = 0;
	for (auto it = m_vecTransData.begin(); it != m_vecTransData.end(); ++it, ++i) {
		if (it->get()->td == td) {
			if (it->get()->bDownload) {
				it->get()->detailText = bSuccess ? L"ダウンロード完了" : L"ダウンロード失敗";
			} else {
				it->get()->detailText = bSuccess ? L"アップロード完了" : L"アップロード失敗";
			}
			it->get()->td = -1;
			it->get()->lastProgress = it->get()->fileSize;
			InvalidateRect(_GetItemClientRect(i), FALSE);
			SetTimer((UINT_PTR)it->get(), kRemoveTransactionTimerInterval);
			break;
		}
	}
	return 0;
}


void	CTransactionView::DoPaint(CDCHandle dc)
{
	CPoint ptOffset;
	GetScrollOffset(ptOffset);

	CRect rcClient;
	GetClientRect(rcClient);
	rcClient.MoveToY(ptOffset.y);
	//rcClient.bottom	+= ptOffset.y;

	CMemoryDC	memDC(dc, rcClient);
	HFONT hOldFont = memDC.SelectFont(m_Font);

	// 背景を描画
	memDC.FillSolidRect(rcClient, RGB(255, 255, 255));

	for (auto& data : m_vecTransData) {
		CRect rcItem = data->rcItem;
		rcItem.right = rcClient.right;
		if (memDC.RectVisible(&rcItem) == FALSE)
			continue;

		memDC.SetBkMode(TRANSPARENT);
		/*if (it->dwState & DLITEMSTATE_SELECTED) {
			static COLORREF SelectColor = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
			memDC.SetTextColor(SelectColor);
		} else*/ {
			memDC.SetTextColor(0);
		}

		// 選択列描画
		//if (it->dwState & DLITEMSTATE_SELECTED){
		//	memDC.FillRect(&rcItem, COLOR_HIGHLIGHT);
		//}

		// アイコン描画
		CRect rcImage = rcItem;
		CPoint ptImg(cxImageMargin, rcImage.top + cyImageMargin);
		m_ImageList.Draw(memDC, data->nImgIndex, ptImg, ILD_NORMAL);

		// ファイル名を描画
		CRect rcFileName = rcItem;
		rcFileName.left = cxFileNameMargin;
		rcFileName.top += cyFileNameMargin;
		memDC.DrawText(data->fileName, data->fileName.GetLength(), rcFileName, DT_SINGLELINE);

		// progress
		if (data->fileSize) {
			CRect rcProgress(cxFileNameMargin, rcItem.top + cyProgressMargin, rcItem.right - cleftProgressMargin, rcItem.top + cyProgressMargin + ProgressHeight);
			if (IsThemeNull() == false) {
				static int PROGRESSBAR = IsWindowsVistaOrGreater() ? PP_TRANSPARENTBAR : PP_BAR;
				static int PROGRESSBODY = IsWindowsVistaOrGreater() ? PP_FILL : PP_CHUNK;
				if (IsThemeBackgroundPartiallyTransparent(PROGRESSBAR, 0))
					DrawThemeParentBackground(memDC, rcProgress);
				DrawThemeBackground(memDC, PROGRESSBAR, 0, rcProgress);
				CRect rcContent;
				GetThemeBackgroundContentRect(memDC, PROGRESSBAR, 0, rcProgress, rcContent);

				double Propotion = double(data->lastProgress) / double(data->fileSize);
				int nPos = (int)((double)rcContent.Width() * Propotion);
				rcContent.right = rcContent.left + nPos;
				DrawThemeBackground(memDC, PROGRESSBODY, 0, rcContent);
			} else {
				memDC.DrawEdge(rcProgress, EDGE_RAISED, BF_ADJUST | BF_MONO | BF_RECT | BF_MIDDLE);
				double Propotion = double(data->lastProgress) / double(data->fileSize);
				int nPos = (int)((double)rcProgress.Width() * Propotion);
				rcProgress.right = rcProgress.left + nPos;
				memDC.FillSolidRect(rcProgress, RGB(0, 255, 0));
			}
		}

		// 説明描画
		CRect rcDiscribe = rcItem;
		rcDiscribe.top += cyProgressMargin + ProgressHeight;
		rcDiscribe.left = cxFileNameMargin;
		memDC.DrawText(data->detailText, data->detailText.GetLength(), rcDiscribe, DT_SINGLELINE);

		// 停止アイコン描画
		//CPoint ptStop(rcClient.right - cxStopLeftSpace, rcItem.top + cyStopTopMargin);
		//m_ImgStop.Draw(memDC, 0, ptStop, ILD_NORMAL);

		// 下にラインを引く
		static COLORREF BorderColor = ::GetSysColor(COLOR_3DLIGHT);
		HPEN hPen = ::CreatePen(PS_SOLID, 1, BorderColor);
		HPEN hOldPen = memDC.SelectPen(hPen);
		memDC.MoveTo(CPoint(rcItem.left, rcItem.bottom));
		memDC.LineTo(rcItem.right, rcItem.bottom);
		memDC.SelectPen(hOldPen);
		::DeleteObject(hPen);
	}

	dc.SelectFont(hOldFont);
}




int CTransactionView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	// 水平スクロールバーを削除
	ModifyStyle(WS_HSCROLL, 0);

	// 画面更新用のタイマーを起動
	//SetTimer(1, 1000);

	//static bool bInit = false;
	//if (bInit)
	//	return 0;
	//bInit = true;

	SetScrollSize(10, 10);
	SetScrollLine(10, 10);
	//SetScrollPage(100, 100);

	//// イメージリスト作成
	m_ImageList.Create(32, 32, ILC_COLOR32 | ILC_MASK, 20, 100);

	// デフォルトのアイコンを読み込み
	SHFILEINFO sfinfo;
	::SHGetFileInfo(_T("*.*"), FILE_ATTRIBUTE_NORMAL, &sfinfo, sizeof(SHFILEINFO)
		, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES);
	int nImgIndex = m_ImageList.AddIcon(sfinfo.hIcon);	/* 0 */
	::DestroyIcon(sfinfo.hIcon);

	//// 停止アイコンを読み込む
	//m_ImgStop.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
	//HICON hStopIcon = AtlLoadIcon(IDI_DLSTOP);
	//m_ImgStop.AddIcon(hStopIcon);
	//::DestroyIcon(hStopIcon);

	// フォントを設定
	WTL::CLogFont	logfont;
	logfont.SetMenuFont();
	m_Font = logfont.CreateFontIndirectW();

	//// ツールチップを設定
	//m_ToolTip.Create(m_hWnd);
	//m_ToolTip.ModifyStyle(0, TTS_ALWAYSTIP);
	//CToolInfo ti(TTF_SUBCLASS, m_hWnd);
	//ti.hwnd = m_hWnd;
	//m_ToolTip.AddTool(ti);
	//m_ToolTip.Activate(TRUE);
	//m_ToolTip.SetDelayTime(TTDT_AUTOPOP, 30 * 1000);
	//m_ToolTip.SetMaxTipWidth(600);

	OpenThemeData(L"PROGRESS");

	//RegisterDragDrop();

	return 0;
}

void CTransactionView::OnDestroy()
{
	SetMsgHandled(FALSE);
}


void CTransactionView::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	if (size != CSize(0, 0)) {
		for (auto& data : m_vecTransData)
			data->rcItem.right = size.cx;
		CSize	scrollsize;
		GetScrollSize(scrollsize);
		scrollsize.cx = size.cx ? size.cx : 1;
		m_sizeClient.cx = scrollsize.cx;
		SetScrollSize(scrollsize, FALSE, false);
		SetScrollLine(10, 10);
	}
}

void	CTransactionView::OnTimer(UINT_PTR nIDEvent)
{
	for (auto it = m_vecTransData.begin(); it != m_vecTransData.end(); ++it) {
		if (reinterpret_cast<UINT_PTR>(it->get()) == nIDEvent) {
			m_vecTransData.erase(it);
			_RefreshList();
			KillTimer(nIDEvent);
			break;
		}
	}
}

/// nIndexのアイテムのクライアント座標での範囲を返す
CRect	CTransactionView::_GetItemClientRect(int nIndex)
{
	ATLASSERT(0 <= nIndex && nIndex < (int)m_vecTransData.size());

	CPoint	ptOffset;
	GetScrollOffset(ptOffset);

	CRect rcItem = m_vecTransData[nIndex]->rcItem;
	rcItem.top -= ptOffset.y;
	rcItem.bottom -= ptOffset.y;
	return rcItem;
}


/// アイテムの左端に置くアイコンを読み込む
void CTransactionView::_AddIcon(TransData& data)
{
	CString ext = ::PathFindExtensionW(data.fileName);
	if (ext.IsEmpty()) {
		data.nImgIndex = 0;
	} else {
		ext.Delete(0, 1);
		ext.MakeLower();
		auto it = m_mapImgIndex.find(ext);
		if (it != m_mapImgIndex.end()) {	// 見つかった
			data.nImgIndex = it->second;
		} else {							// 見つからなかった
			SHFILEINFO sfinfo;
			CString strFind;
			strFind.Format(_T("*.%s"), ext);
			// アイコンを取得
			::SHGetFileInfo(strFind, FILE_ATTRIBUTE_NORMAL, &sfinfo, sizeof(SHFILEINFO)
				, SHGFI_ICON | SHGFI_USEFILEATTRIBUTES);
			ATLASSERT(sfinfo.hIcon);
			// イメージリストにアイコンを追加
			int nImgIndex = m_ImageList.AddIcon(sfinfo.hIcon);
			// 拡張子とイメージリストのインデックスを関連付ける
			m_mapImgIndex.insert(std::pair<CString, int>(ext, nImgIndex));
			::DestroyIcon(sfinfo.hIcon);

			data.nImgIndex = nImgIndex;
		}
	}
}


















