//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Mounts Steam Workshop items into the filesystem.
//
// A workshop item is expected to mirror the game directory layout, for
// example "maps/mymap.bsp", "maps/mymap.recast", "materials/...". The item
// folder is added to the GAME search path, so maps and their content load
// like any other, and to the WORKSHOP search path so they can be listed.
// Items are only mounted if their content is allowed, see
// wars_workshop_content.cpp.
//
// Mounted items are subscribed items, and items requested without
// subscribing, like the map of a game lobby the player joined.
//
// The filesystem is shared between client and server, so mounting from the
// client also makes the content available to a listen server.
//
//=============================================================================//

#include "cbase.h"
#include "wars_workshop.h"
#include "wars_workshop_content.h"
#include "filesystem.h"
#include "steam/steam_api.h"
#include <io.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar wars_workshop_enabled( "wars_workshop_enabled", "1", FCVAR_ARCHIVE, "Mount subscribed Steam Workshop items on startup." );

#define WORKSHOP_UPDATE_INTERVAL 0.5
#define WORKSHOP_MAX_INIT_ATTEMPTS 10
// Time Steam gets to start a download or finish a subscribe request before
// the item is considered unavailable
#define WORKSHOP_DOWNLOAD_START_TIMEOUT 20.0
#define WORKSHOP_SUBSCRIBE_TIMEOUT 60.0

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
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::Shutdown()
{
	WarsWorkshopSteam_Shutdown();
	m_bSteamReady = false;
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

	UpdatePendingItems();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::TryInitSteam()
{
	if( m_bSteamReady )
		return true;

	m_iInitAttempts++;

	// The engine's ISteamClient can return interface versions newer than its own headers know
	ISteamClient *pSteamClient = SteamClient();
	HSteamUser hSteamUser = SteamAPI_GetHSteamUser();
	HSteamPipe hSteamPipe = SteamAPI_GetHSteamPipe();
	void *pSteamUGC = NULL;
	void *pSteamUtils = NULL;
	if( pSteamClient && hSteamUser && hSteamPipe )
	{
		pSteamUGC = pSteamClient->GetISteamGenericInterface( hSteamUser, hSteamPipe, WarsWorkshopSteam_GetUGCInterfaceVersion() );
		pSteamUtils = pSteamClient->GetISteamUtils( hSteamPipe, WarsWorkshopSteam_GetUtilsInterfaceVersion() );
	}
	m_bSteamReady = WarsWorkshopSteam_Init( pSteamUGC, pSteamUtils );
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
			QueueItem( item, false );
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

	ClearItemFailed( item );
	QueueItem( item, true );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::UnsubscribeItem( WorkshopItemId_t item )
{
	if( !m_bSteamReady || !WarsWorkshopSteam_UnsubscribeItem( item ) )
		return false;

	int pendingIdx = FindPendingItem( item );
	if( pendingIdx != -1 && m_PendingItems[pendingIdx].bSubscribing )
		m_PendingItems.Remove( pendingIdx );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::RequestItem( WorkshopItemId_t item )
{
	if( !m_bSteamReady || !item )
		return false;

	unsigned int state = WarsWorkshopSteam_GetItemState( item );
	if( FindMountedItem( item ) != -1 && !(state & WORKSHOP_ITEM_NEEDS_UPDATE) )
		return true;
	if( FindPendingItem( item ) != -1 )
		return true;

	ClearItemFailed( item );
	if( !(state & (WORKSHOP_ITEM_DOWNLOADING|WORKSHOP_ITEM_DOWNLOAD_PENDING)) && !WarsWorkshopSteam_DownloadItem( item, true ) )
	{
		SetItemFailed( item, WORKSHOP_STATUS_FAILED, "Steam refused to download the item" );
		return false;
	}
	QueueItem( item, false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::QueueItem( WorkshopItemId_t item, bool bSubscribing )
{
	if( FindPendingItem( item ) != -1 )
		return;

	PendingItem_t &pending = m_PendingItems[m_PendingItems.AddToTail()];
	pending.item = item;
	pending.fRequestTime = Plat_FloatTime();
	pending.bSubscribing = bSubscribing;
}

//-----------------------------------------------------------------------------
// Purpose: Mounts pending items once Steam installed them
//-----------------------------------------------------------------------------
void CWarsWorkshop::UpdatePendingItems()
{
	double fCurTime = Plat_FloatTime();
	for( int i = m_PendingItems.Count() - 1; i >= 0; i-- )
	{
		PendingItem_t pending = m_PendingItems[i];
		double fElapsed = fCurTime - pending.fRequestTime;
		unsigned int state = WarsWorkshopSteam_GetItemState( pending.item );

		if( state & (WORKSHOP_ITEM_DOWNLOADING|WORKSHOP_ITEM_DOWNLOAD_PENDING) )
			continue;
		// Steam may not have started the update yet
		if( (state & WORKSHOP_ITEM_NEEDS_UPDATE) && fElapsed < WORKSHOP_DOWNLOAD_START_TIMEOUT )
			continue;

		if( state & WORKSHOP_ITEM_INSTALLED )
		{
			m_PendingItems.Remove( i );

			int mountedIdx = FindMountedItem( pending.item );
			if( mountedIdx == -1 )
			{
				TryMountItem( pending.item );
				continue;
			}

			// An update of a mounted item finished, its content must be validated again
			char szReason[512];
			if( !WarsWorkshop_ValidateItemFolder( m_MountedItems[mountedIdx].folder, szReason, sizeof( szReason ) ) )
			{
				Warning( "Workshop: unmounting updated item %llu: %s\n", pending.item, szReason );
				UnmountItem( mountedIdx );
				SetItemFailed( pending.item, WORKSHOP_STATUS_REJECTED, szReason );
			}
			continue;
		}

		// Not installed and not downloading
		if( pending.bSubscribing && !(state & WORKSHOP_ITEM_SUBSCRIBED) && fElapsed < WORKSHOP_SUBSCRIBE_TIMEOUT )
			continue;
		if( fElapsed < WORKSHOP_DOWNLOAD_START_TIMEOUT )
			continue;

		m_PendingItems.Remove( i );
		Warning( "Workshop: item %llu could not be downloaded (state %u)\n", pending.item, state );
		SetItemFailed( pending.item, WORKSHOP_STATUS_FAILED, "Download failed, the item may be private or removed" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CWarsWorkshop::TryMountItem( WorkshopItemId_t item )
{
	if( FindMountedItem( item ) != -1 )
		return true;

	char szFolder[MAX_PATH];
	if( !WarsWorkshopSteam_GetItemInstallInfo( item, szFolder, sizeof( szFolder ), NULL, NULL ) )
	{
		Warning( "Workshop: no install info for item %llu\n", item );
		SetItemFailed( item, WORKSHOP_STATUS_FAILED, "Item is not installed" );
		return false;
	}

	char szReason[512];
	if( !WarsWorkshop_ValidateItemFolder( szFolder, szReason, sizeof( szReason ) ) )
	{
		Warning( "Workshop: not mounting item %llu: %s\n", item, szReason );
		SetItemFailed( item, WORKSHOP_STATUS_REJECTED, szReason );
		return false;
	}

	filesystem->AddSearchPath( szFolder, "GAME", PATH_ADD_TO_TAIL );
	filesystem->AddSearchPath( szFolder, WARS_WORKSHOP_PATHID, PATH_ADD_TO_TAIL );

	MountedItem_t &mounted = m_MountedItems[m_MountedItems.AddToTail()];
	mounted.item = item;
	mounted.folder = szFolder;
	ClearItemFailed( item );

	Msg( "Workshop: mounted item %llu (%s)\n", item, szFolder );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::UnmountItem( int mountedIdx )
{
	const char *pszFolder = m_MountedItems[mountedIdx].folder;
	filesystem->RemoveSearchPath( pszFolder, "GAME" );
	filesystem->RemoveSearchPath( pszFolder, WARS_WORKSHOP_PATHID );
	m_MountedItems.Remove( mountedIdx );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
WorkshopItemStatus_t CWarsWorkshop::GetItemStatus( WorkshopItemId_t item )
{
	if( FindPendingItem( item ) != -1 )
		return WORKSHOP_STATUS_DOWNLOADING;
	if( FindMountedItem( item ) != -1 )
		return WORKSHOP_STATUS_MOUNTED;
	int failedIdx = FindFailedItem( item );
	if( failedIdx != -1 )
		return m_FailedItems[failedIdx].status;
	return WORKSHOP_STATUS_NONE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const char *CWarsWorkshop::GetItemError( WorkshopItemId_t item )
{
	int failedIdx = FindFailedItem( item );
	return failedIdx != -1 ? m_FailedItems[failedIdx].error.Get() : "";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
WorkshopItemId_t CWarsWorkshop::FindMapItem( const char *pszMapName )
{
	for( int i = 0; i < m_MountedItems.Count(); i++ )
	{
		char szPath[MAX_PATH];
		V_snprintf( szPath, sizeof( szPath ), "%s\\maps\\%s.bsp", m_MountedItems[i].folder.Get(), pszMapName );
		if( _access( szPath, 0 ) == 0 )
			return m_MountedItems[i].item;
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWarsWorkshop::SetItemFailed( WorkshopItemId_t item, WorkshopItemStatus_t status, const char *pszError )
{
	int failedIdx = FindFailedItem( item );
	if( failedIdx == -1 )
		failedIdx = m_FailedItems.AddToTail();
	m_FailedItems[failedIdx].item = item;
	m_FailedItems[failedIdx].status = status;
	m_FailedItems[failedIdx].error = pszError;
}

void CWarsWorkshop::ClearItemFailed( WorkshopItemId_t item )
{
	int failedIdx = FindFailedItem( item );
	if( failedIdx != -1 )
		m_FailedItems.Remove( failedIdx );
}

int CWarsWorkshop::FindMountedItem( WorkshopItemId_t item )
{
	for( int i = 0; i < m_MountedItems.Count(); i++ )
	{
		if( m_MountedItems[i].item == item )
			return i;
	}
	return -1;
}

int CWarsWorkshop::FindPendingItem( WorkshopItemId_t item )
{
	for( int i = 0; i < m_PendingItems.Count(); i++ )
	{
		if( m_PendingItems[i].item == item )
			return i;
	}
	return -1;
}

int CWarsWorkshop::FindFailedItem( WorkshopItemId_t item )
{
	for( int i = 0; i < m_FailedItems.Count(); i++ )
	{
		if( m_FailedItems[i].item == item )
			return i;
	}
	return -1;
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

	// Also list items that were requested without subscribing
	for( int i = 0; i < m_MountedItems.Count(); i++ )
	{
		if( !items.HasElement( m_MountedItems[i].item ) )
			items.AddToTail( m_MountedItems[i].item );
	}
	for( int i = 0; i < m_PendingItems.Count(); i++ )
	{
		if( !items.HasElement( m_PendingItems[i].item ) )
			items.AddToTail( m_PendingItems[i].item );
	}
	for( int i = 0; i < m_FailedItems.Count(); i++ )
	{
		if( !items.HasElement( m_FailedItems[i].item ) )
			items.AddToTail( m_FailedItems[i].item );
	}

	static const char *s_pszStatusNames[] = { "-", "downloading", "mounted", "failed", "rejected" };

	Msg( "Workshop: %d item(s)\n", items.Count() );
	for( int i = 0; i < items.Count(); i++ )
	{
		WorkshopItemId_t item = items[i];
		unsigned int state = WarsWorkshopSteam_GetItemState( item );
		WorkshopItemStatus_t status = GetItemStatus( item );

		char szFolder[MAX_PATH];
		if( !WarsWorkshopSteam_GetItemInstallInfo( item, szFolder, sizeof( szFolder ), NULL, NULL ) )
			V_strncpy( szFolder, "-", sizeof( szFolder ) );

		Msg( "  %llu: %s, state %u%s%s%s%s%s, %s\n", item, s_pszStatusNames[status], state,
			(state & WORKSHOP_ITEM_SUBSCRIBED) ? " subscribed" : "",
			(state & WORKSHOP_ITEM_INSTALLED) ? " installed" : "",
			(state & WORKSHOP_ITEM_NEEDS_UPDATE) ? " needs-update" : "",
			(state & WORKSHOP_ITEM_DOWNLOADING) ? " downloading" : "",
			(state & WORKSHOP_ITEM_DOWNLOAD_PENDING) ? " download-pending" : "",
			szFolder );

		if( status == WORKSHOP_STATUS_FAILED || status == WORKSHOP_STATUS_REJECTED )
			Msg( "    %s\n", GetItemError( item ) );

		unsigned long long bytesDownloaded, bytesTotal;
		if( (state & WORKSHOP_ITEM_DOWNLOADING) && WarsWorkshopSteam_GetItemDownloadInfo( item, &bytesDownloaded, &bytesTotal ) )
			Msg( "    downloaded %llu / %llu bytes\n", bytesDownloaded, bytesTotal );
	}
}

CON_COMMAND( wars_workshop_list, "Lists Steam Workshop items and their state" )
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

CON_COMMAND( wars_workshop_download, "Downloads a Steam Workshop item by id without subscribing and mounts it" )
{
	if( args.ArgC() < 2 )
	{
		Msg( "Usage: wars_workshop_download <published file id>\n" );
		return;
	}

	WorkshopItemId_t item = V_atoui64( args[1] );
	if( !item || !WarsWorkshop().RequestItem( item ) )
	{
		Warning( "Workshop: failed to download item %s: %s\n", args[1], item ? WarsWorkshop().GetItemError( item ) : "invalid id" );
		return;
	}
	Msg( "Workshop: downloading item %llu\n", item );
}

CON_COMMAND( wars_workshop_validate, "Checks if a folder only contains content allowed in Steam Workshop items" )
{
	if( args.ArgC() < 2 )
	{
		Msg( "Usage: wars_workshop_validate <folder>\n" );
		return;
	}

	char szReason[512];
	if( WarsWorkshop_ValidateItemFolder( args[1], szReason, sizeof( szReason ) ) )
		Msg( "Workshop: %s is valid\n", args[1] );
	else
		Warning( "Workshop: %s is not valid: %s\n", args[1], szReason );
}
