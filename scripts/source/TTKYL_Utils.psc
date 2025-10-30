scriptname TTKYL_Utils

Function RestoreAll(int ThreadID) global
    Actor[] actors = GetActors(ThreadID)
    KnowYourLimits.ResetScaledBones()
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

float Function GetDistance(float[] pos1, float[] pos2) global
    return Math.Sqrt( (pos1[0]-pos2[0])*(pos1[0]-pos2[0]) + (pos1[1]-pos2[1])*(pos1[1]-pos2[1]) + (pos1[2]-pos2[2])*(pos1[2]-pos2[2]) )
EndFunction

string Function GetPath(string fileName = "config.json") global
    return "../TT_KnowYourLimits/" + fileName
EndFunction

bool Function HasAction(string actionName, string actionType) global
    return JsonUtil.FindPathStringElement(GetPath(), actionType + ".actions", actionName) != -1
EndFunction
string Function GetBoneName(string actionType, string defaultBone) global
    return JsonUtil.GetPathStringValue(GetPath(), actionType + ".bone", defaultBone)
EndFunction

string Function GetHeadBoneName() global
    return GetBoneName("oral", "NPC Head [Head]")
EndFunction
string Function GetVaginalBoneName() global
    return GetBoneName("vaginal", "NPC Pelvis [Pelv]")
EndFunction
string Function GetAnalBoneName() global
    return GetBoneName("anal", "NPC Spine [Spn0]")
EndFunction


float Function GetThreshold(Actor akActor, string actionType) global
    int size = GetPenisSize(akActor)
    if(JsonUtil.IsPathNumber(GetPath(), "sizes." + size +actionType+".threshold"))
        return JsonUtil.GetPathFloatValue(GetPath(), "sizes." + size + actionType + ".threshold")
    endif
    return JsonUtil.GetPathFloatValue(GetPath(), "sizes.default"+actionType+".threshold", 5.0)
EndFunction
float Function GetOralThreshold(Actor akActor) global
    return GetThreshold(akActor, ".oral")
EndFunction
float Function GetVaginalThreshold(Actor akActor) global
    return GetThreshold(akActor, ".vaginal")
EndFunction
float Function GetAnalThreshold(Actor akActor) global
    return GetThreshold(akActor, ".anal")
EndFunction

string[] Function GetPenisBoneNames(Actor akActor, string actionType) global
    int size = GetPenisSize(akActor)
    if(JsonUtil.IsPathArray(GetPath(), "sizes." + size + actionType + ".penisBones"))
        return JsonUtil.PathStringElements(GetPath(), "sizes." + size + actionType + ".penisBones")
    endif
    return JsonUtil.PathStringElements(GetPath(), "sizes.default"+actionType+".penisBones")
EndFunction

int Function GetPenisSize(Actor akActor) global
    return TNG_PapyrusUtil.GetActorSize(akActor)
EndFunction

bool Function IsExcludedAnimation(string animationName) global
    return JsonUtil.StringListHas(GetPath("excludeAnimations.json"), "animationsIds", animationName)
EndFunction