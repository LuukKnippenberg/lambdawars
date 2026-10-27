//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Access to a current ISteamUGC (Steam Workshop) interface.
//
// The engine uses a 2010-era steam_api.dll whose headers have no usable
// ISteamUGC, and we cannot replace it because the engine is built against it.
// Its ISteamClient can still return any interface version the Steam client
// supports (ISteamClient::GetISteamGenericInterface), so wars_workshop.cpp
// requests the current ISteamUGC and ISteamUtils through it. All communication
// with Steam goes through the engine's steam api, as the Steamworks SDK Access
// Agreement requires (no direct access to steamclient.dll), and the engine's
// steam_api.dll keeps dispatching the callbacks.
//
// This file only uses those interfaces, so it is built against the headers of
// Steamworks SDK 1.65 in public/steamworks_ugc. It must not include cbase.h or
// any of the old steam headers. The new headers are wrapped in their own
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
#include "steamworks_ugc/isteamugc.h"
#include "steamworks_ugc/isteamutils.h"
}

#include "wars_workshop_steam.h"

using namespace WarsSteamworks;

static ISteamUGC *s_pSteamUGC = NULL;
// Used to poll the results of asynchronous calls instead of registering call results
static ISteamUtils *s_pSteamUtils = NULL;
static char s_szError[256] = "Not initialized";

static void SetError( const char *pError )
{
	_snprintf_s( s_szError, sizeof( s_szError ), _TRUNCATE, "%s", pError );
}

//-----------------------------------------------------------------------------
// Purpose: Interface versions of the headers this file is built against
//-----------------------------------------------------------------------------
const char *WarsWorkshopSteam_GetUGCInterfaceVersion()
{
	return STEAMUGC_INTERFACE_VERSION;
}

const char *WarsWorkshopSteam_GetUtilsInterfaceVersion()
{
	return STEAMUTILS_INTERFACE_VERSION;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_Init( void *pSteamUGC, void *pSteamUtils )
{
	if( s_pSteamUGC )
		return true;

	if( !pSteamUGC || !pSteamUtils )
	{
		char szError[256];
		_snprintf_s( szError, sizeof( szError ), _TRUNCATE, "Steam does not provide %s or %s (is Steam running and up to date?)",
			STEAMUGC_INTERFACE_VERSION, STEAMUTILS_INTERFACE_VERSION );
		SetError( szError );
		return false;
	}

	s_pSteamUGC = (ISteamUGC *)pSteamUGC;
	s_pSteamUtils = (ISteamUtils *)pSteamUtils;
	SetError( "" );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The interfaces belong to the engine's Steam connection, which the
//			engine shuts down itself.
//-----------------------------------------------------------------------------
void WarsWorkshopSteam_Shutdown()
{
	s_pSteamUGC = NULL;
	s_pSteamUtils = NULL;
	SetError( "Shut down" );
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

//-----------------------------------------------------------------------------
// Purpose: Unsubscribing is asynchronous, the result is not tracked. The
//			content is removed by Steam after the game quits.
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_UnsubscribeItem( WorkshopItemId_t item )
{
	if( !s_pSteamUGC )
		return false;
	return s_pSteamUGC->UnsubscribeItem( item ) != k_uAPICallInvalid;
}

//-----------------------------------------------------------------------------
// Purpose: Applies the settings shared by all queries
//-----------------------------------------------------------------------------
static WorkshopQueryHandle_t InitQuery( UGCQueryHandle_t handle )
{
	if( handle == k_UGCQueryHandleInvalid )
		return WORKSHOP_QUERY_HANDLE_INVALID;

	// Full descriptions are shown in the item details
	s_pSteamUGC->SetReturnLongDescription( handle, true );
	// Allows switching back and forth between pages without new requests
	s_pSteamUGC->SetAllowCachedResponse( handle, 60 );
	return handle;
}

//-----------------------------------------------------------------------------
// Purpose: Query over all items of this game. Page numbers start at 1.
//-----------------------------------------------------------------------------
WorkshopQueryHandle_t WarsWorkshopSteam_CreateQueryAll( int queryType, unsigned int page )
{
	if( !s_pSteamUGC )
		return WORKSHOP_QUERY_HANDLE_INVALID;

	AppId_t appID = s_pSteamUtils->GetAppID();
	return InitQuery( s_pSteamUGC->CreateQueryAllUGCRequest( (EUGCQuery)queryType, k_EUGCMatchingUGCType_Items, appID, appID, page ) );
}

//-----------------------------------------------------------------------------
// Purpose: Query over a list of a user, like their subscribed or published items
//-----------------------------------------------------------------------------
WorkshopQueryHandle_t WarsWorkshopSteam_CreateQueryUser( unsigned int accountID, int list, int sortOrder, unsigned int page )
{
	if( !s_pSteamUGC )
		return WORKSHOP_QUERY_HANDLE_INVALID;

	AppId_t appID = s_pSteamUtils->GetAppID();
	return InitQuery( s_pSteamUGC->CreateQueryUserUGCRequest( accountID, (EUserUGCList)list, k_EUGCMatchingUGCType_Items,
		(EUserUGCListSortOrder)sortOrder, appID, appID, page ) );
}

bool WarsWorkshopSteam_SetQuerySearchText( WorkshopQueryHandle_t handle, const char *pszText )
{
	return s_pSteamUGC && s_pSteamUGC->SetSearchText( handle, pszText );
}

bool WarsWorkshopSteam_AddQueryRequiredTag( WorkshopQueryHandle_t handle, const char *pszTag )
{
	return s_pSteamUGC && s_pSteamUGC->AddRequiredTag( handle, pszTag );
}

bool WarsWorkshopSteam_SetQueryRankedByTrendDays( WorkshopQueryHandle_t handle, unsigned int days )
{
	return s_pSteamUGC && s_pSteamUGC->SetRankedByTrendDays( handle, days );
}

WorkshopAPICall_t WarsWorkshopSteam_SendQuery( WorkshopQueryHandle_t handle )
{
	if( !s_pSteamUGC )
		return k_uAPICallInvalid;
	return s_pSteamUGC->SendQueryUGCRequest( handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetQueryCompleted( WorkshopAPICall_t call, WorkshopQueryResult_t &result )
{
	SteamUGCQueryCompleted_t data;
	memset( &data, 0, sizeof( data ) );
	if( !GetCallResult( call, data, result.bIOFailure ) )
		return false;

	result.result = result.bIOFailure ? k_EResultIOFailure : data.m_eResult;
	result.numResults = data.m_unNumResultsReturned;
	result.totalResults = data.m_unTotalMatchingResults;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_GetQueryItem( WorkshopQueryHandle_t handle, unsigned int index, WorkshopItemDetails_t &details )
{
	if( !s_pSteamUGC )
		return false;

	// Large structure (full description), so not on the stack
	SteamUGCDetails_t *pData = new SteamUGCDetails_t;
	bool bSuccess = s_pSteamUGC->GetQueryUGCResult( handle, index, pData ) && pData->m_eResult == k_EResultOK;
	if( bSuccess )
	{
		memset( &details, 0, sizeof( details ) );
		details.item = pData->m_nPublishedFileId;
		strncpy_s( details.szTitle, sizeof( details.szTitle ), pData->m_rgchTitle, _TRUNCATE );
		strncpy_s( details.szDescription, sizeof( details.szDescription ), pData->m_rgchDescription, _TRUNCATE );
		strncpy_s( details.szTags, sizeof( details.szTags ), pData->m_rgchTags, _TRUNCATE );
		details.ownerSteamID = pData->m_ulSteamIDOwner;
		details.timeCreated = pData->m_rtimeCreated;
		details.timeUpdated = pData->m_rtimeUpdated;
		details.votesUp = pData->m_unVotesUp;
		details.votesDown = pData->m_unVotesDown;
		details.score = pData->m_flScore;
		details.fileSize = pData->m_ulTotalFilesSize ? pData->m_ulTotalFilesSize : (unsigned long long)pData->m_nFileSize;
		details.visibility = pData->m_eVisibility;
		details.bBanned = pData->m_bBanned;

		if( !s_pSteamUGC->GetQueryUGCPreviewURL( handle, index, details.szPreviewURL, sizeof( details.szPreviewURL ) ) )
			details.szPreviewURL[0] = '\0';

		uint64 subscriptions = 0;
		if( s_pSteamUGC->GetQueryUGCStatistic( handle, index, k_EItemStatistic_NumUniqueSubscriptions, &subscriptions ) )
			details.subscriptions = subscriptions;
	}
	delete pData;
	return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void WarsWorkshopSteam_ReleaseQuery( WorkshopQueryHandle_t handle )
{
	if( s_pSteamUGC && handle != WORKSHOP_QUERY_HANDLE_INVALID )
		s_pSteamUGC->ReleaseQueryUGCRequest( handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
unsigned int WarsWorkshopSteam_GetAppID()
{
	return s_pSteamUtils ? s_pSteamUtils->GetAppID() : 0;
}

//-----------------------------------------------------------------------------
// Purpose: Game servers have no subscriptions, items are downloaded on request
//			into the given folder.
//-----------------------------------------------------------------------------
bool WarsWorkshopSteam_InitWorkshopForGameServer( unsigned int workshopDepotID, const char *pszFolder )
{
	if( !s_pSteamUGC )
		return false;
	return s_pSteamUGC->BInitWorkshopForGameServer( workshopDepotID, pszFolder );
}
