scriptname TTKYL_Utils

float Function GetDistance(float[] pos1, float[] pos2) global
    return Math.Sqrt( (pos1[0]-pos2[0])*(pos1[0]-pos2[0]) + (pos1[1]-pos2[1])*(pos1[1]-pos2[1]) + (pos1[2]-pos2[2])*(pos1[2]-pos2[2]) )
EndFunction

string Function GetPath(string fileName = "config.json") global
    return "../KnowYourLimits/" + fileName
EndFunction

bool Function HasOStimAction(string actionName, string actionType) global
    return JsonUtil.FindPathStringElement(GetPath(), actionType + ".ostimActions", actionName) != -1
EndFunction
bool Function HasSexlabTag(string tagName, string actionType) global
    return JsonUtil.FindPathStringElement(GetPath(), actionType + ".sexlabTags", tagName) != -1
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

float Function GetThreshold(string actionType) global
    return JsonUtil.GetPathFloatValue(GetPath(), "."+actionType+".threshold", 5.0)
EndFunction
float Function GetRestoreThreshold(string actionType) global
    return JsonUtil.GetPathFloatValue(GetPath(), "."+actionType+".restoreThreshold", -5.0)
EndFunction
float Function GetOralThreshold() global
    return GetThreshold(".oral")
EndFunction
float Function GetOralRestoreThreshold() global
    return GetRestoreThreshold(".oral")
EndFunction
float Function GetVaginalThreshold() global
    return GetThreshold(".vaginal")
EndFunction
float Function GetVaginalRestoreThreshold() global
    return GetRestoreThreshold(".vaginal")
EndFunction
float Function GetAnalThreshold() global
    return GetThreshold(".anal")
EndFunction
float Function GetAnalRestoreThreshold() global
    return GetRestoreThreshold(".anal")
EndFunction

int Function GetIntervalMs() global
    return JsonUtil.GetPathIntValue(GetPath(), "general.intervalMs", 50)
EndFunction

Function ApplyIntervalFromConfig() global
    int intervalMs = GetIntervalMs()
    KnowYourLimits.SetTickInterval(intervalMs)
    MiscUtil.PrintConsole("TTKYL: Tick interval set to " + intervalMs + "ms from configuration")
EndFunction

string[] Function GetPenisBoneNames() global
    return JsonUtil.PathStringElements(GetPath(), ".penisBones")
EndFunction
