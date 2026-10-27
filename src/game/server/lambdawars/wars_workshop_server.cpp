//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Downloads and mounts Steam Workshop items on dedicated servers.
//
// Items are downloaded when a lobby requests a game with a Workshop map, or
// with the console command wars_sv_workshop_download. On listen servers the
// client mounts Workshop items (see wars_workshop.cpp), so this only runs on
// dedicated servers. Old items are removed when the Workshop starts, see
// wars_sv_workshop_max_items.
//
// The game server's ISteamClient (set up by the engine's steam_api.dll) is
// asked for a current ISteamUGC by version, like the client does, so all
// communication with Steam goes through the engine's steam api.
//
//=============================================================================//

#include "cbase.h"
#include "wars_workshop_server.h"
#include "wars_workshop_steam.h"
#include "wars_workshop_content.h"
#include "filesystem.h"
#include "steam/steam_gameserver.h"
#include <io.h>
#include <direct.h>
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Folder in the mod directory Workshop items are downloaded to
#define WORKSHOP_SERVER_CONTENT_FOLDER "workshop_content"
// When each item was last used by a game, one "<published file id> <unix time>" per line
#define WORKSHOP_SERVER_LASTUSED_FILE WORKSHOP_SERVER_CONTENT_FOLDER "/lastused.txt"
#define WORKSHOP_SERVER_UPDATE_INTERVAL 0.5
// Time Steam gets to start a download before the item is considered unavailable
#define WORKSHOP_SERVER_DOWNLOAD_START_TIMEOUT 30.0

static ConVar wars_sv_workshop_max_items( "wars_sv_workshop_max_items", "30", 0,
	"Dedicated server: number of downloaded Steam Workshop items kept on disk. When the Workshop starts "
	"(at the first game with a Workshop map), the least recently used items above this number are removed. 0 keeps all items." );

struct WorkshopServerItem_t
{
	WorkshopItemId_t item;
	double fRequestTime;
	bool bMounted;
	bool bPending;
	CUtlString folder;
	CUtlString error;
};

static bool s_bInitialized = false;
static double s_fNextUpdateTime = 0.0;
static CUtlVector< WorkshopServerItem_t > s_Items;

static int FindItem( WorkshopItemId_t item );

// DownloadItemResult_t of Steamworks SDK 1.65 (isteamugc.h), which the engine's
// old steam headers don't have. Same layout: callbacks use 8 byte packing on Windows.
#pragma pack( push, 8 )
struct WarsDownloadItemResult_t
{
	enum { k_iCallback = k_iClientUGCCallbacks + 6 };
	AppId_t m_unAppID;
	PublishedFileId_t m_nPublishedFileId;
	EResult m_eResult;
};
#pragma pack( pop )

//-----------------------------------------------------------------------------
// Purpose: Receives the result of downloads through the engine's steam api,
//			which dispatches the game server's callbacks.
//-----------------------------------------------------------------------------
class CWarsWorkshopServerCallbacks
{
public:
	CWarsWorkshopServerCallbacks() :
		m_CallbackDownloadItemResult( this, &CWarsWorkshopServerCallbacks::OnDownloadItemResult )
	{
	}

private:
	void OnDownloadItemResult( WarsDownloadItemResult_t *pResult )
	{
		Msg( "Workshop: download result for item %llu (app %u): EResult %d\n", pResult->m_nPublishedFileId, pResult->m_unAppID, pResult->m_eResult );

		int idx = FindItem( pResult->m_nPublishedFileId );
		if( idx == -1 || pResult->m_eResult == k_EResultOK )
			return;

		WorkshopServerItem_t &entry = s_Items[idx];
		entry.bPending = false;
		entry.error.Format( "Download failed (EResult %d)", pResult->m_eResult );
		Warning( "Workshop: if the Workshop is not public, download the item with SteamCMD and a developer account, then use wars_sv_workshop_mount\n" );
	}

	CCallback< CWarsWorkshopServerCallbacks, WarsDownloadItemResult_t, true > m_CallbackDownloadItemResult;
};

static CWarsWorkshopServerCallbacks *s_pCallbacks = NULL;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static void PrintGameServerStatus()
{
	ISteamGameServer *pSteamGameServer = steamgameserverapicontext ? steamgameserverapicontext->SteamGameServer() : NULL;
	if( !pSteamGameServer )
	{
		Msg( "Workshop: no Steam game server interface\n" );
		return;
	}
	Msg( "Workshop: game server logged on: %s, secure: %s\n",
		pSteamGameServer->BLoggedOn() ? "yes" : "no", pSteamGameServer->BSecure() ? "yes" : "no" );
}

//-----------------------------------------------------------------------------
// Purpose: When downloaded items were last used by a game
//-----------------------------------------------------------------------------
struct WorkshopLastUsed_t
{
	WorkshopItemId_t item;
	unsigned int time;
};

static void LoadLastUsed( CUtlVector< WorkshopLastUsed_t > &lastUsed )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if( !filesystem->ReadFile( WORKSHOP_SERVER_LASTUSED_FILE, "MOD", buf ) )
		return;

	char szLine[128];
	while( buf.IsValid() )
	{
		buf.GetLine( szLine, sizeof( szLine ) );
		if( !szLine[0] )
			break;
		const char *pszTime = V_strstr( szLine, " " );
		WorkshopLastUsed_t entry;
		entry.item = V_atoui64( szLine );
		entry.time = pszTime ? (unsigned int)V_atoui64( pszTime + 1 ) : 0;
		if( entry.item )
			lastUsed.AddToTail( entry );
	}
}

static unsigned int GetLastUsedTime( const CUtlVector< WorkshopLastUsed_t > &lastUsed, WorkshopItemId_t item )
{
	for( int i = 0; i < lastUsed.Count(); i++ )
	{
		if( lastUsed[i].item == item )
			return lastUsed[i].time;
	}
	return 0;
}

static void SaveLastUsed( const CUtlVector< WorkshopLastUsed_t > &lastUsed )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	for( int i = 0; i < lastUsed.Count(); i++ )
		buf.Printf( "%llu %u\n", lastUsed[i].item, lastUsed[i].time );
	filesystem->CreateDirHierarchy( WORKSHOP_SERVER_CONTENT_FOLDER, "MOD" );
	filesystem->WriteFile( WORKSHOP_SERVER_LASTUSED_FILE, "MOD", buf );
}

static void MarkItemUsed( WorkshopItemId_t item )
{
	CUtlVector< WorkshopLastUsed_t > lastUsed;
	LoadLastUsed( lastUsed );

	int i;
	for( i = 0; i < lastUsed.Count(); i++ )
	{
		if( lastUsed[i].item == item )
			break;
	}
	if( i == lastUsed.Count() )
	{
		lastUsed.AddToTail();
		lastUsed[i].item = item;
	}
	lastUsed[i].time = (unsigned int)time( NULL );
	SaveLastUsed( lastUsed );
}

//-----------------------------------------------------------------------------
// Purpose: Removes the items in the Steam manifest (appworkshop_<appid>.acf),
//			so Steam downloads them again when they are requested. Only edits
//			the blocks of the items:
//				"<published file id>"
//				{
//					...
//				}
//-----------------------------------------------------------------------------
static const char *TrimWhitespace( char *pszText )
{
	while( *pszText == ' ' || *pszText == '\t' )
		pszText++;
	int len = V_strlen( pszText );
	while( len > 0 && ( pszText[len-1] == ' ' || pszText[len-1] == '\t' || pszText[len-1] == '\r' || pszText[len-1] == '\n' ) )
		pszText[--len] = '\0';
	return pszText;
}

static void RemoveItemsFromSteamManifest( const char *pszManifest, const CUtlVector< WorkshopItemId_t > &removed )
{
	// Binary buffers, so the line endings stay as Steam wrote them
	CUtlBuffer buf;
	if( !filesystem->ReadFile( pszManifest, NULL, buf ) )
		return;

	CUtlBuffer out;
	char szLine[1024], szToken[1024];
	int skipDepth = 0;
	bool bSkipNextBlock = false;
	while( buf.IsValid() )
	{
		buf.GetLine( szLine, sizeof( szLine ) );
		if( !szLine[0] )
			break;
		V_strncpy( szToken, szLine, sizeof( szToken ) );
		const char *pszToken = TrimWhitespace( szToken );

		if( skipDepth > 0 )
		{
			if( !V_strcmp( pszToken, "{" ) )
				skipDepth++;
			else if( !V_strcmp( pszToken, "}" ) )
				skipDepth--;
			continue;
		}
		if( bSkipNextBlock )
		{
			bSkipNextBlock = false;
			if( !V_strcmp( pszToken, "{" ) )
			{
				skipDepth = 1;
				continue;
			}
		}

		int len = V_strlen( pszToken );
		if( len > 2 && pszToken[0] == '"' && pszToken[len-1] == '"' && removed.HasElement( V_atoui64( pszToken + 1 ) ) )
		{
			bSkipNextBlock = true;
			continue;
		}
		out.Put( szLine, V_strlen( szLine ) );
	}

	if( !filesystem->WriteFile( pszManifest, NULL, out ) )
		Warning( "Workshop: could not update %s\n", pszManifest );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static bool DeleteFolder( const char *pszFolder )
{
	char szWildcard[MAX_PATH];
	V_ComposeFileName( pszFolder, "*", szWildcard, sizeof( szWildcard ) );

	_finddata_t findData;
	intptr_t hFind = _findfirst( szWildcard, &findData );
	if( hFind != -1 )
	{
		do
		{
			if( !V_strcmp( findData.name, "." ) || !V_strcmp( findData.name, ".." ) )
				continue;
			char szPath[MAX_PATH];
			V_ComposeFileName( pszFolder, findData.name, szPath, sizeof( szPath ) );
			if( findData.attrib & _A_SUBDIR )
				DeleteFolder( szPath );
			else
				_unlink( szPath );
		}
		while( _findnext( hFind, &findData ) == 0 );
		_findclose( hFind );
	}
	return _rmdir( pszFolder ) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: Keeps at most wars_sv_workshop_max_items downloaded items, removing
//			the least recently used ones. Runs before Steam reads its manifest
//			in BInitWorkshopForGameServer, since Steam doesn't expect items to
//			disappear while it runs.
//-----------------------------------------------------------------------------
static void RemoveOldItems( const char *pszWorkshopFolder, unsigned int appID, WorkshopItemId_t keepItem )
{
	int maxItems = wars_sv_workshop_max_items.GetInt();
	if( maxItems <= 0 )
		return;

	// Steam stores items in <folder>/content/<appid>/<published file id>
	char szContentFolder[MAX_PATH], szWildcard[MAX_PATH];
	V_snprintf( szContentFolder, sizeof( szContentFolder ), "%s%ccontent%c%u", pszWorkshopFolder, CORRECT_PATH_SEPARATOR, CORRECT_PATH_SEPARATOR, appID );
	V_ComposeFileName( szContentFolder, "*", szWildcard, sizeof( szWildcard ) );

	CUtlVector< WorkshopItemId_t > items;
	_finddata_t findData;
	intptr_t hFind = _findfirst( szWildcard, &findData );
	if( hFind == -1 )
		return;
	do
	{
		WorkshopItemId_t item = V_atoui64( findData.name );
		if( ( findData.attrib & _A_SUBDIR ) && item )
			items.AddToTail( item );
	}
	while( _findnext( hFind, &findData ) == 0 );
	_findclose( hFind );

	// The requested item counts as kept, also if it still has to be downloaded
	int numItems = items.Count() + ( ( keepItem && !items.HasElement( keepItem ) ) ? 1 : 0 );
	if( numItems <= maxItems )
		return;

	CUtlVector< WorkshopLastUsed_t > lastUsed;
	LoadLastUsed( lastUsed );

	// Items in use are never removed. Items without a last used time are removed first.
	CUtlVector< WorkshopLastUsed_t > candidates;
	for( int i = 0; i < items.Count(); i++ )
	{
		if( items[i] == keepItem || FindItem( items[i] ) != -1 )
			continue;
		WorkshopLastUsed_t candidate;
		candidate.item = items[i];
		candidate.time = GetLastUsedTime( lastUsed, items[i] );
		int pos = 0;
		while( pos < candidates.Count() && candidates[pos].time <= candidate.time )
			pos++;
		candidates.InsertBefore( pos, candidate );
	}

	CUtlVector< WorkshopItemId_t > removed;
	for( int i = 0; i < candidates.Count() && numItems > maxItems; i++ )
	{
		char szItemFolder[MAX_PATH], szName[32];
		V_snprintf( szName, sizeof( szName ), "%llu", candidates[i].item );
		V_ComposeFileName( szContentFolder, szName, szItemFolder, sizeof( szItemFolder ) );
		if( !DeleteFolder( szItemFolder ) )
		{
			Warning( "Workshop: could not remove item %llu (%s)\n", candidates[i].item, szItemFolder );
			continue;
		}
		Msg( "Workshop: removed item %llu, more than %d items are downloaded (wars_sv_workshop_max_items)\n", candidates[i].item, maxItems );
		removed.AddToTail( candidates[i].item );
		numItems--;
	}

	if( removed.Count() == 0 )
		return;

	char szManifest[MAX_PATH], szManifestName[64];
	V_snprintf( szManifestName, sizeof( szManifestName ), "appworkshop_%u.acf", appID );
	V_ComposeFileName( pszWorkshopFolder, szManifestName, szManifest, sizeof( szManifest ) );
	RemoveItemsFromSteamManifest( szManifest, removed );

	for( int i = lastUsed.Count() - 1; i >= 0; i-- )
	{
		if( removed.HasElement( lastUsed[i].item ) )
			lastUsed.Remove( i );
	}
	SaveLastUsed( lastUsed );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static unsigned int GetGameAppID();

static bool InitWorkshopServer( WorkshopItemId_t keepItem )
{
	if( s_bInitialized )
		return true;

	if( !engine->IsDedicatedServer() )
	{
		Warning( "Workshop: server downloads are only used on dedicated servers\n" );
		return false;
	}

	// The engine's game server client is an older interface version, see steam_gameserver.h
	ISteamClient009 *pSteamClient = (ISteamClient009 *)g_pSteamClientGameServer;
	HSteamUser hSteamUser = SteamGameServer_GetHSteamUser();
	HSteamPipe hSteamPipe = SteamGameServer_GetHSteamPipe();
	if( !pSteamClient || !hSteamUser || !hSteamPipe )
	{
		Warning( "Workshop: the Steam game server is not initialized yet\n" );
		return false;
	}

	void *pSteamUGC = pSteamClient->GetISteamGenericInterface( hSteamUser, hSteamPipe, WarsWorkshopSteam_GetUGCInterfaceVersion() );
	void *pSteamUtils = pSteamClient->GetISteamUtils( hSteamPipe, WarsWorkshopSteam_GetUtilsInterfaceVersion() );
	if( !WarsWorkshopSteam_Init( pSteamUGC, pSteamUtils ) )
	{
		Warning( "Workshop: Steam UGC not available for the game server: %s\n", WarsWorkshopSteam_GetError() );
		return false;
	}

	unsigned int gameAppID = GetGameAppID();
	if( !gameAppID )
	{
		Warning( "Workshop: could not read appID from steam.inf\n" );
		WarsWorkshopSteam_Shutdown();
		return false;
	}

	char szFolder[MAX_PATH];
	filesystem->CreateDirHierarchy( WORKSHOP_SERVER_CONTENT_FOLDER, "MOD" );
	filesystem->RelativePathToFullPath( WORKSHOP_SERVER_CONTENT_FOLDER, "MOD", szFolder, sizeof( szFolder ) );
	RemoveOldItems( szFolder, gameAppID, keepItem );
	if( !WarsWorkshopSteam_InitWorkshopForGameServer( gameAppID, szFolder ) )
	{
		Warning( "Workshop: BInitWorkshopForGameServer failed for depot %u, folder %s\n", gameAppID, szFolder );
		WarsWorkshopSteam_Shutdown();
		return false;
	}

	Msg( "Workshop: Steam UGC available for the game server (server app %u, Workshop of app %u), downloading to %s\n",
		WarsWorkshopSteam_GetAppID(), gameAppID, szFolder );
	PrintGameServerStatus();
	s_pCallbacks = new CWarsWorkshopServerCallbacks();
	s_bInitialized = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static int FindItem( WorkshopItemId_t item )
{
	for( int i = 0; i < s_Items.Count(); i++ )
	{
		if( s_Items[i].item == item )
			return i;
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static bool RequestItem( WorkshopItemId_t item )
{
	if( !InitWorkshopServer( item ) )
		return false;

	int idx = FindItem( item );
	if( idx == -1 )
	{
		idx = s_Items.AddToTail();
		s_Items[idx].item = item;
		s_Items[idx].bMounted = false;
	}
	WorkshopServerItem_t &entry = s_Items[idx];
	entry.fRequestTime = Plat_FloatTime();
	entry.bPending = true;
	entry.error = "";

	if( !WarsWorkshopSteam_DownloadItem( item, true ) )
	{
		entry.bPending = false;
		entry.error = "Steam refused to download the item";
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static bool MountFolder( WorkshopServerItem_t &entry, const char *pszFolder )
{
	char szReason[512];
	if( !WarsWorkshop_ValidateItemFolder( pszFolder, szReason, sizeof( szReason ) ) )
	{
		entry.error = szReason;
		Warning( "Workshop: not mounting item %llu: %s\n", entry.item, szReason );
		return false;
	}

	if( !entry.bMounted )
	{
		filesystem->AddSearchPath( pszFolder, "GAME", PATH_ADD_TO_TAIL );
		filesystem->AddSearchPath( pszFolder, "WORKSHOP", PATH_ADD_TO_TAIL );
		entry.bMounted = true;
		entry.folder = pszFolder;
	}
	entry.error = "";
	Msg( "Workshop: mounted item %llu (%s)\n", entry.item, pszFolder );
	return true;
}

static void MountItem( WorkshopServerItem_t &entry )
{
	char szFolder[MAX_PATH];
	if( !WarsWorkshopSteam_GetItemInstallInfo( entry.item, szFolder, sizeof( szFolder ), NULL, NULL ) )
	{
		entry.error = "No install info after download";
		Warning( "Workshop: item %llu: %s\n", entry.item, entry.error.Get() );
		return;
	}
	MountFolder( entry, szFolder );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WarsWorkshopServer_Update()
{
	if( !s_bInitialized )
		return;

	double fCurTime = Plat_FloatTime();
	if( fCurTime < s_fNextUpdateTime )
		return;
	s_fNextUpdateTime = fCurTime + WORKSHOP_SERVER_UPDATE_INTERVAL;

	for( int i = 0; i < s_Items.Count(); i++ )
	{
		WorkshopServerItem_t &entry = s_Items[i];
		if( !entry.bPending )
			continue;

		unsigned int state = WarsWorkshopSteam_GetItemState( entry.item );
		if( state & (WORKSHOP_ITEM_DOWNLOADING|WORKSHOP_ITEM_DOWNLOAD_PENDING) )
			continue;

		double fElapsed = fCurTime - entry.fRequestTime;
		if( (state & WORKSHOP_ITEM_INSTALLED) && !(state & WORKSHOP_ITEM_NEEDS_UPDATE) )
		{
			entry.bPending = false;
			MountItem( entry );
		}
		else if( fElapsed > WORKSHOP_SERVER_DOWNLOAD_START_TIMEOUT )
		{
			entry.bPending = false;
			entry.error = "Download failed, the item may be private or removed";
			Warning( "Workshop: item %llu could not be downloaded (state %u)\n", entry.item, state );
			Warning( "Workshop: if the Workshop is not public, download the item with SteamCMD and a developer account, then use wars_sv_workshop_mount\n" );
		}
	}
}

CON_COMMAND( wars_sv_workshop_download, "Dedicated server: downloads a Steam Workshop item by id and mounts it" )
{
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if( args.ArgC() < 2 )
	{
		Msg( "Usage: wars_sv_workshop_download <published file id>\n" );
		return;
	}

	WorkshopItemId_t item = V_atoui64( args[1] );
	if( !item || !RequestItem( item ) )
	{
		Warning( "Workshop: failed to download item %s\n", args[1] );
		return;
	}
	MarkItemUsed( item );
	Msg( "Workshop: downloading item %llu\n", item );
}

CON_COMMAND( wars_sv_workshop_mount, "Dedicated server: mounts a Workshop item folder downloaded beforehand, for example with SteamCMD" )
{
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if( args.ArgC() < 2 )
	{
		Msg( "Usage: wars_sv_workshop_mount \"<folder of the item>\"\n"
			"  For example: wars_sv_workshop_mount \"C:\\steamcmd\\steamapps\\workshop\\content\\270370\\<published file id>\"\n" );
		return;
	}

	if( !engine->IsDedicatedServer() )
	{
		Warning( "Workshop: server mounts are only used on dedicated servers\n" );
		return;
	}

	// The folder of a downloaded item is named after its published file id
	char szFolder[MAX_PATH];
	V_strncpy( szFolder, args[1], sizeof( szFolder ) );
	V_FixSlashes( szFolder );
	V_StripTrailingSlash( szFolder );
	WorkshopItemId_t item = V_atoui64( V_UnqualifiedFileName( szFolder ) );

	int idx = item ? FindItem( item ) : -1;
	if( idx == -1 )
	{
		idx = s_Items.AddToTail();
		s_Items[idx].item = item;
		s_Items[idx].bMounted = false;
		s_Items[idx].fRequestTime = Plat_FloatTime();
	}
	s_Items[idx].bPending = false;
	MountFolder( s_Items[idx], szFolder );
}

CON_COMMAND( wars_sv_workshop_list, "Dedicated server: lists requested and mounted Steam Workshop items" )
{
	if( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	if( s_bInitialized )
		PrintGameServerStatus();
	Msg( "Workshop: %d item(s)\n", s_Items.Count() );
	for( int i = 0; i < s_Items.Count(); i++ )
	{
		const WorkshopServerItem_t &entry = s_Items[i];
		unsigned int state = s_bInitialized ? WarsWorkshopSteam_GetItemState( entry.item ) : 0;
		Msg( "  %llu: %s, state %u, %s\n", entry.item,
			entry.bPending ? "downloading" : ( entry.bMounted ? "mounted" : "failed" ), state,
			entry.bMounted ? entry.folder.Get() : entry.error.Get() );

		unsigned long long bytesDownloaded, bytesTotal;
		if( s_bInitialized && entry.bPending && WarsWorkshopSteam_GetItemDownloadInfo( entry.item, &bytesDownloaded, &bytesTotal ) )
			Msg( "    downloaded %llu / %llu bytes\n", bytesDownloaded, bytesTotal );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns the app id of the game from steam.inf ("appID=270370"), or 0.
//			Dedicated servers run as their own app (ServerAppID), but the
//			Workshop items belong to the game.
//-----------------------------------------------------------------------------
static unsigned int GetGameAppID()
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if( !filesystem->ReadFile( "steam.inf", "MOD", buf ) )
		return 0;

	char szLine[256];
	while( buf.IsValid() )
	{
		buf.GetLine( szLine, sizeof( szLine ) );
		if( !szLine[0] )
			break;
		if( !V_strnicmp( szLine, "appID=", 6 ) )
			return (unsigned int)V_atoi( szLine + 6 );
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopServer_IsSupported()
{
	return engine->IsDedicatedServer();
}

//-----------------------------------------------------------------------------
// Purpose: Items mounted by the server operator (wars_sv_workshop_mount) are
//			used as they are. Items downloaded by Steam are updated if needed.
//-----------------------------------------------------------------------------
WorkshopServerItemStatus_t WarsWorkshopServer_PrepareItem( WorkshopItemId_t item )
{
	MarkItemUsed( item );

	int idx = FindItem( item );
	if( idx != -1 )
	{
		const WorkshopServerItem_t &entry = s_Items[idx];
		if( entry.bPending )
			return WORKSHOP_SERVER_ITEM_PENDING;
		if( entry.bMounted && ( !s_bInitialized || !(WarsWorkshopSteam_GetItemState( item ) & WORKSHOP_ITEM_NEEDS_UPDATE) ) )
			return WORKSHOP_SERVER_ITEM_READY;
	}

	return RequestItem( item ) ? WORKSHOP_SERVER_ITEM_PENDING : WORKSHOP_SERVER_ITEM_FAILED;
}

//-----------------------------------------------------------------------------
// Purpose: A mounted item stays usable if its update failed. The caller
//			checks the map revision.
//-----------------------------------------------------------------------------
WorkshopServerItemStatus_t WarsWorkshopServer_GetItemStatus( WorkshopItemId_t item )
{
	int idx = FindItem( item );
	if( idx == -1 )
		return WORKSHOP_SERVER_ITEM_FAILED;
	if( s_Items[idx].bPending )
		return WORKSHOP_SERVER_ITEM_PENDING;
	return s_Items[idx].bMounted ? WORKSHOP_SERVER_ITEM_READY : WORKSHOP_SERVER_ITEM_FAILED;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *WarsWorkshopServer_GetItemError( WorkshopItemId_t item )
{
	int idx = FindItem( item );
	if( idx == -1 )
		return "Workshop not available on this server";
	return s_Items[idx].error.Get();
}
