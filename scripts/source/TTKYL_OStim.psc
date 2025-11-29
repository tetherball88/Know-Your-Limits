scriptname TTKYL_OStim

Function OnStart(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    ClearActors(ThreadID)
    StoreActors(ThreadID)
    RestoreAll(ThreadID)
EndFunction

Function OnEnd(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    RestoreAll(ThreadID)
    KnowYourLimits.StopBoneMonitor(GetActors(ThreadID))
    Utility.Wait(1)
    ClearActors(ThreadID)
EndFunction

Function OnChange(string EventName, string StrArg, float numArg, Form Sender) global
    int ThreadID = numArg as int
    string sceneId = OThread.GetScene(ThreadID)
    RestoreAll(ThreadID)
    KnowYourLimits.StopBoneMonitor(GetActors(ThreadID))
    
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

Function RestoreAll(int ThreadID) global
    Actor[] actors = GetActors(ThreadID)
    KnowYourLimits.ResetScaledBones(actors)
EndFunction

Function StoreActors(int ThreadID, string prefix = "OStim") global
    Actor[] actors = OThread.GetActors(ThreadID)
    int i = 0
    while(i < actors.Length)
        StorageUtil.FormListAdd(none, prefix + "Thread" + ThreadID + "_Actors", actors[i])
        i += 1
    endwhile
EndFunction

Actor[] Function GetActors(int ThreadID, string prefix = "OStim") global
    int count = StorageUtil.FormListCount(none, prefix + "Thread" + ThreadID + "_Actors")
    Actor[] actors = PapyrusUtil.ActorArray(count)
    int i = 0
    while(i < count)
        actors[i] = StorageUtil.FormListGet(none, prefix + "Thread" + ThreadID + "_Actors", i) as Actor
        i += 1
    endwhile
    return actors
EndFunction

Function ClearActors(int ThreadID, string prefix = "OStim") global
    StorageUtil.FormListClear(none, prefix + "Thread" + ThreadID + "_Actors")
EndFunction