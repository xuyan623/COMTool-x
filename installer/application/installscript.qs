// CoRig keeps program files, plugins and personal projects in one folder, so
// the installer owns program files while the user owns projects and imports.
// Uninstall asks once whether that user content should survive.
function Component()
{
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "InstallScope", QInstaller.TargetDirectory);
        installer.setValue("TargetDir", installer.environmentVariable("LOCALAPPDATA") + "/Programs/CoRig");
    }
    // Default to keeping projects: deleting them cannot be undone.
    if (!installer.containsValue("RemoveUserData")) installer.setValue("RemoveUserData", "false");
    if (installer.isUninstaller() && !installer.isCommandLineInstance()) {
        if (installer.addWizardPage(component, "UninstallOptions", QInstaller.ReadyForInstallation)) {
            var page = gui.pageWidgetByObjectName("UninstallOptions");
            if (page != null) page.KeepUserData.checked = installer.value("RemoveUserData") !== "true";
            var entry = gui.pageByObjectName("UninstallOptions");
            if (entry != null) entry.entered.connect(function() {
                var widget = gui.pageWidgetByObjectName("UninstallOptions");
                if (widget != null) widget.KeepUserData.checked = installer.value("RemoveUserData") !== "true";
            });
        }
    }
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.productType !== "windows") return;
    if (installer.isUninstaller()) {
        var widget = gui.pageWidgetByObjectName("UninstallOptions");
        // An explicit widget state wins; the command line keeps the stored value.
        var remove = installer.value("RemoveUserData") === "true";
        if (widget != null) remove = !widget.KeepUserData.checked;
        installer.setValue("RemoveUserData", remove ? "true" : "false");
        if (remove) {
            // Only paths under the installation root are ever removed.
            component.addOperation("Execute", "cmd", "/c", "rmdir", "/s", "/q", "\\\"@TargetDir@\\project\\\"");
            component.addOperation("Execute", "cmd", "/c", "rmdir", "/s", "/q", "\\\"@TargetDir@\\plugins\\\"");
            component.addOperation("Execute", "cmd", "/c", "rmdir", "/s", "/q", "\\\"@TargetDir@\\cache\\\"");
            component.addOperation("Delete", "@TargetDir@/settings.ini");
        }
        return;
    }
    // Updating must never rewrite the folders that hold user content.
    component.addOperation("Mkdir", "@TargetDir@/project");
    component.addOperation("Mkdir", "@TargetDir@/plugins");
    var menu = "@UserStartMenuProgramsPath@/CoRig";
    if (installer.value("CreateShortcuts", "true") === "true") {
        component.addOperation("CreateShortcut", "@TargetDir@/CoRig.exe", menu + "/联调 CoRig.lnk", "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/CoRig.exe");
        component.addOperation("CreateShortcut", "@TargetDir@/CoRigMaintenanceTool.exe", menu + "/更新 CoRig.lnk", "workingDirectory=@TargetDir@");
        component.addOperation("CreateShortcut", "@TargetDir@/CoRig-uninstall.exe", menu + "/卸载 CoRig.lnk", "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/CoRig.exe");
    }
    if (installer.value("RegisterFileTypes", "true") === "true") {
        component.addOperation("RegisterFileType", "crpj", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 工程", "application/x-corig-project", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Project");
        component.addOperation("RegisterFileType", "crpl", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 插件包", "application/x-corig-plugin", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Plugin");
    }
}
