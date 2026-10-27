//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Native open file dialog for the CEF menu, which can't provide the
//			paths of chosen files.
//
//=============================================================================//

#ifndef WARS_FILEDIALOG_H
#define WARS_FILEDIALOG_H

#ifdef _WIN32
#pragma once
#endif

// Opens the dialog on its own thread, so the game keeps running. Paths are UTF-8.
// pszPatterns is a list of patterns like "*.jpg;*.png". Returns false if a dialog is already open.
bool WarsFileDialog_Open( const char *pszTitle, const char *pszFilterName, const char *pszPatterns, const char *pszInitialFolder );
bool WarsFileDialog_IsOpen();
// Path chosen in the last dialog, empty if it was cancelled
void WarsFileDialog_GetResult( char *pszPath, int maxLen );

#endif // WARS_FILEDIALOG_H
