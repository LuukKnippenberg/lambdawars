# Steamworks SDK headers for Workshop support

The Steam Workshop code (`game/client/lambdawars/wars_workshop_steam.cpp`) needs a newer
`ISteamUGC` than the old Steam headers in `src/public/steam`. It is built against the
headers of **Steamworks SDK 1.65**. The interfaces themselves are requested by version through
the engine's own steam api (`ISteamClient::GetISteamGenericInterface`), see the comment at the
top of that file.

These headers are not in the repository: the Steamworks SDK Access Agreement only allows
redistributing the `redistributable_bin` folder. Every developer has to copy them from their
own SDK download.

1. Download the Steamworks SDK from https://partner.steamgames.com/downloads/list
   (requires a free Steamworks account and accepting the SDK Access Agreement).
2. Copy these files from `sdk/public/steam/` into this folder:

   - `isteamremotestorage.h`
   - `isteamugc.h`
   - `isteamutils.h`
   - `steam_api_common.h`
   - `steam_api_internal.h`
   - `steamclientpublic.h`
   - `steamtypes.h`
   - `steamuniverse.h`

Only `wars_workshop_steam.cpp` may include these headers. They are wrapped in their own
namespace there, so they do not clash with the old Steam headers used by the rest of the code.
When updating to a different SDK version, check that the Steam client still provides the
`STEAMUGC_INTERFACE_VERSION` and `STEAMUTILS_INTERFACE_VERSION` of the new headers.
