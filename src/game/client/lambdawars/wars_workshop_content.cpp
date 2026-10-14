//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Validates the content of Steam Workshop items before mounting.
//
// Workshop items are added to the GAME search path. They can't override files
// of the game (they are added at the tail), but they could add files the game
// looks for, like python/autorun_once/*.py or cfg/listenserver.cfg, and run
// code that way. Only map content is allowed: a fixed set of folders and file
// types. The same applies to files embedded in a map's pakfile lump, which is
// mounted at the head of the search path while the map is loaded.
//
//=============================================================================//

#include "cbase.h"
#include "wars_workshop_content.h"
#include "bspfile.h"
#include "filesystem.h"
#include <io.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

struct WorkshopContentRule_t
{
	const char *pszFolder;
	bool bSubFolders;
	const char *pszExtensions; // ";ext;ext;"
};

static const WorkshopContentRule_t s_ContentRules[] =
{
	{ "maps",		false,	";bsp;res;recast;wars;layout;bin;" },
	{ "materials",	true,	";vmt;vtf;" },
	{ "models",		true,	";mdl;vvd;vtx;phy;ani;" },
	{ "sound",		true,	";wav;mp3;" },
};

// Files written to the root of the pakfile by the map compile tools
static const char *s_pszAllowedPakRootFiles[] =
{
	"stale.txt",
	"stringtable_dictionary.dct",
};
#define PAK_ROOT_EXTENSIONS ";vhv;" // static prop lighting

// Limits to keep validation of malformed or huge items bounded
#define MAX_ITEM_FILES 20000
#define MAX_FOLDER_DEPTH 16
#define MAX_ZIP_COMMENT 0xFFFF
#define MAX_CENTRAL_DIR_SIZE ( 64 * 1024 * 1024 )

#define ZIP_END_OF_CENTRAL_DIR_SIG 0x06054b50
#define ZIP_CENTRAL_DIR_FILE_SIG 0x02014b50
#define ZIP_END_OF_CENTRAL_DIR_SIZE 22
#define ZIP_CENTRAL_DIR_FILE_SIZE 46

static void SetReason( char *pszReason, int reasonSize, const char *pszFormat, const char *pszArg )
{
	if( pszReason && reasonSize > 0 )
		V_snprintf( pszReason, reasonSize, pszFormat, pszArg );
}

//-----------------------------------------------------------------------------
// Purpose: Tests a relative path ("maps/mymap.bsp") against the allowed content
//-----------------------------------------------------------------------------
static bool IsAllowedContentPath( const char *pszRelativePath, bool bInPakfile )
{
	char szPath[MAX_PATH];
	V_strncpy( szPath, pszRelativePath, sizeof( szPath ) );
	V_FixSlashes( szPath, '/' );
	V_strlower( szPath );

	if( !szPath[0] || szPath[0] == '/' || V_strstr( szPath, ":" ) || V_strstr( szPath, ".." ) )
		return false;

	char szExtension[32];
	V_ExtractFileExtension( szPath, szExtension, sizeof( szExtension ) );
	char szExtensionToken[40];
	V_snprintf( szExtensionToken, sizeof( szExtensionToken ), ";%s;", szExtension );

	const char *pszFirstSlash = V_strstr( szPath, "/" );
	if( !pszFirstSlash )
	{
		if( !bInPakfile )
			return false;
		for( int i = 0; i < ARRAYSIZE( s_pszAllowedPakRootFiles ); i++ )
		{
			if( !V_strcmp( szPath, s_pszAllowedPakRootFiles[i] ) )
				return true;
		}
		return szExtension[0] && V_strstr( PAK_ROOT_EXTENSIONS, szExtensionToken ) != NULL;
	}

	int folderLength = pszFirstSlash - szPath;
	for( int i = 0; i < ARRAYSIZE( s_ContentRules ); i++ )
	{
		const WorkshopContentRule_t &rule = s_ContentRules[i];
		if( V_strlen( rule.pszFolder ) != folderLength || V_strncmp( szPath, rule.pszFolder, folderLength ) )
			continue;
		if( !rule.bSubFolders && V_strstr( pszFirstSlash + 1, "/" ) )
			return false;
		return szExtension[0] && V_strstr( rule.pszExtensions, szExtensionToken ) != NULL;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static bool ValidateFolderRecursive( const char *pszRoot, const char *pszRelativeFolder, int depth, int &numFiles, char *pszReason, int reasonSize )
{
	if( depth > MAX_FOLDER_DEPTH )
	{
		SetReason( pszReason, reasonSize, "folders nested too deep: %s", pszRelativeFolder );
		return false;
	}

	char szWildcard[MAX_PATH];
	if( pszRelativeFolder[0] )
		V_snprintf( szWildcard, sizeof( szWildcard ), "%s\\%s\\*", pszRoot, pszRelativeFolder );
	else
		V_snprintf( szWildcard, sizeof( szWildcard ), "%s\\*", pszRoot );

	// Even an empty folder lists "." and "..", so failing means it is missing or unreadable
	_finddata_t findData;
	intptr_t hFind = _findfirst( szWildcard, &findData );
	if( hFind == -1 )
	{
		SetReason( pszReason, reasonSize, "could not read folder %s", pszRelativeFolder[0] ? pszRelativeFolder : pszRoot );
		return false;
	}

	bool bValid = true;
	do
	{
		if( !V_strcmp( findData.name, "." ) || !V_strcmp( findData.name, ".." ) )
			continue;

		char szRelativePath[MAX_PATH];
		if( pszRelativeFolder[0] )
			V_snprintf( szRelativePath, sizeof( szRelativePath ), "%s/%s", pszRelativeFolder, findData.name );
		else
			V_strncpy( szRelativePath, findData.name, sizeof( szRelativePath ) );

		if( findData.attrib & _A_SUBDIR )
		{
			bValid = ValidateFolderRecursive( pszRoot, szRelativePath, depth + 1, numFiles, pszReason, reasonSize );
			continue;
		}

		if( ++numFiles > MAX_ITEM_FILES )
		{
			SetReason( pszReason, reasonSize, "too many files (%s)", "limit reached" );
			bValid = false;
			break;
		}

		if( !IsAllowedContentPath( szRelativePath, false ) )
		{
			SetReason( pszReason, reasonSize, "file type or location not allowed: %s", szRelativePath );
			bValid = false;
			break;
		}

		if( !V_stricmp( V_GetFileExtension( findData.name ) ? V_GetFileExtension( findData.name ) : "", "bsp" ) )
		{
			char szFullPath[MAX_PATH];
			V_snprintf( szFullPath, sizeof( szFullPath ), "%s\\%s", pszRoot, szRelativePath );
			V_FixSlashes( szFullPath );
			bValid = WarsWorkshop_ValidateMapFile( szFullPath, pszReason, reasonSize );
		}
	}
	while( bValid && _findnext( hFind, &findData ) == 0 );

	_findclose( hFind );
	return bValid;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshop_ValidateItemFolder( const char *pszFolder, char *pszReason, int reasonSize )
{
	SetReason( pszReason, reasonSize, "%s", "" );
	int numFiles = 0;
	return ValidateFolderRecursive( pszFolder, "", 0, numFiles, pszReason, reasonSize );
}

//-----------------------------------------------------------------------------
// Purpose: Reads the central directory of the zip in the pakfile lump and
//			validates the name of every embedded file.
//-----------------------------------------------------------------------------
static bool ValidatePakfile( FileHandle_t fh, int lumpOffset, int lumpLength, const char *pszMapName, char *pszReason, int reasonSize )
{
	if( lumpLength < ZIP_END_OF_CENTRAL_DIR_SIZE )
		return true; // No embedded files

	// Find the end of central directory record, which can be followed by a comment
	int tailLength = MIN( lumpLength, ZIP_END_OF_CENTRAL_DIR_SIZE + MAX_ZIP_COMMENT );
	CUtlBuffer tail;
	tail.EnsureCapacity( tailLength );
	filesystem->Seek( fh, lumpOffset + lumpLength - tailLength, FILESYSTEM_SEEK_HEAD );
	if( filesystem->Read( tail.Base(), tailLength, fh ) != tailLength )
	{
		SetReason( pszReason, reasonSize, "could not read the embedded files of %s", pszMapName );
		return false;
	}
	const unsigned char *pTail = (const unsigned char *)tail.Base();
	int eocd = -1;
	for( int i = tailLength - ZIP_END_OF_CENTRAL_DIR_SIZE; i >= 0; i-- )
	{
		if( *(const unsigned int *)( pTail + i ) == ZIP_END_OF_CENTRAL_DIR_SIG )
		{
			eocd = i;
			break;
		}
	}
	if( eocd == -1 )
	{
		SetReason( pszReason, reasonSize, "invalid embedded files in %s", pszMapName );
		return false;
	}

	int numEntries = *(const unsigned short *)( pTail + eocd + 10 );
	unsigned int centralDirSize = *(const unsigned int *)( pTail + eocd + 12 );
	unsigned int centralDirOffset = *(const unsigned int *)( pTail + eocd + 16 );
	if( centralDirOffset > (unsigned int)lumpLength || centralDirSize > (unsigned int)lumpLength - centralDirOffset ||
		centralDirSize > MAX_CENTRAL_DIR_SIZE )
	{
		SetReason( pszReason, reasonSize, "invalid embedded files in %s", pszMapName );
		return false;
	}

	CUtlBuffer centralDir;
	centralDir.EnsureCapacity( centralDirSize + 1 );
	filesystem->Seek( fh, lumpOffset + centralDirOffset, FILESYSTEM_SEEK_HEAD );
	if( filesystem->Read( centralDir.Base(), centralDirSize, fh ) != (int)centralDirSize )
	{
		SetReason( pszReason, reasonSize, "could not read the embedded files of %s", pszMapName );
		return false;
	}

	const unsigned char *pDir = (const unsigned char *)centralDir.Base();
	unsigned int pos = 0;
	for( int i = 0; i < numEntries; i++ )
	{
		if( pos + ZIP_CENTRAL_DIR_FILE_SIZE > centralDirSize || *(const unsigned int *)( pDir + pos ) != ZIP_CENTRAL_DIR_FILE_SIG )
		{
			SetReason( pszReason, reasonSize, "invalid embedded files in %s", pszMapName );
			return false;
		}
		unsigned int nameLength = *(const unsigned short *)( pDir + pos + 28 );
		unsigned int extraLength = *(const unsigned short *)( pDir + pos + 30 );
		unsigned int commentLength = *(const unsigned short *)( pDir + pos + 32 );
		if( pos + ZIP_CENTRAL_DIR_FILE_SIZE + nameLength > centralDirSize || nameLength >= MAX_PATH )
		{
			SetReason( pszReason, reasonSize, "invalid embedded files in %s", pszMapName );
			return false;
		}

		char szName[MAX_PATH];
		V_memcpy( szName, pDir + pos + ZIP_CENTRAL_DIR_FILE_SIZE, nameLength );
		szName[nameLength] = '\0';

		// Directory entries end with a slash and contain no data
		bool bDirectory = nameLength > 0 && ( szName[nameLength - 1] == '/' || szName[nameLength - 1] == '\\' );
		if( !bDirectory && !IsAllowedContentPath( szName, true ) )
		{
			char szFile[MAX_PATH * 2];
			V_snprintf( szFile, sizeof( szFile ), "%s (in %s)", szName, pszMapName );
			SetReason( pszReason, reasonSize, "embedded file type or location not allowed: %s", szFile );
			return false;
		}

		pos += ZIP_CENTRAL_DIR_FILE_SIZE + nameLength + extraLength + commentLength;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsWorkshop_ValidateMapFile( const char *pszBspPath, char *pszReason, int reasonSize )
{
	const char *pszMapName = V_UnqualifiedFileName( pszBspPath );

	FileHandle_t fh = filesystem->Open( pszBspPath, "rb" );
	if( fh == FILESYSTEM_INVALID_HANDLE )
	{
		SetReason( pszReason, reasonSize, "could not open %s", pszMapName );
		return false;
	}

	BSPHeader_t header;
	bool bValid = filesystem->Read( &header, sizeof( header ), fh ) == sizeof( header ) && header.ident == IDBSPHEADER;
	if( !bValid )
	{
		SetReason( pszReason, reasonSize, "%s is not a valid map", pszMapName );
	}
	else
	{
		int fileSize = (int)filesystem->Size( fh );
		const lump_t &pakLump = header.lumps[LUMP_PAKFILE];
		if( pakLump.fileofs < 0 || pakLump.filelen < 0 || pakLump.fileofs > fileSize || pakLump.filelen > fileSize - pakLump.fileofs )
		{
			SetReason( pszReason, reasonSize, "%s is not a valid map", pszMapName );
			bValid = false;
		}
		else
		{
			bValid = ValidatePakfile( fh, pakLump.fileofs, pakLump.filelen, pszMapName, pszReason, reasonSize );
		}
	}

	filesystem->Close( fh );
	return bValid;
}
