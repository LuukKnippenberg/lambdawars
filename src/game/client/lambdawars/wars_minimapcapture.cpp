//=============================================================================//
//
// Purpose: Captures the minimap (overview) of the current map. The camera is
//			placed high above the center of the playable area, looking down,
//			and the rendered world is written as a vtf, before the HUD is drawn.
//
//			A perspective camera with a narrow field of view is used instead of
//			the orthographic view of cl_leveloverview, because the deferred
//			lighting reconstructs positions with perspective view rays. The res
//			values are exact at the height of the ground below the camera.
//
//=============================================================================//

#include "cbase.h"
#include "wars_minimapcapture.h"
#include "wars_mapboundary.h"
#include "c_world.h"
#include "view_shared.h"
#include "materialsystem/imaterialsystem.h"
#include "vtf/vtf.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern IFileSystem *filesystem;

// Vertical field of view of the camera. Narrow, so tall objects barely lean outwards.
static const float MINIMAP_CAPTURE_FOV = 20.0f;
// Frames rendered from above before capturing, so level of detail and fading settle
static const int MINIMAP_CAPTURE_SETTLE_FRAMES = 10;
// Number of traces per side used to find the height of the ground
static const int MINIMAP_CAPTURE_GROUND_SAMPLES = 7;
// Empty parts of the minimap (no geometry below) are found with a trace per this many pixels per side
static const int MINIMAP_CAPTURE_EMPTY_STEP = 2;
// Empty parts get the hatch pattern of the menu: diagonal stripes, sized for a minimap of 512 pixels
static const int MINIMAP_CAPTURE_HATCH_PERIOD = 17;
static const int MINIMAP_CAPTURE_HATCH_STRIPE = 9;
static const unsigned char MINIMAP_CAPTURE_HATCH_DARK[3] = { 28, 26, 23 };
static const unsigned char MINIMAP_CAPTURE_HATCH_LIGHT[3] = { 36, 34, 32 };

struct MinimapCapture_t
{
	bool bActive;
	int iFrames;
	int iResolution;
	char path[MAX_PATH];

	Vector vecCenter;
	float flSize;
	Vector vecCamera;
	float flTanHalfFov;
	float flZNear;
	float flZFar;
	// Height range of the world
	float flTop;
	float flBottom;
	// Aspect ratio of the view, set when the view is set up
	float flAspect;
};

static MinimapCapture_t s_Capture;
static WarsMinimapCaptureResult_t s_Result;

static void SetResult( bool bSuccess, const char *pszError )
{
	s_Result.bDone = true;
	s_Result.bSuccess = bSuccess;
	V_strncpy( s_Result.error, pszError ? pszError : "", sizeof( s_Result.error ) );
}

//-----------------------------------------------------------------------------
// Purpose: The playable area: the map boundaries, or the world if the map has none
//-----------------------------------------------------------------------------
static bool GetPlayableArea( Vector &mins, Vector &maxs )
{
	bool bFound = false;
	for( CBaseFuncMapBoundary *pBoundary = GetMapBoundaryList(); pBoundary; pBoundary = pBoundary->GetNext() )
	{
		Vector boundaryMins, boundaryMaxs;
		pBoundary->CollisionProp()->WorldSpaceAABB( &boundaryMins, &boundaryMaxs );
		if( !bFound )
		{
			mins = boundaryMins;
			maxs = boundaryMaxs;
			bFound = true;
		}
		else
		{
			VectorMin( mins, boundaryMins, mins );
			VectorMax( maxs, boundaryMaxs, maxs );
		}
	}
	if( bFound )
		return true;

	C_World *pWorld = GetClientWorldEntity();
	if( pWorld && pWorld->m_WorldMins != pWorld->m_WorldMaxs )
	{
		mins = pWorld->m_WorldMins;
		maxs = pWorld->m_WorldMaxs;
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Height of the ground in the playable area: the median of a grid of traces down
//-----------------------------------------------------------------------------
static int CompareFloats( const float *a, const float *b )
{
	return ( *a < *b ) ? -1 : ( *a > *b ) ? 1 : 0;
}

static float GetGroundHeight( const Vector &mins, const Vector &maxs )
{
	CUtlVector< float > heights;
	for( int i = 0; i < MINIMAP_CAPTURE_GROUND_SAMPLES; i++ )
	{
		for( int j = 0; j < MINIMAP_CAPTURE_GROUND_SAMPLES; j++ )
		{
			float fx = ( i + 0.5f ) / MINIMAP_CAPTURE_GROUND_SAMPLES;
			float fy = ( j + 0.5f ) / MINIMAP_CAPTURE_GROUND_SAMPLES;
			Vector start( Lerp( fx, mins.x, maxs.x ), Lerp( fy, mins.y, maxs.y ), maxs.z );
			Vector end( start.x, start.y, mins.z - 1.0f );

			trace_t tr;
			UTIL_TraceLine( start, end, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if( tr.fraction < 1.0f && !tr.startsolid )
				heights.AddToTail( tr.endpos.z );
		}
	}
	if( heights.Count() == 0 )
		return mins.z;
	heights.Sort( CompareFloats );
	return heights[ heights.Count() / 2 ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WarsMinimapCapture_Start( const char *pszPath, int iResolution )
{
	V_memset( &s_Result, 0, sizeof( s_Result ) );
	s_Capture.bActive = false;

	if( !engine->IsInGame() )
	{
		SetResult( false, "no map is loaded" );
		return false;
	}
	if( !IsPowerOfTwo( iResolution ) || iResolution < 64 || iResolution > 4096 )
	{
		SetResult( false, "invalid resolution" );
		return false;
	}

	Vector mins, maxs;
	if( !GetPlayableArea( mins, maxs ) )
	{
		SetResult( false, "the playable area of the map couldn't be found" );
		return false;
	}

	float flGround = GetGroundHeight( mins, maxs );
	float flSize = MAX( maxs.x - mins.x, maxs.y - mins.y );
	Vector vecCenter( ( mins.x + maxs.x ) * 0.5f, ( mins.y + maxs.y ) * 0.5f, flGround );

	// High enough that the square playable area fills the field of view, and above everything of the world
	C_World *pWorld = GetClientWorldEntity();
	float flTop = MAX( maxs.z, pWorld ? pWorld->m_WorldMaxs.z : maxs.z );
	float flBottom = MIN( mins.z, pWorld ? pWorld->m_WorldMins.z : mins.z );
	float flDistance = ( flSize * 0.5f ) / tanf( DEG2RAD( MINIMAP_CAPTURE_FOV * 0.5f ) );
	flDistance = MAX( flDistance, flTop - flGround + 256.0f );

	s_Capture.iFrames = 0;
	s_Capture.flAspect = 0.0f;
	s_Capture.iResolution = iResolution;
	V_strncpy( s_Capture.path, pszPath, sizeof( s_Capture.path ) );
	s_Capture.vecCenter = vecCenter;
	s_Capture.flSize = flSize;
	s_Capture.vecCamera = Vector( vecCenter.x, vecCenter.y, flGround + flDistance );
	s_Capture.flTanHalfFov = ( flSize * 0.5f ) / flDistance;
	s_Capture.flZNear = MAX( 8.0f, s_Capture.vecCamera.z - flTop - 64.0f );
	s_Capture.flZFar = s_Capture.vecCamera.z - flBottom + 256.0f;
	s_Capture.flTop = flTop;
	s_Capture.flBottom = flBottom;
	s_Capture.bActive = true;

	DevMsg( "Minimap capture: center %.0f %.0f, ground %.0f, size %.0f, camera height %.0f\n",
		vecCenter.x, vecCenter.y, flGround, flSize, s_Capture.vecCamera.z );
	return true;
}

void WarsMinimapCapture_Cancel()
{
	if( !s_Capture.bActive )
		return;
	s_Capture.bActive = false;
	SetResult( false, "cancelled" );
}

bool WarsMinimapCapture_IsActive()
{
	return s_Capture.bActive;
}

const WarsMinimapCaptureResult_t &WarsMinimapCapture_GetResult()
{
	return s_Result;
}

//-----------------------------------------------------------------------------
// Purpose: Looks straight down from above the center, with the top of the image to the north (+y)
//-----------------------------------------------------------------------------
bool WarsMinimapCapture_SetupView( CViewSetup &view )
{
	if( !s_Capture.bActive )
		return false;

	float flAspect = view.m_flAspectRatio > 0.0f ? view.m_flAspectRatio : (float)view.width / (float)view.height;
	s_Capture.flAspect = flAspect;

	view.origin = s_Capture.vecCamera;
	view.angles.Init( 90.0f, 90.0f, 0.0f );
	view.m_bOrtho = false;
	// The fov of the view is horizontal
	view.fov = RAD2DEG( 2.0f * atanf( s_Capture.flTanHalfFov * flAspect ) );
	view.zNear = s_Capture.flZNear;
	view.zFar = s_Capture.flZFar;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The dark color of the hatch pattern, so what is left of it along the edges of empty parts
//			blends in with the pattern
//-----------------------------------------------------------------------------
bool WarsMinimapCapture_SetClearColor()
{
	if( !s_Capture.bActive )
		return false;
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearColor4ub( MINIMAP_CAPTURE_HATCH_DARK[0], MINIMAP_CAPTURE_HATCH_DARK[1], MINIMAP_CAPTURE_HATCH_DARK[2], 255 );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Whether geometry is seen through a pixel of the minimap (0 to 1, from the top left).
//			Traces from above the world along the view ray of the camera. Sky and nodraw surfaces,
//			like the top of the skybox, are passed through.
//-----------------------------------------------------------------------------
static bool HasGeometryBelow( float u, float v )
{
	Vector vecGround( s_Capture.vecCenter.x + ( u - 0.5f ) * s_Capture.flSize,
		s_Capture.vecCenter.y + ( 0.5f - v ) * s_Capture.flSize, s_Capture.vecCenter.z );
	Vector vecDir = vecGround - s_Capture.vecCamera;
	float flDown = -vecDir.z;
	if( flDown <= 0.0f )
		return true;

	Vector vecStart = s_Capture.vecCamera + vecDir * ( ( s_Capture.vecCamera.z - s_Capture.flTop - 16.0f ) / flDown );
	Vector vecEnd = s_Capture.vecCamera + vecDir * ( ( s_Capture.vecCamera.z - s_Capture.flBottom + 16.0f ) / flDown );
	Vector vecStep = vecDir;
	VectorNormalize( vecStep );

	CTraceFilterWorldOnly filter;
	for( int i = 0; i < 8; i++ )
	{
		trace_t tr;
		UTIL_TraceLine( vecStart, vecEnd, MASK_SOLID_BRUSHONLY, &filter, &tr );
		if( tr.allsolid )
			return true;
		if( tr.fraction >= 1.0f )
			return false;
		if( !( tr.surface.flags & ( SURF_SKY | SURF_SKY2D | SURF_NODRAW ) ) )
			return true;
		vecStart = tr.endpos + vecStep;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Fills the parts without geometry with the hatch pattern of the menu, instead of whatever
//			the renderer leaves there. Pixels next to an empty part get the pattern too, instead of
//			blending, because a blend shows the color the renderer left as a line along the edge.
//-----------------------------------------------------------------------------
static void FillEmptyParts( unsigned char *pPixels, int iResolution )
{
	int iGrid = iResolution / MINIMAP_CAPTURE_EMPTY_STEP;
	CUtlVector< float > geometry;
	geometry.SetCount( iGrid * iGrid );
	bool bAnyEmpty = false;
	for( int y = 0; y < iGrid; y++ )
	{
		for( int x = 0; x < iGrid; x++ )
		{
			bool bGeometry = HasGeometryBelow( ( x + 0.5f ) / iGrid, ( y + 0.5f ) / iGrid );
			geometry[ y * iGrid + x ] = bGeometry ? 1.0f : 0.0f;
			bAnyEmpty |= !bGeometry;
		}
	}
	if( !bAnyEmpty )
		return;

	float flHatchScale = iResolution / 512.0f;
	for( int y = 0; y < iResolution; y++ )
	{
		// Bilinear sample of the grid, at the center of the pixel
		float gy = clamp( ( y + 0.5f ) / MINIMAP_CAPTURE_EMPTY_STEP - 0.5f, 0.0f, iGrid - 1.0f );
		int y0 = (int)gy;
		int y1 = MIN( y0 + 1, iGrid - 1 );
		float fy = gy - y0;
		for( int x = 0; x < iResolution; x++ )
		{
			float gx = clamp( ( x + 0.5f ) / MINIMAP_CAPTURE_EMPTY_STEP - 0.5f, 0.0f, iGrid - 1.0f );
			int x0 = (int)gx;
			int x1 = MIN( x0 + 1, iGrid - 1 );
			float fx = gx - x0;
			float flGeometry = Lerp( fy, Lerp( fx, geometry[ y0 * iGrid + x0 ], geometry[ y0 * iGrid + x1 ] ),
				Lerp( fx, geometry[ y1 * iGrid + x0 ], geometry[ y1 * iGrid + x1 ] ) );
			if( flGeometry >= 1.0f )
				continue;

			// Any part empty is the pattern

			int iHatch = (int)( ( x + y ) / flHatchScale ) % MINIMAP_CAPTURE_HATCH_PERIOD;
			const unsigned char *pHatch = iHatch < MINIMAP_CAPTURE_HATCH_STRIPE ? MINIMAP_CAPTURE_HATCH_LIGHT : MINIMAP_CAPTURE_HATCH_DARK;
			unsigned char *pPixel = pPixels + ( y * iResolution + x ) * 4;
			for( int c = 0; c < 3; c++ )
				pPixel[c] = pHatch[c];
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The height of the view shows the playable area, so the middle part of the
//			view with the same size in world units is the minimap
//-----------------------------------------------------------------------------
static bool WriteMinimap( int x, int y, int width, int height, const char **ppszError )
{
	float flAspect = s_Capture.flAspect > 0.0f ? s_Capture.flAspect : (float)width / (float)height;
	int iSourceWidth = MIN( width, (int)( width / flAspect + 0.5f ) );
	if( iSourceWidth <= 0 || height <= 0 )
	{
		*ppszError = "invalid screen size";
		return false;
	}

	int iResolution = s_Capture.iResolution;
	CUtlMemory< unsigned char > pixels;
	pixels.EnsureCapacity( iResolution * iResolution * 4 );

	// Read and scale on the graphics card
	Rect_t srcRect;
	srcRect.x = x + ( width - iSourceWidth ) / 2;
	srcRect.y = y;
	srcRect.width = iSourceWidth;
	srcRect.height = height;
	Rect_t dstRect;
	dstRect.x = 0;
	dstRect.y = 0;
	dstRect.width = iResolution;
	dstRect.height = iResolution;
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->ReadPixelsAndStretch( &srcRect, &dstRect, pixels.Base(), IMAGE_FORMAT_RGBA8888, iResolution * 4 );
	}
	for( int i = 0; i < iResolution * iResolution; i++ )
		pixels[ i * 4 + 3 ] = 255;
	FillEmptyParts( pixels.Base(), iResolution );

	IVTFTexture *pTexture = CreateVTFTexture();
	bool bSuccess = pTexture->Init( iResolution, iResolution, 1, IMAGE_FORMAT_RGBA8888,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOLOD, 1 );
	if( bSuccess )
	{
		V_memcpy( pTexture->ImageData( 0, 0, 0 ), pixels.Base(), iResolution * iResolution * 4 );
		pTexture->GenerateMipmaps();
		// Compressed like the official minimaps, uncompressed if that isn't possible
		pTexture->ConvertImageFormat( IMAGE_FORMAT_DXT1, false );
		if( pTexture->Format() != IMAGE_FORMAT_DXT1 )
			pTexture->ConvertImageFormat( IMAGE_FORMAT_BGR888, false );

		CUtlBuffer buf;
		bSuccess = pTexture->Serialize( buf ) && filesystem->WriteFile( s_Capture.path, NULL, buf );
		if( !bSuccess )
			*ppszError = "the minimap file couldn't be written";
	}
	else
	{
		*ppszError = "the minimap texture couldn't be created";
	}
	DestroyVTFTexture( pTexture );
	return bSuccess;
}

void WarsMinimapCapture_OnRenderedWorld( int x, int y, int width, int height )
{
	if( !s_Capture.bActive )
		return;
	if( ++s_Capture.iFrames < MINIMAP_CAPTURE_SETTLE_FRAMES )
		return;
	s_Capture.bActive = false;

	const char *pszError = "";
	bool bSuccess = WriteMinimap( x, y, width, height, &pszError );
	if( bSuccess )
	{
		s_Result.posX = s_Capture.vecCenter.x - s_Capture.flSize * 0.5f;
		s_Result.posY = s_Capture.vecCenter.y + s_Capture.flSize * 0.5f;
		s_Result.scale = s_Capture.flSize / 1024.0f;
	}
	SetResult( bSuccess, pszError );
}
