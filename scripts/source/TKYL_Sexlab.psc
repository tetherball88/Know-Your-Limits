scriptname TKYL_Sexlab

Function OnHookStart(SexLabFramework SexLab, int ThreadID, bool HasPlayer) global
    ; TODO implementation
EndFunction

Function OnHookStageStart(SexLabFramework SexLab, int ThreadID, bool HasPlayer) global
    ; TODO implementation
    ; For exmample check if it has some tags specified in config.json
    ; TTKYL_Utils.HasSexlabTag(tagName, ".oral") - to check tags of oral type
    ; TTKYL_Utils.HasSexlabTag(tagName, ".vaginal") - to check tags of vaginal type
    ; TTKYL_Utils.HasSexlabTag(tagName, ".anal") - to check tags of anal type
    ; You can read other values from config using function from TTKYL_Utils.psc
EndFunction

Function OnHookStageEnd(SexLabFramework SexLab, int ThreadID, bool HasPlayer) global
    ; TODO implementation
EndFunction

Function OnHookEnd(SexLabFramework SexLab, int ThreadID, bool HasPlayer) global
    ; TODO implementation
EndFunction