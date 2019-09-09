#ifndef NULLSOFT_API_H
#define NULLSOFT_API_H

#include <sdk/nu/servicebuilder.h>

#include <api/service/api_service.h>

#include <api/service/waServiceFactory.h>

#include <sdk/Agave/AlbumArt/api_albumart.h>
extern api_albumart *AGAVE_API_ALBUMART;

#include <api/service/svcs/svc_imgload.h>
#include <api/service/svcs/svc_imgwrite.h>

#include <api/memmgr/api_memmgr.h>

#include <playlist/api_playlists.h>
extern api_playlists *playlistsApi;
#define AGAVE_API_PLAYLISTS playlistsApi

#include <Agave/Language/api_language.h>

#include <Agave/ExplorerFindFile/api_explorerfindfile.h>

#include <loader/hook/api_skin.h>
#define WASABI_API_SKIN skinApi

#endif