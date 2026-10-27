//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Downloads and mounts Steam Workshop items on dedicated servers.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_SERVER_H
#define WARS_WORKSHOP_SERVER_H

#ifdef _WIN32
#pragma once
#endif

#include "wars_workshop_steam.h"

// Polls pending downloads. Called every server frame, also without a map loaded.
void WarsWorkshopServer_Update();

enum WorkshopServerItemStatus_t
{
	WORKSHOP_SERVER_ITEM_READY = 0,	// Mounted
	WORKSHOP_SERVER_ITEM_PENDING,	// Downloading
	WORKSHOP_SERVER_ITEM_FAILED,	// See WarsWorkshopServer_GetItemError
};

// Dedicated servers can download the Workshop map of a requested game
bool WarsWorkshopServer_IsSupported();
// Makes sure an item is mounted, downloading or updating it if needed
WorkshopServerItemStatus_t WarsWorkshopServer_PrepareItem( WorkshopItemId_t item );
WorkshopServerItemStatus_t WarsWorkshopServer_GetItemStatus( WorkshopItemId_t item );
const char *WarsWorkshopServer_GetItemError( WorkshopItemId_t item );

#endif // WARS_WORKSHOP_SERVER_H
