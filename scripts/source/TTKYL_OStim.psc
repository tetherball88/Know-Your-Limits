scriptname TTKYL_OStim

Function OnStart(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    TTKYL_Utils.ClearActors(ThreadID)
    TTKYL_Utils.StoreActors(ThreadID)
    TTKYL_Utils.RestoreAll(ThreadID)
EndFunction

Function OnEnd(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    TTKYL_Utils.RestoreAll(ThreadID)
    KnowYourLimits.StopBoneMonitor()
    Utility.Wait(1)
    TTKYL_Utils.ClearActors(ThreadID)
EndFunction

Function OnChange(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    string sceneId = OThread.GetScene(ThreadID)
    TTKYL_Utils.RestoreAll(ThreadID)
    KnowYourLimits.StopBoneMonitor()
    
    ; wait here to allow animation to transition to new scene and don't catch fake min/max positions during movement
    Utility.Wait(0.1)
    int[] actions = OMetadata.FindActionsSuperloadCSVv2(sceneId)
    int i = 0
    bool scaledDown = false

    string[] penisBones = TTKYL_Utils.GetPenisBoneNames()
        
    while(i < actions.Length)
        string actionName = OMetadata.GetActionType(sceneId, actions[i])
        if(TTKYL_Utils.HasOStimAction(actionName, ".oral"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                penisBones, \
                OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i])), \
                TTKYL_Utils.GetHeadBoneName(), \
                TTKYL_Utils.GetOralThreshold(), \
                TTKYL_Utils.GetOralRestoreThreshold() \
            )
        elseif(TTKYL_Utils.HasOStimAction(actionName, ".vaginal"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                penisBones, \
                OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i])), \
                TTKYL_Utils.GetVaginalBoneName(), \
                TTKYL_Utils.GetVaginalThreshold(), \
                TTKYL_Utils.GetVaginalRestoreThreshold() \
            )
        elseif(TTKYL_Utils.HasOStimAction(actionName, ".anal"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                penisBones, \
                OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i])), \
                TTKYL_Utils.GetAnalBoneName(), \
                TTKYL_Utils.GetAnalThreshold(), \
                TTKYL_Utils.GetAnalRestoreThreshold() \
            )
        endif
        i += 1
    endwhile
EndFunction