//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Mounts Steam Workshop items into the filesystem.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_H
#define WARS_WORKSHOP_H

#ifdef _WIN32
#pragma once
#endif

#include "igamesystem.h"
#include "utlvector.h"
#include "utlstring.h"
#include "wars_workshop_steam.h"

// Search path id under which all mounted workshop items are also added,
// so they can be listed separately (e.g. filesystem.ListDir('maps/', pathid='WORKSHOP')).
#define WARS_WORKSHOP_PATHID "WORKSHOP"

enum WorkshopItemStatus_t
{
	WORKSHOP_STATUS_NONE = 0,
	WORKSHOP_STATUS_DOWNLOADING,
	WORKSHOP_STATUS_MOUNTED,
	WORKSHOP_STATUS_FAILED,		// Download failed or timed out
	WORKSHOP_STATUS_REJECTED,	// Content is not allowed, see GetItemError
};

class CWarsWorkshop : public CAutoGameSystemPerFrame
{
public:
	CWarsWorkshop();

	virtual void PostInit();
	virtual void Shutdown();
	virtual void Update( float frametime );

	// Mounts installed subscribed items and requests downloads for the rest
	void RefreshSubscribedItems();
	bool SubscribeItem( WorkshopItemId_t item );
	// Downloads (or updates) an item without subscribing and mounts it, for
	// example the map of a game lobby
	bool RequestItem( WorkshopItemId_t item );

	WorkshopItemStatus_t GetItemStatus( WorkshopItemId_t item );
	const char *GetItemError( WorkshopItemId_t item );
	// Returns the mounted item containing maps/<mapname>.bsp, or 0
	WorkshopItemId_t FindMapItem( const char *pszMapName );

	void PrintItems();

private:
	struct MountedItem_t
	{
		WorkshopItemId_t item;
		CUtlString folder;
	};

	struct PendingItem_t
	{
		WorkshopItemId_t item;
		double fRequestTime;
		bool bSubscribing;
	};

	struct FailedItem_t
	{
		WorkshopItemId_t item;
		WorkshopItemStatus_t status;
		CUtlString error;
	};

	bool TryInitSteam();
	void QueueItem( WorkshopItemId_t item, bool bSubscribing );
	void UpdatePendingItems();
	bool TryMountItem( WorkshopItemId_t item );
	void UnmountItem( int mountedIdx );
	void SetItemFailed( WorkshopItemId_t item, WorkshopItemStatus_t status, const char *pszError );
	void ClearItemFailed( WorkshopItemId_t item );
	int FindMountedItem( WorkshopItemId_t item );
	int FindPendingItem( WorkshopItemId_t item );
	int FindFailedItem( WorkshopItemId_t item );

	bool m_bSteamReady;
	int m_iInitAttempts;
	double m_fNextUpdateTime;

	CUtlVector< MountedItem_t > m_MountedItems;
	CUtlVector< PendingItem_t > m_PendingItems;
	CUtlVector< FailedItem_t > m_FailedItems;
};

CWarsWorkshop &WarsWorkshop();

#endif // WARS_WORKSHOP_H
