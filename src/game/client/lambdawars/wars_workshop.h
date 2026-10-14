//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Mounts subscribed Steam Workshop items into the filesystem.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_H
#define WARS_WORKSHOP_H

#ifdef _WIN32
#pragma once
#endif

#include "igamesystem.h"
#include "utlvector.h"
#include "wars_workshop_steam.h"

// Search path id under which all mounted workshop items are also added,
// so they can be listed separately (e.g. filesystem.ListDir('maps/', pathid='WORKSHOP')).
#define WARS_WORKSHOP_PATHID "WORKSHOP"

class CWarsWorkshop : public CAutoGameSystemPerFrame
{
public:
	CWarsWorkshop();

	virtual void PostInit();
	virtual void Update( float frametime );

	// Mounts installed subscribed items and requests downloads for the rest
	void RefreshSubscribedItems();
	bool SubscribeItem( WorkshopItemId_t item );
	void PrintItems();

private:
	bool TryInitSteam();
	bool TryMountItem( WorkshopItemId_t item );

	bool m_bSteamReady;
	int m_iInitAttempts;
	double m_fNextUpdateTime;

	CUtlVector< WorkshopItemId_t > m_MountedItems;
	// Subscribed items waiting for a download to complete
	CUtlVector< WorkshopItemId_t > m_PendingItems;
};

CWarsWorkshop &WarsWorkshop();

#endif // WARS_WORKSHOP_H
