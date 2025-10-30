Scriptname TTKYL_MainController extends Quest  

Actor Property PlayerRef  Auto  

Int Function GetVersion()
    return 1
EndFunction

Event OnInit()
    Maintenance()
EndEvent

Function Maintenance()
    if Game.GetModByName("OStim.esp") != 255
        OstimSetup()
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






