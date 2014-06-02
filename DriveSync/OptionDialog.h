/**
*	@file	OptionDialog.h
*/

#pragma once

#include <unordered_set>
#include <string>
#include <atldlgs.h>
#include <atlcrack.h>
#include "resource.h"

class CConfig
{
public:
	static CString	s_mailaddress;
	static CString	s_password;

	static CString	s_syncFolderPath;
	static bool		s_bAutoSync;

	static bool		s_bDownload;
	static CString	s_targetMegaFolder;
	static CString	s_DLTargetFolder;

	static std::unordered_set<std::wstring>	s_setExcludeExt;

	static int		s_nMaxUploadFileCount;
	static int		s_nMaxDownloadFileCount;

	static void		LoadConfig();
	static void		SaveConfig();
	
	static void		ClearDLTarget();
};


///////////////////////////////////////////////////////////
///  設定プロパティシート

class COptionSheet : public CPropertySheetImpl<COptionSheet>
{
public:

	INT_PTR	Show(HWND hWndParent);

	BEGIN_MSG_MAP_EX(COptionSheet)
		CHAIN_MSG_MAP(CPropertySheetImpl<COptionSheet>)
	END_MSG_MAP()

};


























