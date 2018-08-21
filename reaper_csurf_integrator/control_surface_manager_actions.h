//
//  control_surface_manager_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_manager_actions_h
#define control_surface_manager_actions_h

#include "control_surface_action_contexts.h"

extern Manager* TheManager;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapTrackToWidgets  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, RealSurface* surface) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->UnmapWidgetsFromTrack();
                page->MapTrackToWidgets(surface, track);
            }
        }
        else
            page->UnmapWidgetsFromTrack();
    }
    
    void Do(Page* page, RealSurface* surface, double value) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->ToggleMapTrackToWidgets(surface, track);
            }
        }
        else
            page->UnmapWidgetsFromTrack();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapFXToWidgets  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, RealSurface* surface) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->UnmapWidgetsFromFX();
                page->MapFXToWidgets(track);
            }
        }
        else
            page->UnmapWidgetsFromFX();
    }
    
    void Do(Page* page, RealSurface* surface, double value) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->ToggleMapFXToWidgets(surface, track);
            }
        }
        else
            page->UnmapWidgetsFromFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MapTrackAndFXToWidgets  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, RealSurface* surface) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->UnmapWidgetsFromTrackAndFX();
                page->MapTrackAndFXToWidgets(surface, track);
            }
        }
        else
            page->UnmapWidgetsFromTrackAndFX();
    }

    void Do(Page* page, RealSurface* surface, double value) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            MediaTrack* track = nullptr;
            
            for(int i = 0; i < CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    track = DAW::CSurf_TrackFromID(i, page->GetFollowMCP());
                    break;
                }
            
            if(track)
            {
                page->ToggleMapTrackAndFXToWidgets(surface, track);
            }
        }
        else
            page->UnmapWidgetsFromTrackAndFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectTrackRelative : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double stride) override
    {
        if(1 == DAW::CountSelectedTracks(nullptr))
        {
            int trackIndex = 0;
            
            for(int i = 0; i < DAW::CSurf_NumTracks(page->GetFollowMCP()); i++)
                if(DAW::GetMediaTrackInfo_Value(DAW::CSurf_TrackFromID(i, page->GetFollowMCP()), "I_SELECTED"))
                {
                    trackIndex = i;
                    break;
                }
            
            trackIndex += stride;
            
            if(trackIndex < 0)
                trackIndex = 0;
            
            if(trackIndex > DAW::CSurf_NumTracks(page->GetFollowMCP()))
                trackIndex = DAW::CSurf_NumTracks(page->GetFollowMCP());
            
            DAW::SetOnlyTrackSelected(DAW::CSurf_TrackFromID(trackIndex, page->GetFollowMCP()));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetShowFXWindows : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        actionContext->SetWidgetValue(widget, 0, page->GetShowFXWindows());
    }
    
    void Do(Page* page, double value) override
    {
        page->SetShowFXWindows(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetScrollLink : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void RequestUpdate(Page* page, ActionContext* actionContext, Widget* widget) override
    {
        actionContext->SetWidgetValue(widget, 0, page->GetScrollLink());
    }
    
    void Do(Page* page, double value) override
    {
        page->SetScrollLink(! page->GetScrollLink());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeDisplayModes : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        page->CycleTimeDisplayModes();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class NextPage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        TheManager->NextPage();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double stride) override
    {
        TheManager->AdjustTrackBank(page, stride);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double stride) override
    {
        page->AdjustTrackSendBank(stride);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PinSelectedTracks : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        page->PinSelectedTracks();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class UnpinSelectedTracks : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    void Do(Page* page, double value) override
    {
        page->UnpinSelectedTracks();
    }
};

#endif /* control_surface_manager_actions_h */
