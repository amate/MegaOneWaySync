/**
*	@file	Utility.cpp
*/

#include "stdafx.h"
#include "Utility.h"
#include <boost\algorithm\string\case_conv.hpp>


std::string ConvertUTF8fromUTF16(const std::wstring& utf16)
{
	std::string utf8;
	int size = ::WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), utf16.length(), (LPSTR)utf8.data(), 0, nullptr, nullptr);
	if (size > 0) {
		utf8.resize(size);
		if (::WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), utf16.length(), (LPSTR)utf8.data(), size, nullptr, nullptr))
			return utf8;
	}
	return utf8;
}

std::wstring ConvertUTF16fromUTF8(const std::string& utf8)
{
	std::wstring utf16;
	int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.length(), (LPWSTR)utf16.data(), 0);
	if (size > 0) {
		utf16.resize(size);
		if (::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.length(), (LPWSTR)utf16.data(), size))
			return utf16;
	}
	return utf16;
}


std::vector<std::string>	SeparatePathUTF8(const std::wstring& path)
{
	std::vector<std::string>	partPath;
	int start = 0;
	int slash = path.find(L'\\', start);
	while (slash != path.npos) {
		std::wstring part = path.substr(start, slash - start);
		if (part.empty()) {
			ATLASSERT(FALSE);
			return partPath;
		}
		partPath.emplace_back(ConvertUTF8fromUTF16(part));
		start = slash + 1;
		slash = path.find(L'\\', start);
	}
	std::wstring part = path.substr(start);
	if (part.length())
		partPath.emplace_back(ConvertUTF8fromUTF16(part));

	return partPath;
}

std::vector<std::wstring>	SeparatePathUTF16(const std::wstring& path)
{
	std::vector<std::wstring>	partPath;
	int start = 0;
	int slash = path.find(L'\\', start);
	while (slash != path.npos) {
		std::wstring part = path.substr(start, slash - start);
		if (part.empty()) {
			ATLASSERT(FALSE);
			return partPath;
		}
		partPath.emplace_back(part);
		start = slash + 1;
		slash = path.find(L'\\', start);
	}
	std::wstring part = path.substr(start);
	if (part.length())
		partPath.emplace_back(part);

	return partPath;
}

CString	GetFileOrFolderName(CString path)
{
	::PathRemoveBackslashW(path.GetBuffer(MAX_PATH));
	path.ReleaseBuffer();
	LPCWSTR name = ::PathFindFileNameW(path);
	return name;
}

CString	GetParentFolderPath(CString path)
{
	::PathRemoveBackslashW(path.GetBuffer(MAX_PATH));
	::PathRemoveFileSpecW(path.GetBuffer(MAX_PATH));
	::PathRemoveBackslashW(path.GetBuffer(MAX_PATH));
	path.ReleaseBuffer();
	return path;
}

CString AddBackslash(CString path)
{
	::PathAddBackslashW(path.GetBuffer(MAX_PATH));
	path.ReleaseBuffer();
	return path;
}

std::wstring PathFindExtention(const std::wstring& fileName)
{
	std::wstring ext = ::PathFindExtensionW(fileName.c_str());
	if (ext.empty())
		return ext;
	if (ext.front() == L'.')
		ext.erase(ext.begin());
	boost::algorithm::to_lower(ext);
	return ext;
}


FILETIME	GetLastWriteTime(const std::wstring& filePath)
{
	FILETIME ft = {};
	HANDLE hFile = ::CreateFile(filePath.c_str(), GENERIC_READ, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return ft;
	
	::GetFileTime(hFile, nullptr, nullptr, &ft);

	::CloseHandle(hFile);
	return ft;
}

FILETIME	GetCreateTime(const std::wstring& filePath)
{
	FILETIME ft = {};
	HANDLE hFile = ::CreateFile(filePath.c_str(), GENERIC_READ, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return ft;

	::GetFileTime(hFile, &ft, nullptr, nullptr);

	::CloseHandle(hFile);
	return ft;
}

void UnixTimeToFileTime(time_t t, LPFILETIME pft)
{
	// Note that LONGLONG is a 64-bit value
	LONGLONG ll;

	ll = Int32x32To64(t, 10000000) + 116444736000000000;
	pft->dwLowDateTime = (DWORD)ll;
	pft->dwHighDateTime = ll >> 32;
}

void		SetLastWriteTime(const std::wstring& filePath, time_t posixTime)
{
	HANDLE hFile = ::CreateFile(filePath.c_str(), GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return ;

	FILETIME ft = {};
	UnixTimeToFileTime(posixTime, &ft);

	::SetFileTime(hFile, nullptr, nullptr, &ft);

	::CloseHandle(hFile);
}

time_t FileTime_to_POSIX(const FILETIME* ft)
{
	// takes the last modified date
	LARGE_INTEGER date, adjust;

	date.HighPart = ft->dwHighDateTime;
	date.LowPart = ft->dwLowDateTime;

	// 100-nanoseconds = milliseconds*10000
	adjust.QuadPart = 11644473600000 * 10000;

	// removes the diff between 1970 and 1601
	date.QuadPart -= adjust.QuadPart;

	// converts back from 100-nanoseconds to seconds
	return date.QuadPart / 10000000;
}

time_t	GetLastWritePosixTime(const std::wstring& filePath)
{
	FILETIME ft = GetLastWriteTime(filePath);
	return FileTime_to_POSIX(&ft);
}

time_t	GetCreatePosixTime(const std::wstring& filePath)
{
	FILETIME ft = GetCreateTime(filePath);
	return FileTime_to_POSIX(&ft);
}


bool SetClipboardText(const CString &str)
{
	if (str.IsEmpty())
		return false;

	int 	nByte = (str.GetLength() + 1) * sizeof(TCHAR);
	HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, nByte);
	if (hText == NULL)
		return false;

	BYTE*	pText = (BYTE *) ::GlobalLock(hText);
	if (pText == NULL)
		return false;

	::memcpy(pText, (LPCTSTR)str, nByte);

	::GlobalUnlock(hText);

	::OpenClipboard(NULL);
	::EmptyClipboard();
	::SetClipboardData(CF_UNICODETEXT, hText);
	::CloseClipboard();
	return true;
}

CString GetExeDirectory()
{
	CString exePath;
	::GetModuleFileNameW(NULL, exePath.GetBuffer(MAX_PATH), MAX_PATH);
	::PathRemoveFileSpecW(exePath.GetBuffer(MAX_PATH));
	::PathAddBackslashW(exePath.GetBuffer(MAX_PATH));
	exePath.ReleaseBuffer();
	return exePath;
}



















