//=============================================================================//
//
// Purpose: Captures the minimap (overview) of the current map. The camera is
//			placed high above the center of the playable area, looking down,
//			and the rendered world is written as a vtf, before the HUD is drawn.
//
//=============================================================================//

#ifndef WARS_MINIMAPCAPTURE_H
#define WARS_MINIMAPCAPTURE_H

#ifdef _WIN32
#pragma once
#endif

class CViewSetup;

struct WarsMinimapCaptureResult_t
{
	bool bDone;
	bool bSuccess;
	char error[256];
	// Values for the res file of the map: top left corner of the minimap and world units per pixel of 1024
	float posX;
	float posY;
	float scale;
};

// Starts capturing the minimap of the current map, written as vtf to pszPath (absolute) with a size of
// iResolution pixels. Returns false if it can't start, the result has the reason.
bool WarsMinimapCapture_Start( const char *pszPath, int iResolution );
void WarsMinimapCapture_Cancel();
bool WarsMinimapCapture_IsActive();
const WarsMinimapCaptureResult_t &WarsMinimapCapture_GetResult();

// Places the camera above the playable area while capturing. Returns true if the view was changed.
bool WarsMinimapCapture_SetupView( CViewSetup &view );
// While capturing, clears to a dark color instead of the fog color, where no geometry is drawn.
// Returns true if the clear color was set.
bool WarsMinimapCapture_SetClearColor();
// Reads the rendered world, called before the HUD is drawn
void WarsMinimapCapture_OnRenderedWorld( int x, int y, int width, int height );

#endif // WARS_MINIMAPCAPTURE_H
