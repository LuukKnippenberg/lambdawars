//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Plain wrapper around a current ISteamUGC (Steam Workshop) interface.
//			See wars_workshop_steam.cpp for why this is separate from the
//			regular steam api context. Only plain types cross this boundary,
//			so callers never include the newer Steamworks headers.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_STEAM_H
#define WARS_WORKSHOP_STEAM_H

#ifdef _WIN32
#pragma once
#endif

typedef unsigned long long WorkshopItemId_t;

// Mirrors EItemState from isteamugc.h
enum WorkshopItemState_t
{
	WORKSHOP_ITEM_NONE				= 0,
	WORKSHOP_ITEM_SUBSCRIBED		= 1,
	WORKSHOP_ITEM_LEGACY			= 2,
	WORKSHOP_ITEM_INSTALLED			= 4,
	WORKSHOP_ITEM_NEEDS_UPDATE		= 8,
	WORKSHOP_ITEM_DOWNLOADING		= 16,
	WORKSHOP_ITEM_DOWNLOAD_PENDING	= 32,
};

// Interface versions to request from the engine's ISteamClient (GetISteamGenericInterface)
const char *WarsWorkshopSteam_GetUGCInterfaceVersion();
const char *WarsWorkshopSteam_GetUtilsInterfaceVersion();
// Takes the ISteamUGC and ISteamUtils interfaces of those versions. Fails if either is NULL.
bool WarsWorkshopSteam_Init( void *pSteamUGC, void *pSteamUtils );
void WarsWorkshopSteam_Shutdown();
bool WarsWorkshopSteam_IsAvailable();
const char *WarsWorkshopSteam_GetError();

int WarsWorkshopSteam_GetNumSubscribedItems();
int WarsWorkshopSteam_GetSubscribedItems( WorkshopItemId_t *pItems, int maxItems );
unsigned int WarsWorkshopSteam_GetItemState( WorkshopItemId_t item );
bool WarsWorkshopSteam_GetItemInstallInfo( WorkshopItemId_t item, char *pszFolder, int folderSize, unsigned long long *pSizeOnDisk, unsigned int *pTimeStamp );
bool WarsWorkshopSteam_GetItemDownloadInfo( WorkshopItemId_t item, unsigned long long *pBytesDownloaded, unsigned long long *pBytesTotal );
bool WarsWorkshopSteam_DownloadItem( WorkshopItemId_t item, bool bHighPriority );
bool WarsWorkshopSteam_SubscribeItem( WorkshopItemId_t item );

// Publishing. Asynchronous Steam calls return a call handle (0 on failure),
// which is polled with the matching Get...Result function. Those return false
// while the call is still pending.
typedef unsigned long long WorkshopAPICall_t;
typedef unsigned long long WorkshopUpdateHandle_t;

#define WORKSHOP_API_CALL_INVALID 0ull
#define WORKSHOP_UPDATE_HANDLE_INVALID 0xffffffffffffffffull

// Mirrors ERemoteStoragePublishedFileVisibility
enum WorkshopVisibility_t
{
	WORKSHOP_VISIBILITY_PUBLIC		= 0,
	WORKSHOP_VISIBILITY_FRIENDSONLY	= 1,
	WORKSHOP_VISIBILITY_PRIVATE		= 2,
	WORKSHOP_VISIBILITY_UNLISTED	= 3,
};

// Mirrors EItemUpdateStatus
enum WorkshopUpdateStatus_t
{
	WORKSHOP_UPDATE_INVALID				= 0,
	WORKSHOP_UPDATE_PREPARING_CONFIG	= 1,
	WORKSHOP_UPDATE_PREPARING_CONTENT	= 2,
	WORKSHOP_UPDATE_UPLOADING_CONTENT	= 3,
	WORKSHOP_UPDATE_UPLOADING_PREVIEW	= 4,
	WORKSHOP_UPDATE_COMMITTING			= 5,
};

struct WorkshopCallResult_t
{
	bool bIOFailure;
	int result;		// EResult, 1 is OK
	WorkshopItemId_t item;
	bool bNeedsLegalAgreement;
};

WorkshopAPICall_t WarsWorkshopSteam_CreateItem();
bool WarsWorkshopSteam_GetCreateItemResult( WorkshopAPICall_t call, WorkshopCallResult_t &result );

WorkshopUpdateHandle_t WarsWorkshopSteam_StartItemUpdate( WorkshopItemId_t item );
bool WarsWorkshopSteam_SetItemTitle( WorkshopUpdateHandle_t handle, const char *pszTitle );
bool WarsWorkshopSteam_SetItemDescription( WorkshopUpdateHandle_t handle, const char *pszDescription );
bool WarsWorkshopSteam_SetItemVisibility( WorkshopUpdateHandle_t handle, int visibility );
bool WarsWorkshopSteam_SetItemTags( WorkshopUpdateHandle_t handle, const char **ppTags, int numTags );
bool WarsWorkshopSteam_SetItemContent( WorkshopUpdateHandle_t handle, const char *pszFolder );
bool WarsWorkshopSteam_SetItemPreview( WorkshopUpdateHandle_t handle, const char *pszPreviewFile );
WorkshopAPICall_t WarsWorkshopSteam_SubmitItemUpdate( WorkshopUpdateHandle_t handle, const char *pszChangeNote );
int WarsWorkshopSteam_GetItemUpdateProgress( WorkshopUpdateHandle_t handle, unsigned long long *pBytesProcessed, unsigned long long *pBytesTotal );
bool WarsWorkshopSteam_GetSubmitItemUpdateResult( WorkshopAPICall_t call, WorkshopCallResult_t &result );

#endif // WARS_WORKSHOP_STEAM_H
