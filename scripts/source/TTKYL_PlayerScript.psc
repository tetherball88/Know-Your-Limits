Scriptname TTKYL_PlayerScript extends ReferenceAlias

Event OnPlayerLoadGame()
    TTKYL_MainController mainController = self.GetOwningQuest() as TTKYL_MainController
    mainController.Maintenance()
EndEvent