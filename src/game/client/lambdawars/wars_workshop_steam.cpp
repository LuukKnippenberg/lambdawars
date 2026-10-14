//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Access to a current ISteamUGC (Steam Workshop) interface.
//
// The engine uses a 2010-era steam_api.dll without a usable ISteamUGC, and we
// cannot replace it because the engine is built against it. Instead we ask the
// Steam client's own steamclient.dll (already loaded by steam_api.dll) for a
// current ISteamClient, and reuse the pipe and user handles that the old
// steam_api.dll opened.
//
// This file must not include cbase.h or any of the old steam headers. The
// Steamworks SDK 1.65 headers in public/steamworks_ugc are wrapped in their own
// namespace so their classes cannot clash with the old versions used by the
// rest of the dll. No precompiled header is used for this file.
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

namespace WarsSteamworks
{
#include "steamworks_ugc/isteamclient.h"
#include "steamworks_ugc/isteamugc.h"
#include "steamworks_ugc/isteamutils.h"
}

#include "wars_workshop_steam.h"

using namespace WarsSteamworks;

typedef void *(__cdecl *SteamCreateInterfaceFn)( const char *pName, int *pReturnCode );
typedef HSteamPipe (__cdecl *SteamGetHSteamPipeFn)();
typedef HSteamUser (__cdecl *SteamGetHSteamUserFn)();

static ISteamUGC *s_pSteamUGC = NULL;
// Used to poll the results of asynchronous calls. The old steam_api.dll dispatcher
// cannot deliver call results for these newer structures, so they are not registered.
static ISteamUtils *s_pSteamUtils = NULL;
static char s_szError[256] = "Not initialized";

static void SetError( const char *pError )
{
	_snprintf_s( s_szError, sizeof( s_szError ), _TRUNCATE, "%s", pError );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_Init()
{
	if( s_pSteamUGC )
		return true;

	HMODULE hSteamAPI = GetModuleHandleA( "steam_api.dll" );
	HMODULE hSteamClient = GetModuleHandleA( "steamclient.dll" );
	if( !hSteamAPI || !hSteamClient )
	{
		SetError( "steam_api.dll or steamclient.dll is not loaded (is Steam running?)" );
		return false;
	}

	SteamGetHSteamPipeFn pfnGetHSteamPipe = (SteamGetHSteamPipeFn)GetProcAddress( hSteamAPI, "SteamAPI_GetHSteamPipe" );
	SteamGetHSteamUserFn pfnGetHSteamUser = (SteamGetHSteamUserFn)GetProcAddress( hSteamAPI, "SteamAPI_GetHSteamUser" );
	SteamCreateInterfaceFn pfnCreateInterface = (SteamCreateInterfaceFn)GetProcAddress( hSteamClient, "CreateInterface" );
	if( !pfnGetHSteamPipe || !pfnGetHSteamUser || !pfnCreateInterface )
	{
		SetError( "Missing exports in steam_api.dll or steamclient.dll" );
		return false;
	}

	HSteamPipe hSteamPipe = pfnGetHSteamPipe();
	HSteamUser hSteamUser = pfnGetHSteamUser();
	if( !hSteamPipe || !hSteamUser )
	{
		SetError( "Steam api is not initialized yet" );
		return false;
	}

	ISteamClient *pSteamClient = (ISteamClient *)pfnCreateInterface( STEAMCLIENT_INTERFACE_VERSION, NULL );
	if( !pSteamClient )
	{
		SetError( "steamclient.dll does not provide " STEAMCLIENT_INTERFACE_VERSION );
		return false;
	}

	ISteamUtils *pSteamUtils = pSteamClient->GetISteamUtils( hSteamPipe, STEAMUTILS_INTERFACE_VERSION );
	if( !pSteamUtils )
	{
		SetError( "steamclient.dll does not provide " STEAMUTILS_INTERFACE_VERSION );
		return false;
	}

	s_pSteamUGC = pSteamClient->GetISteamUGC( hSteamUser, hSteamPipe, STEAMUGC_INTERFACE_VERSION );
	if( !s_pSteamUGC )
	{
		SetError( "steamclient.dll does not provide " STEAMUGC_INTERFACE_VERSION );
		return false;
	}
	s_pSteamUtils = pSteamUtils;

	SetError( "" );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_IsAvailable()
{
	return s_pSteamUGC != NULL;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const char *WarsWorkshopSteam_GetError()
{
	return s_szError;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int WarsWorkshopSteam_GetNumSubscribedItems()
{
	if( !s_pSteamUGC )
		return 0;
	return (int)s_pSteamUGC->GetNumSubscribedItems();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int WarsWorkshopSteam_GetSubscribedItems( WorkshopItemId_t *pItems, int maxItems )
{
	if( !s_pSteamUGC || !pItems || maxItems <= 0 )
		return 0;
	return (int)s_pSteamUGC->GetSubscribedItems( pItems, (uint32)maxItems );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
unsigned int WarsWorkshopSteam_GetItemState( WorkshopItemId_t item )
{
	if( !s_pSteamUGC )
		return WORKSHOP_ITEM_NONE;
	return s_pSteamUGC->GetItemState( item );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetItemInstallInfo( WorkshopItemId_t item, char *pszFolder, int folderSize, unsigned long long *pSizeOnDisk, unsigned int *pTimeStamp )
{
	if( !s_pSteamUGC || !pszFolder || folderSize <= 0 )
		return false;

	uint64 sizeOnDisk = 0;
	uint32 timeStamp = 0;
	if( !s_pSteamUGC->GetItemInstallInfo( item, &sizeOnDisk, pszFolder, (uint32)folderSize, &timeStamp ) )
		return false;

	if( pSizeOnDisk )
		*pSizeOnDisk = sizeOnDisk;
	if( pTimeStamp )
		*pTimeStamp = timeStamp;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetItemDownloadInfo( WorkshopItemId_t item, unsigned long long *pBytesDownloaded, unsigned long long *pBytesTotal )
{
	if( !s_pSteamUGC )
		return false;

	uint64 bytesDownloaded = 0;
	uint64 bytesTotal = 0;
	if( !s_pSteamUGC->GetItemDownloadInfo( item, &bytesDownloaded, &bytesTotal ) )
		return false;

	if( pBytesDownloaded )
		*pBytesDownloaded = bytesDownloaded;
	if( pBytesTotal )
		*pBytesTotal = bytesTotal;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_DownloadItem( WorkshopItemId_t item, bool bHighPriority )
{
	if( !s_pSteamUGC )
		return false;
	return s_pSteamUGC->DownloadItem( item, bHighPriority );
}

//-----------------------------------------------------------------------------
// Purpose: Subscribing is asynchronous. The result is not tracked; callers
//			poll WarsWorkshopSteam_GetItemState instead.
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_SubscribeItem( WorkshopItemId_t item )
{
	if( !s_pSteamUGC )
		return false;
	return s_pSteamUGC->SubscribeItem( item ) != k_uAPICallInvalid;
}

//-----------------------------------------------------------------------------
// Purpose: Polls an asynchronous call. Returns false while still pending.
//-----------------------------------------------------------------------------
template< class T >
static bool GetCallResult( WorkshopAPICall_t call, T &data, bool &bIOFailure )
{
	bIOFailure = false;
	if( !s_pSteamUtils || call == k_uAPICallInvalid )
	{
		bIOFailure = true;
		return true;
	}

	bool bFailed = false;
	if( !s_pSteamUtils->IsAPICallCompleted( call, &bFailed ) )
		return false;

	if( bFailed || !s_pSteamUtils->GetAPICallResult( call, &data, sizeof( data ), T::k_iCallback, &bFailed ) || bFailed )
		bIOFailure = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
WorkshopAPICall_t WarsWorkshopSteam_CreateItem()
{
	if( !s_pSteamUGC )
		return k_uAPICallInvalid;
	return s_pSteamUGC->CreateItem( s_pSteamUtils->GetAppID(), k_EWorkshopFileTypeCommunity );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetCreateItemResult( WorkshopAPICall_t call, WorkshopCallResult_t &result )
{
	CreateItemResult_t data;
	memset( &data, 0, sizeof( data ) );
	if( !GetCallResult( call, data, result.bIOFailure ) )
		return false;

	result.result = result.bIOFailure ? k_EResultIOFailure : data.m_eResult;
	result.item = data.m_nPublishedFileId;
	result.bNeedsLegalAgreement = data.m_bUserNeedsToAcceptWorkshopLegalAgreement;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
WorkshopUpdateHandle_t WarsWorkshopSteam_StartItemUpdate( WorkshopItemId_t item )
{
	if( !s_pSteamUGC )
		return k_UGCUpdateHandleInvalid;
	return s_pSteamUGC->StartItemUpdate( s_pSteamUtils->GetAppID(), item );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_SetItemTitle( WorkshopUpdateHandle_t handle, const char *pszTitle )
{
	return s_pSteamUGC && s_pSteamUGC->SetItemTitle( handle, pszTitle );
}

bool WarsWorkshopSteam_SetItemDescription( WorkshopUpdateHandle_t handle, const char *pszDescription )
{
	return s_pSteamUGC && s_pSteamUGC->SetItemDescription( handle, pszDescription );
}

bool WarsWorkshopSteam_SetItemVisibility( WorkshopUpdateHandle_t handle, int visibility )
{
	return s_pSteamUGC && s_pSteamUGC->SetItemVisibility( handle, (ERemoteStoragePublishedFileVisibility)visibility );
}

bool WarsWorkshopSteam_SetItemTags( WorkshopUpdateHandle_t handle, const char **ppTags, int numTags )
{
	if( !s_pSteamUGC )
		return false;
	SteamParamStringArray_t tags;
	tags.m_ppStrings = ppTags;
	tags.m_nNumStrings = numTags;
	return s_pSteamUGC->SetItemTags( handle, &tags );
}

bool WarsWorkshopSteam_SetItemContent( WorkshopUpdateHandle_t handle, const char *pszFolder )
{
	return s_pSteamUGC && s_pSteamUGC->SetItemContent( handle, pszFolder );
}

bool WarsWorkshopSteam_SetItemPreview( WorkshopUpdateHandle_t handle, const char *pszPreviewFile )
{
	return s_pSteamUGC && s_pSteamUGC->SetItemPreview( handle, pszPreviewFile );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
WorkshopAPICall_t WarsWorkshopSteam_SubmitItemUpdate( WorkshopUpdateHandle_t handle, const char *pszChangeNote )
{
	if( !s_pSteamUGC )
		return k_uAPICallInvalid;
	return s_pSteamUGC->SubmitItemUpdate( handle, pszChangeNote );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int WarsWorkshopSteam_GetItemUpdateProgress( WorkshopUpdateHandle_t handle, unsigned long long *pBytesProcessed, unsigned long long *pBytesTotal )
{
	uint64 bytesProcessed = 0;
	uint64 bytesTotal = 0;
	int status = s_pSteamUGC ? s_pSteamUGC->GetItemUpdateProgress( handle, &bytesProcessed, &bytesTotal ) : k_EItemUpdateStatusInvalid;
	if( pBytesProcessed )
		*pBytesProcessed = bytesProcessed;
	if( pBytesTotal )
		*pBytesTotal = bytesTotal;
	return status;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetSubmitItemUpdateResult( WorkshopAPICall_t call, WorkshopCallResult_t &result )
{
	SubmitItemUpdateResult_t data;
	memset( &data, 0, sizeof( data ) );
	if( !GetCallResult( call, data, result.bIOFailure ) )
		return false;

	result.result = result.bIOFailure ? k_EResultIOFailure : data.m_eResult;
	result.item = data.m_nPublishedFileId;
	result.bNeedsLegalAgreement = data.m_bUserNeedsToAcceptWorkshopLegalAgreement;
	return true;
}
