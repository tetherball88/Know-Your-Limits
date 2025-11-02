Scriptname TTKYL_MainController extends Quest  

Actor Property PlayerRef  Auto  

SexLabFramework Property SexLab Auto

Int Function GetVersion()
    return 1
EndFunction

Event OnInit()
    Maintenance()
EndEvent

Function Maintenance()
    ; Apply interval from configuration
    TTKYL_Utils.ApplyIntervalFromConfig()

    
    
    if Game.GetModByName("OStim.esp") != 255
        OstimSetup()
    EndIf

    if Game.GetModByName("SexLab.esm") != 255
        SexLab = Game.GetFormFromFile(0xD62, "SexLab.esm") as SexLabFramework
        SexlabSetup()
    EndIf
EndFunction

Function OstimSetup()
    RegisterForModEvent("ostim_thread_start", "OStimStart")
    RegisterForModEvent("ostim_thread_scenechanged", "OStimSceneChanged")
    RegisterForModEvent("ostim_thread_end", "OStimEnd")
EndFunction

Function OStimStart(string EventName, string StrArg, float ThreadID, Form Sender)
    TTKYL_OStim.OnStart(EventName, StrArg, ThreadID, Sender)
EndFunction

Function OStimEnd(string EventName, string StrArg, float ThreadID, Form Sender)
    TTKYL_OStim.OnEnd(EventName, StrArg, ThreadID, Sender)
EndFunction

Function OStimSceneChanged(string EventName, string StrArg, float ThreadID, Form Sender)
    TTKYL_OStim.OnChange(EventName, StrArg, ThreadID, Sender)
EndFunction

Function SexlabSetup()
    RegisterForModEvent("HookAnimationStart", "SexlabHookStart")
    RegisterForModEvent("HookStageStart", "SexlabHookStageStart")
    RegisterForModEvent("HookStageEnd", "SexlabHookStageEnd")
    RegisterForModEvent("HookAnimationEnd", "SexlabHookEnd")
EndFunction

Function SexlabHookStart(int ThreadID, bool HasPlayer)
    TKYL_Sexlab.OnHookStart(SexLab, ThreadID, HasPlayer)
EndFunction

Function SexlabHookStageStart(int ThreadID, bool HasPlayer)
    TKYL_Sexlab.OnHookStageStart(SexLab, ThreadID, HasPlayer)
EndFunction

Function SexlabHookStageEnd(int ThreadID, bool HasPlayer)
    TKYL_Sexlab.OnHookStageEnd(SexLab, ThreadID, HasPlayer)
EndFunction

Function SexlabHookEnd(int ThreadID, bool HasPlayer)
    TKYL_Sexlab.OnHookEnd(SexLab, ThreadID, HasPlayer)
EndFunction

