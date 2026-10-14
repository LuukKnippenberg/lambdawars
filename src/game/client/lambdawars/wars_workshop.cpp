//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Mounts subscribed Steam Workshop items into the filesystem.
//
// A workshop item is expected to mirror the game directory layout, for
// example "maps/mymap.bsp", "maps/mymap.recast", "materials/...". The item
// folder is added to the GAME search path, so maps and their content load
// like any other, and to the WORKSHOP search path so they can be listed.
//
// The filesystem is shared between client and server, so mounting from the
// client also makes the content available to a listen server.
//
//=============================================================================//

#include "cbase.h"
#include "wars_workshop.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar wars_workshop_enabled( "wars_workshop_enabled", "1", FCVAR_ARCHIVE, "Mount subscribed Steam Workshop items on startup." );

#define WORKSHOP_UPDATE_INTERVAL 1.0
#define WORKSHOP_MAX_INIT_ATTEMPTS 10

static CWarsWorkshop s_WarsWorkshop;
CWarsWorkshop &WarsWorkshop() { return s_WarsWorkshop; }

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CWarsWorkshop::CWarsWorkshop() : CAutoGameSystemPerFrame( "CWarsWorkshop" ),
	m_bSteamReady( false ), m_iInitAttempts( 0 ), m_fNextUpdateTime( 0.0 )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::PostInit()
{
	if( !wars_workshop_enabled.GetBool() )
		return;

	if( TryInitSteam() )
		RefreshSubscribedItems();
}

//-----------------------------------------------------------------------------
// Purpose: Retries initialization if steam was not ready yet and mounts
//			items once their download completes.
//-----------------------------------------------------------------------------
void CWarsWorkshop::Update( float frametime )
{
	if( !wars_workshop_enabled.GetBool() )
		return;

	double fCurTime = Plat_FloatTime();
	if( fCurTime < m_fNextUpdateTime )
		return;
	m_fNextUpdateTime = fCurTime + WORKSHOP_UPDATE_INTERVAL;

	if( !m_bSteamReady )
	{
		if( m_iInitAttempts < WORKSHOP_MAX_INIT_ATTEMPTS && TryInitSteam() )
			RefreshSubscribedItems();
		return;
	}

	for( int i = m_PendingItems.Count() - 1; i >= 0; i-- )
	{
		WorkshopItemId_t item = m_PendingItems[i];
		unsigned int state = WarsWorkshopSteam_GetItemState( item );
		// Not subscribed yet means a subscribe request is still being processed
		if( !(state & WORKSHOP_ITEM_SUBSCRIBED) || (state & (WORKSHOP_ITEM_DOWNLOADING|WORKSHOP_ITEM_DOWNLOAD_PENDING|WORKSHOP_ITEM_NEEDS_UPDATE)) )
			continue;

		if( state & WORKSHOP_ITEM_INSTALLED )
			TryMountItem( item );
		else
			Warning( "Workshop: item %llu is not installed after download (state %u)\n", item, state );
		m_PendingItems.Remove( i );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::TryInitSteam()
{
	if( m_bSteamReady )
		return true;

	m_iInitAttempts++;
	m_bSteamReady = WarsWorkshopSteam_Init();
	if( m_bSteamReady )
		Msg( "Workshop: Steam UGC available\n" );
	else if( m_iInitAttempts >= WORKSHOP_MAX_INIT_ATTEMPTS )
		Warning( "Workshop: Steam UGC not available: %s\n", WarsWorkshopSteam_GetError() );
	return m_bSteamReady;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::RefreshSubscribedItems()
{
	if( !m_bSteamReady )
		return;

	CUtlVector< WorkshopItemId_t > items;
	items.SetCount( WarsWorkshopSteam_GetNumSubscribedItems() );
	if( items.Count() > 0 )
		items.SetCount( WarsWorkshopSteam_GetSubscribedItems( items.Base(), items.Count() ) );

	for( int i = 0; i < items.Count(); i++ )
	{
		WorkshopItemId_t item = items[i];
		unsigned int state = WarsWorkshopSteam_GetItemState( item );

		// An outdated item can still be used while the update downloads
		if( state & WORKSHOP_ITEM_INSTALLED )
			TryMountItem( item );

		if( !(state & WORKSHOP_ITEM_INSTALLED) || (state & WORKSHOP_ITEM_NEEDS_UPDATE) )
		{
			if( !(state & (WORKSHOP_ITEM_DOWNLOADING|WORKSHOP_ITEM_DOWNLOAD_PENDING)) )
				WarsWorkshopSteam_DownloadItem( item, false );
			if( !m_PendingItems.HasElement( item ) )
				m_PendingItems.AddToTail( item );
		}
	}

	Msg( "Workshop: %d subscribed item(s), %d mounted, %d downloading\n", items.Count(), m_MountedItems.Count(), m_PendingItems.Count() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::SubscribeItem( WorkshopItemId_t item )
{
	if( !m_bSteamReady || !WarsWorkshopSteam_SubscribeItem( item ) )
		return false;

	if( !m_PendingItems.HasElement( item ) )
		m_PendingItems.AddToTail( item );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::TryMountItem( WorkshopItemId_t item )
{
	if( m_MountedItems.HasElement( item ) )
		return true;

	char szFolder[MAX_PATH];
	if( !WarsWorkshopSteam_GetItemInstallInfo( item, szFolder, sizeof( szFolder ), NULL, NULL ) )
	{
		Warning( "Workshop: no install info for item %llu\n", item );
		return false;
	}

	filesystem->AddSearchPath( szFolder, "GAME", PATH_ADD_TO_TAIL );
	filesystem->AddSearchPath( szFolder, WARS_WORKSHOP_PATHID, PATH_ADD_TO_TAIL );
	m_MountedItems.AddToTail( item );

	Msg( "Workshop: mounted item %llu (%s)\n", item, szFolder );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::PrintItems()
{
	if( !m_bSteamReady )
	{
		Msg( "Workshop: Steam UGC not available: %s\n", WarsWorkshopSteam_GetError() );
		return;
	}

	CUtlVector< WorkshopItemId_t > items;
	items.SetCount( WarsWorkshopSteam_GetNumSubscribedItems() );
	if( items.Count() > 0 )
		items.SetCount( WarsWorkshopSteam_GetSubscribedItems( items.Base(), items.Count() ) );

	Msg( "Workshop: %d subscribed item(s)\n", items.Count() );
	for( int i = 0; i < items.Count(); i++ )
	{
		WorkshopItemId_t item = items[i];
		unsigned int state = WarsWorkshopSteam_GetItemState( item );

		char szFolder[MAX_PATH];
		if( !WarsWorkshopSteam_GetItemInstallInfo( item, szFolder, sizeof( szFolder ), NULL, NULL ) )
			V_strncpy( szFolder, "-", sizeof( szFolder ) );

		Msg( "  %llu: state %u%s%s%s%s, %s, %s\n", item, state,
			(state & WORKSHOP_ITEM_INSTALLED) ? " installed" : "",
			(state & WORKSHOP_ITEM_NEEDS_UPDATE) ? " needs-update" : "",
			(state & WORKSHOP_ITEM_DOWNLOADING) ? " downloading" : "",
			(state & WORKSHOP_ITEM_DOWNLOAD_PENDING) ? " download-pending" : "",
			m_MountedItems.HasElement( item ) ? "mounted" : "not mounted",
			szFolder );

		unsigned long long bytesDownloaded, bytesTotal;
		if( (state & WORKSHOP_ITEM_DOWNLOADING) && WarsWorkshopSteam_GetItemDownloadInfo( item, &bytesDownloaded, &bytesTotal ) )
			Msg( "    downloaded %llu / %llu bytes\n", bytesDownloaded, bytesTotal );
	}
}

CON_COMMAND( wars_workshop_list, "Lists subscribed Steam Workshop items and their state" )
{
	WarsWorkshop().PrintItems();
}

CON_COMMAND( wars_workshop_refresh, "Mounts newly installed subscribed Steam Workshop items and downloads missing ones" )
{
	WarsWorkshop().RefreshSubscribedItems();
}

CON_COMMAND( wars_workshop_subscribe, "Subscribes to a Steam Workshop item by id and mounts it once downloaded" )
{
	if( args.ArgC() < 2 )
	{
		Msg( "Usage: wars_workshop_subscribe <published file id>\n" );
		return;
	}

	WorkshopItemId_t item = V_atoui64( args[1] );
	if( !item || !WarsWorkshop().SubscribeItem( item ) )
	{
		Warning( "Workshop: failed to subscribe to item %s\n", args[1] );
		return;
	}
	Msg( "Workshop: subscribing to item %llu\n", item );
}
