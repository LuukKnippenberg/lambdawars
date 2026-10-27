//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Native open file dialog for the CEF menu, which can't provide the
//			paths of chosen files.
//
// The dialog runs on its own thread: the modal loop of a dialog on the main
// thread would stop the game and could run CEF messages from within a
// JavaScript call. Uses windows.h, so no precompiled header is used for this file.
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <string.h>

#include "wars_filedialog.h"

#pragma comment( lib, "comdlg32.lib" )
#pragma comment( lib, "ole32.lib" )

static volatile LONG s_bOpen = 0;
static HWND s_hOwner = NULL;
static wchar_t s_szTitle[256];
// Filter name and patterns, each terminated by a null character, ending with an extra one
static wchar_t s_szFilter[512];
static wchar_t s_szInitialFolder[MAX_PATH];
static wchar_t s_szPath[MAX_PATH];
static char s_szResult[MAX_PATH * 3];

static void ToWide( const char *pszText, wchar_t *pszOut, int maxLen )
{
	pszOut[0] = L'\0';
	if( pszText && !MultiByteToWideChar( CP_UTF8, 0, pszText, -1, pszOut, maxLen ) )
		pszOut[0] = L'\0';
}

static DWORD WINAPI FileDialogThread( LPVOID )
{
	// The explorer style dialog requires COM
	HRESULT hr = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

	s_szPath[0] = L'\0';
	OPENFILENAMEW ofn;
	memset( &ofn, 0, sizeof( ofn ) );
	ofn.lStructSize = sizeof( ofn );
	ofn.hwndOwner = s_hOwner;
	ofn.lpstrFilter = s_szFilter;
	ofn.lpstrFile = s_szPath;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = s_szTitle[0] ? s_szTitle : NULL;
	ofn.lpstrInitialDir = s_szInitialFolder[0] ? s_szInitialFolder : NULL;
	// Changing the working directory would break the relative paths of the game
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

	s_szResult[0] = '\0';
	if( GetOpenFileNameW( &ofn ) )
	{
		if( !WideCharToMultiByte( CP_UTF8, 0, s_szPath, -1, s_szResult, sizeof( s_szResult ), NULL, NULL ) )
			s_szResult[0] = '\0';
	}

	if( SUCCEEDED( hr ) )
		CoUninitialize();

	InterlockedExchange( &s_bOpen, 0 );
	return 0;
}

bool WarsFileDialog_Open( const char *pszTitle, const char *pszFilterName, const char *pszPatterns, const char *pszInitialFolder )
{
	if( InterlockedCompareExchange( &s_bOpen, 1, 0 ) != 0 )
		return false;

	ToWide( pszTitle, s_szTitle, ARRAYSIZE( s_szTitle ) );
	ToWide( pszInitialFolder, s_szInitialFolder, ARRAYSIZE( s_szInitialFolder ) );

	wchar_t szName[200], szPatterns[200];
	ToWide( pszFilterName, szName, ARRAYSIZE( szName ) );
	ToWide( pszPatterns, szPatterns, ARRAYSIZE( szPatterns ) );
	if( !szPatterns[0] )
		wcscpy_s( szPatterns, L"*.*" );
	memset( s_szFilter, 0, sizeof( s_szFilter ) );
	int nameLen = (int)wcslen( szName );
	wcscpy_s( s_szFilter, szName );
	wcscpy_s( s_szFilter + nameLen + 1, ARRAYSIZE( s_szFilter ) - nameLen - 1, szPatterns );

	// Keeps the dialog in front of the game window
	s_hOwner = GetActiveWindow();

	HANDLE hThread = CreateThread( NULL, 0, FileDialogThread, NULL, 0, NULL );
	if( !hThread )
	{
		InterlockedExchange( &s_bOpen, 0 );
		return false;
	}
	CloseHandle( hThread );
	return true;
}

bool WarsFileDialog_IsOpen()
{
	return InterlockedCompareExchange( &s_bOpen, 0, 0 ) != 0;
}

void WarsFileDialog_GetResult( char *pszPath, int maxLen )
{
	if( maxLen <= 0 )
		return;
	pszPath[0] = '\0';
	if( WarsFileDialog_IsOpen() )
		return;
	strncpy_s( pszPath, maxLen, s_szResult, _TRUNCATE );
}
