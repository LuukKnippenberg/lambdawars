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

bool WarsWorkshopSteam_UnsubscribeItem( WorkshopItemId_t item );

// Browsing. A query returns one page of at most WORKSHOP_RESULTS_PER_PAGE items.
// Create a query, configure it, send it and poll the call with GetQueryCompleted.
// The query handle must always be released.
typedef unsigned long long WorkshopQueryHandle_t;

#define WORKSHOP_RESULTS_PER_PAGE 50
#define WORKSHOP_QUERY_HANDLE_INVALID 0xffffffffffffffffull

// Mirrors EUGCQuery (subset)
enum WorkshopQueryType_t
{
	WORKSHOP_QUERY_RANKED_BY_VOTE					= 0,
	WORKSHOP_QUERY_RANKED_BY_PUBLICATION_DATE		= 1,
	WORKSHOP_QUERY_RANKED_BY_TREND					= 3,
	WORKSHOP_QUERY_RANKED_BY_TEXT_SEARCH			= 11,
	WORKSHOP_QUERY_RANKED_BY_UNIQUE_SUBSCRIPTIONS	= 12,
	WORKSHOP_QUERY_RANKED_BY_LAST_UPDATED_DATE		= 19,
};

// Mirrors EUserUGCList (subset)
enum WorkshopUserList_t
{
	WORKSHOP_USER_LIST_PUBLISHED	= 0,
	WORKSHOP_USER_LIST_SUBSCRIBED	= 6,
};

// Mirrors EUserUGCListSortOrder (subset)
enum WorkshopUserListSortOrder_t
{
	WORKSHOP_USER_SORT_CREATION_DESC		= 0,
	WORKSHOP_USER_SORT_TITLE_ASC			= 2,
	WORKSHOP_USER_SORT_LAST_UPDATED_DESC	= 3,
	WORKSHOP_USER_SORT_SUBSCRIPTION_DESC	= 4,
	WORKSHOP_USER_SORT_VOTE_SCORE_DESC		= 5,
};

struct WorkshopQueryResult_t
{
	bool bIOFailure;
	int result;		// EResult, 1 is OK
	unsigned int numResults;
	unsigned int totalResults;
};

struct WorkshopItemDetails_t
{
	WorkshopItemId_t item;
	char szTitle[129];
	char szDescription[8000];
	char szTags[1025];		// Comma separated
	char szPreviewURL[256];
	unsigned long long ownerSteamID;
	unsigned int timeCreated;
	unsigned int timeUpdated;
	unsigned int votesUp;
	unsigned int votesDown;
	float score;
	unsigned long long fileSize;
	unsigned long long subscriptions;
	int visibility;
	bool bBanned;
};

WorkshopQueryHandle_t WarsWorkshopSteam_CreateQueryAll( int queryType, unsigned int page );
WorkshopQueryHandle_t WarsWorkshopSteam_CreateQueryUser( unsigned int accountID, int list, int sortOrder, unsigned int page );
bool WarsWorkshopSteam_SetQuerySearchText( WorkshopQueryHandle_t handle, const char *pszText );
bool WarsWorkshopSteam_AddQueryRequiredTag( WorkshopQueryHandle_t handle, const char *pszTag );
bool WarsWorkshopSteam_SetQueryRankedByTrendDays( WorkshopQueryHandle_t handle, unsigned int days );
WorkshopAPICall_t WarsWorkshopSteam_SendQuery( WorkshopQueryHandle_t handle );
bool WarsWorkshopSteam_GetQueryCompleted( WorkshopAPICall_t call, WorkshopQueryResult_t &result );
bool WarsWorkshopSteam_GetQueryItem( WorkshopQueryHandle_t handle, unsigned int index, WorkshopItemDetails_t &details );
void WarsWorkshopSteam_ReleaseQuery( WorkshopQueryHandle_t handle );

#endif // WARS_WORKSHOP_STEAM_H
