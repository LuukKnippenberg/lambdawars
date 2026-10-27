//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Validates the content of Steam Workshop items before mounting.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_CONTENT_H
#define WARS_WORKSHOP_CONTENT_H

#ifdef _WIN32
#pragma once
#endif

// Returns true if every file in the item folder, and every file embedded in its
// maps, is an allowed content type. Otherwise returns false and writes the reason.
bool WarsWorkshop_ValidateItemFolder( const char *pszFolder, char *pszReason, int reasonSize );

// Validates the files embedded in the pakfile lump of a map
bool WarsWorkshop_ValidateMapFile( const char *pszBspPath, char *pszReason, int reasonSize );

#endif // WARS_WORKSHOP_CONTENT_H
