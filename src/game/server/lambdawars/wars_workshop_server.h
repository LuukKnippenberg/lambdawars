//====== Copyright © Sandern Corporation, All rights reserved. ===========//
//
// Purpose: Downloads and mounts Steam Workshop items on dedicated servers.
//
//=============================================================================//

#ifndef WARS_WORKSHOP_SERVER_H
#define WARS_WORKSHOP_SERVER_H

#ifdef _WIN32
#pragma once
#endif

// Polls pending downloads. Called every server frame, also without a map loaded.
void WarsWorkshopServer_Update();

#endif // WARS_WORKSHOP_SERVER_H
