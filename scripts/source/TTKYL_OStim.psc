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
    if(TTKYL_Utils.IsExcludedAnimation(sceneId))
        TTKYL_Utils.RestoreAll(ThreadID)
        return
    endif
    int[] actions = OMetadata.FindActionsSuperloadCSVv2(sceneId)
    int i = 0
    bool scaledDown = false
    KnowYourLimits.StopBoneMonitor()
    if(actions.Length == 0)
        TTKYL_Utils.RestoreAll(ThreadID)
        return
    endif
    
    while(i < actions.Length)
        string actionName = OMetadata.GetActionType(sceneId, actions[i])
        if(TTKYL_Utils.HasAction(actionName, "oral"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                TTKYL_Utils.GetPenisBoneNames(withPenis, ".oral"), \
                OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i])), \
                TTKYL_Utils.GetHeadBoneName(), \
                5.0, \
                TTKYL_Utils.GetOralThreshold(withPenis) \
            )
        elseif(TTKYL_Utils.HasAction(actionName, "vaginal"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                TTKYL_Utils.GetPenisBoneNames(withPenis, ".vaginal"), \
                OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i])), \
                TTKYL_Utils.GetVaginalBoneName(), \
                5.0, \
                TTKYL_Utils.GetVaginalThreshold(withPenis) \
            )
        elseif(TTKYL_Utils.HasAction(actionName, "anal"))
            Actor withPenis = OThread.GetActor(ThreadID, OMetadata.GetActionActor(sceneId, actions[i]))
            KnowYourLimits.RegisterBoneMonitor( \
                withPenis, \
                TTKYL_Utils.GetPenisBoneNames(withPenis, ".anal"), \
                OThread.GetActor(ThreadID, OMetadata.GetActionTarget(sceneId, actions[i])), \
                TTKYL_Utils.GetAnalBoneName(), \
                5.0, \
                TTKYL_Utils.GetAnalThreshold(withPenis) \
            )
        else
            TTKYL_Utils.RestoreAll(ThreadID)
        endif
        i += 1
    endwhile
EndFunction